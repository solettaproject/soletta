/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sol-flow/http-server.h"
#include "sol-flow.h"
#include "sol-http.h"
#include "sol-http-server.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-flow-internal.h"

#define HTTP_HEADER_ACCEPT "Accept"
#define HTTP_HEADER_CONTENT_TYPE "Content-Type"
#define HTTP_HEADER_CONTENT_TYPE_TEXT "text/plain"
#define HTTP_HEADER_CONTENT_TYPE_JSON "application/json"

#define DOUBLE_STRING_LEN 64

struct server_data {
    struct sol_http_server *server;
    int port;
    uint16_t refcount;
};

struct http_data {
    union {
        struct sol_irange i;
        struct sol_drange d;
        struct sol_rgb rgb;
        struct sol_direction_vector dir_vector;
        struct sol_blob *blob;
        char *s;
        bool b;
    } value;

    struct server_data *sdata;
    char *path;
    char *basename;
    uint8_t allowed_methods;
};

struct http_server_node_type {
    struct sol_flow_node_type base;
    int (*post_cb)(struct http_data *mdata, struct sol_flow_node *node,
        struct sol_http_param_value *value);
    int (*response_cb)(struct http_data *mdata, struct sol_buffer *content,
        bool json);
    int (*process_cb)(struct http_data *mdata,
        const struct sol_flow_packet *packet);
    void (*send_packet_cb)(struct http_data *mdata, struct sol_flow_node *node);
    int (*handle_response_cb)(struct sol_flow_node *node,
        struct sol_http_request *request, struct sol_http_response *response,
        bool *updated);
};

static struct sol_ptr_vector servers = SOL_PTR_VECTOR_INIT;

#define STRTOL(field_, var_, changed_, is_unsigned_) \
    do { \
        errno = 0; \
        if ((is_unsigned_)) \
            var_ = sol_util_strtoul(value->value.key_value.value.data, NULL, value->value.key_value.value.len, 0); \
        else \
            var_ = sol_util_strtol(value->value.key_value.value.data, NULL, value->value.key_value.value.len, 0); \
        if (errno != 0) { \
            return -errno; \
        } \
        if (mdata->value.field_ != (var_)) { \
            mdata->value.field_ = (var_); \
            changed_ = 1; \
        } \
    } while (0)

#define STRTOD(field_, ret_) \
    do { \
        double d; \
        errno = 0; \
        d = sol_util_strtodn(value->value.key_value.value.data, NULL, \
            value->value.key_value.value.len, false); \
        if ((fpclassify(mdata->value.field_) == FP_ZERO) && (errno != 0)) { \
            return -errno; \
        } \
        if (!sol_util_double_equal(mdata->value.field_, d)) { \
            mdata->value.field_ = d; \
            ret_ = 1; \
        } \
    } while (0)

static bool
is_method_allowed(const uint8_t allowed_methods, enum sol_http_method method)
{
    if (allowed_methods & (1 << method))
        return true;
    return false;
}

static void
servers_clear(void)
{
    if (sol_ptr_vector_get_len(&servers) != 0)
        return;

    sol_ptr_vector_clear(&servers);
}

static int
validate_port(int32_t port)
{
    if (port > UINT16_MAX) {
        SOL_WRN("Invalid server port (%" PRId32 "). It must be in range "
            "0 - (%" PRId32 "). Using default port  (%" PRId32 ").",
            port, UINT16_MAX, HTTP_SERVER_PORT);
        return HTTP_SERVER_PORT;
    }

    if (port < 0)
        return HTTP_SERVER_PORT;

    return port;
}

static struct server_data *
server_ref(int32_t opt_port)
{
    struct server_data *idata, *sdata = NULL;
    uint16_t i, port;

    port = validate_port(opt_port);

    SOL_PTR_VECTOR_FOREACH_IDX (&servers, idata, i) {
        if (idata->port == port) {
            sdata = idata;
            if (sdata->refcount == UINT16_MAX) {
                SOL_WRN("Server port %d reached its max refcount %" PRIu16,
                    sdata->port, UINT16_MAX);
                return NULL;
            }
            break;
        }
    }

    if (!sdata) {
        int r;

        sdata = calloc(1, sizeof(struct server_data));
        SOL_NULL_CHECK_GOTO(sdata, err_sdata);

        r = sol_ptr_vector_append(&servers, sdata);
        SOL_INT_CHECK_GOTO(r, < 0, err_vec);

        sdata->server = sol_http_server_new(port);
        SOL_NULL_CHECK_GOTO(sdata->server, err_server);

        sdata->port = port;
    }

    sdata->refcount++;

    return sdata;

err_server:
    sol_ptr_vector_remove(&servers, sdata);
err_vec:
    free(sdata);
err_sdata:
    servers_clear();

    return NULL;
}

static void
server_unref(struct server_data *sdata)
{
    sdata->refcount--;

    if (sdata->refcount > 0)
        return;

    sol_ptr_vector_remove(&servers, sdata);
    sol_http_server_del(sdata->server);
    free(sdata);
    servers_clear();
}

static int
common_handle_response_cb(struct sol_flow_node *node,
    struct sol_http_request *request, struct sol_http_response *response,
    bool *updated)
{
    int r;
    uint16_t idx;
    bool send_json, err_r;
    struct sol_http_param_value *value;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_server_node_type *type;

    send_json = false;

    type = (const struct http_server_node_type *)
        sol_flow_node_get_type(node);

    SOL_HTTP_PARAMS_FOREACH_IDX (sol_http_request_get_params(request),
        value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_POST_FIELD:
            r = type->post_cb(mdata, node, value);
            response->response_code = SOL_HTTP_STATUS_BAD_REQUEST;
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            if (r == 0)
                continue;

            *updated = true;
            break;
        case SOL_HTTP_PARAM_HEADER:
            if (sol_str_slice_str_caseeq(value->value.key_value.key,
                HTTP_HEADER_ACCEPT)) {
                if (sol_str_slice_str_contains(value->value.key_value.value,
                    HTTP_HEADER_CONTENT_TYPE_JSON))
                    send_json = true;
            }
            break;
        default:
            break;
        }
    }

    if (send_json) {
        r = sol_buffer_append_printf(&response->content,
            "{\"%s\":", mdata->path);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    r = type->response_cb(mdata, &response->content, send_json);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if (send_json) {
        r = sol_buffer_append_char(&response->content, '}');
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    err_r = sol_http_param_add(&response->param, SOL_HTTP_REQUEST_PARAM_HEADER(
        HTTP_HEADER_CONTENT_TYPE, (send_json) ? HTTP_HEADER_CONTENT_TYPE_JSON :
        HTTP_HEADER_CONTENT_TYPE_TEXT));

    if (!err_r) {
        r = -ENOMEM;
        SOL_WRN("Could not set the Content-Type");
        goto err_exit;
    }

    return 0;

err_exit:
    return r;
}

static int
common_response_cb(void *data, struct sol_http_request *request)
{
    int r = 0;
    enum sol_http_method method;
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_server_node_type *type;
    bool updated = false;
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .content = SOL_BUFFER_INIT_EMPTY,
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR
    };

    type = (const struct http_server_node_type *)
        sol_flow_node_get_type(node);

    method = sol_http_request_get_method(request);
    response.url = sol_http_request_get_url(request);

    if (!is_method_allowed(mdata->allowed_methods, method)) {
        SOL_INF("HTTP Method not allowed. Method: %d", (int)method);
        response.response_code = SOL_HTTP_STATUS_FORBIDDEN;
        r = sol_http_server_send_response(request, &response);
        if (r < 0)
            SOL_WRN("Could not send the forbidden response");
        return 0;
    }

    r = type->handle_response_cb(node, request, &response, &updated);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if (updated) {
        r = sol_http_server_set_last_modified(mdata->sdata->server,
            mdata->path, time(NULL));
        SOL_INT_CHECK_GOTO(r, < 0, end);

        if (sol_http_request_get_method(request) == SOL_HTTP_METHOD_POST)
            type->send_packet_cb(mdata, node);
    }

    response.response_code = SOL_HTTP_STATUS_OK;
    r = sol_http_server_send_response(request, &response);
    response.response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
    SOL_INT_CHECK_GOTO(r, < 0, end);

end:
    if (r < 0) {
        sol_buffer_reset(&response.content);
        sol_buffer_append_printf(&response.content,
            "Could not serve request: %s", sol_util_strerrora(-r));

        sol_http_params_clear(&response.param);
        r = sol_http_param_add(&response.param, SOL_HTTP_REQUEST_PARAM_HEADER(
            HTTP_HEADER_CONTENT_TYPE, HTTP_HEADER_CONTENT_TYPE_TEXT));
        if (r < 0)
            SOL_WRN("could not set response content-type: text/plain: %s",
                sol_util_strerrora(-r));

        /* response_code was set before goto, so use as-is */
        sol_http_server_send_response(request, &response);

        sol_flow_send_error_packet_str(node, -r, response.content.data);
    }

    sol_buffer_fini(&response.content);
    sol_http_params_clear(&response.param);

    return 0;
}

static int
start_server(struct http_data *http, struct sol_flow_node *node,
    const char *path, int32_t opt_port)
{
    int r = -ENOMEM;

    http->sdata = server_ref(opt_port);
    SOL_NULL_CHECK(http->sdata, r);

    http->path = strdup(path);
    SOL_NULL_CHECK_GOTO(http->path, err_path);

    r = sol_http_server_register_handler(http->sdata->server, http->path,
        common_response_cb, node);
    SOL_INT_CHECK_GOTO(r, < 0, err_handler);

    r = sol_http_server_set_last_modified(http->sdata->server, http->path,
        time(NULL));
    SOL_INT_CHECK_GOTO(r, < 0, err_handler);

    return 0;

err_handler:
    free(http->path);
err_path:
    server_unref(http->sdata);
    return r;
}

static void
stop_server(struct http_data *http)
{
    sol_http_server_unregister_handler(http->sdata->server, http->path);
    free(http->path);
    server_unref(http->sdata);
}

static void
common_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    stop_server(mdata);
}

static int
parse_allowed_methods(const char *allowed_methods_str, uint8_t *allowed_methods)
{
    if (!allowed_methods_str) {
        SOL_WRN("Allowed methods is NULL");
        return -EINVAL;
    }

    while (*allowed_methods_str) {
        const char *sep;
        struct sol_str_slice method;

        sep = strchr(allowed_methods_str, '|');

        if (!sep)
            sep = allowed_methods_str + strlen(allowed_methods_str);

        method.data = allowed_methods_str;
        method.len = sep - allowed_methods_str;

        if (sol_str_slice_str_eq(method, "GET"))
            *allowed_methods |= (1 << SOL_HTTP_METHOD_GET);
        else if (sol_str_slice_str_eq(method, "POST"))
            *allowed_methods |= (1 << SOL_HTTP_METHOD_POST);
        else {
            SOL_WRN("Unsupported allowed_method: %.*s",
                SOL_STR_SLICE_PRINT(method));
            return -EINVAL;
        }

        if (*sep == '|')
            allowed_methods_str = sep + 1;
        else
            break;
    }

    return 0;
}

static int
common_open(struct sol_flow_node *node, struct http_data *mdata,
    const char *path, const char *allowed_methods, const int32_t port)
{
    int r;

    r = parse_allowed_methods(allowed_methods, &mdata->allowed_methods);
    SOL_INT_CHECK(r, < 0, r);

    r = start_server(mdata, node, path, port);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
boolean_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_boolean_options *opts =
        (struct sol_flow_node_type_http_server_boolean_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    r = common_open(node, mdata, opts->path, opts->allowed_methods, opts->port);
    SOL_INT_CHECK(r, < 0, r);

    mdata->value.b = opts->value;

    return 0;
}

static int
int_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_int_options *opts =
        (struct sol_flow_node_type_http_server_int_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_INT_OPTIONS_API_VERSION,
        -EINVAL);

    r = sol_irange_compose(&opts->value_spec, opts->value, &mdata->value.i);
    SOL_INT_CHECK(r, < 0, r);

    r = common_open(node, mdata, opts->path, opts->allowed_methods, opts->port);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
float_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_float_options *opts =
        (struct sol_flow_node_type_http_server_float_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_FLOAT_OPTIONS_API_VERSION,
        -EINVAL);

    r = sol_drange_compose(&opts->value_spec, opts->value, &mdata->value.d);
    SOL_INT_CHECK(r, < 0, r);

    r = common_open(node, mdata, opts->path, opts->allowed_methods, opts->port);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
common_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct http_data *mdata = data;
    const struct http_server_node_type *type;
    int r;

    type = (const struct http_server_node_type *)
        sol_flow_node_get_type(node);

    r = type->process_cb(mdata, packet);
    SOL_INT_CHECK(r, < 0, r);
    if (r == 0)
        return 0;

    r = sol_http_server_set_last_modified(mdata->sdata->server,
        mdata->path, time(NULL));
    SOL_INT_CHECK(r, < 0, r);

    if (type->send_packet_cb)
        type->send_packet_cb(mdata, node);

    return 0;
}

static int
boolean_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    bool b;

    if (sol_str_slice_str_eq(value->value.key_value.value, "true"))
        b = true;
    else if (sol_str_slice_str_eq(value->value.key_value.value, "false"))
        b = false;
    else
        return -EINVAL;

    if (mdata->value.b == b)
        return 0;

    mdata->value.b = b;
    return 1;
}

static int
boolean_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r;

    r = sol_buffer_append_printf(content, "%s", mdata->value.b == true ? "true" : "false");
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static void
boolean_send_packet_cb(struct http_data *mdata, struct sol_flow_node *node)
{
    sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_BOOLEAN__OUT__OUT,
        mdata->value.b);
}

static int
boolean_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    bool b;
    int r;

    r = sol_flow_packet_get_boolean(packet, &b);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->value.b == b)
        return 0;

    mdata->value.b = b;
    return 1;
}

/* ------------------------------------- string ------------------------------------------- */

static int
string_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r = 0;

    if (json) {
        r = sol_json_serialize_string(content, mdata->value.s);
    } else {
        r = sol_buffer_append_slice(content, sol_str_slice_from_str(mdata->value.s));
    }

    return r;
}

static int
string_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    if (sol_str_slice_str_eq(value->value.key_value.key, "value")) {
        return sol_util_replace_str_from_slice_if_changed(&mdata->value.s,
            value->value.key_value.value);
    } else {
        return -EINVAL;
    }

    return 0;
}

static int
string_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    const char *val;
    int r;

    r = sol_flow_packet_get_string(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return sol_util_replace_str_if_changed(&mdata->value.s, val);
}

static void
string_send_packet_cb(struct http_data *mdata, struct sol_flow_node *node)
{
    sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_STRING__OUT__OUT,
        mdata->value.s);
}

static void
string_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    free(mdata->value.s);
    stop_server(mdata);
}

static int
string_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_string_options *opts =
        (struct sol_flow_node_type_http_server_string_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->value.s = strdup(opts->value);
    SOL_NULL_CHECK(mdata->value.s, -ENOMEM);

    r = common_open(node, mdata, opts->path, opts->allowed_methods, opts->port);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return 0;

err:
    free(mdata->value.s);
    return r;
}

/* ----------------------------------- int ------------------------------------------- */

static int
int_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    int ret = 0;
    int32_t i;

    if (sol_str_slice_str_eq(value->value.key_value.key, "value"))
        STRTOL(i.val, i, ret, false);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "min"))
        STRTOL(i.min, i, ret, false);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "max"))
        STRTOL(i.max, i, ret, false);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "step"))
        STRTOL(i.step, i, ret, false);
    else
        return -EINVAL;

    return ret;
}

static int
int_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r;

    if (json) {
        r = sol_buffer_append_printf(content, "{\"value\":%" PRId32
            ",\"min\":%" PRId32 ",\"max\":%" PRId32 ",\"step\":%" PRId32 "}",
            mdata->value.i.val, mdata->value.i.min, mdata->value.i.max,
            mdata->value.i.step);
    } else {
        r = sol_buffer_append_printf(content, "%" PRId32, mdata->value.i.val);
    }

    return r;
}

static void
int_send_packet_cb(struct http_data *mdata, struct sol_flow_node *node)
{
    sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_INT__OUT__OUT, &mdata->value.i);
}

static int
int_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    struct sol_irange i;
    int r;

    r = sol_flow_packet_get_irange(packet, &i);
    SOL_INT_CHECK(r, < 0, r);

    if (sol_irange_equal(&mdata->value.i, &i))
        return 0;

    memcpy(&mdata->value.i, &i, sizeof(i));
    return 1;
}

/* ------------------------------------------- float ------------------------------------------------- */

static int
float_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    int ret = 0;

    if (sol_str_slice_str_eq(value->value.key_value.key, "value"))
        STRTOD(d.val, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "min"))
        STRTOD(d.min, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "max"))
        STRTOD(d.max, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "step"))
        STRTOD(d.step, ret);
    else
        return -EINVAL;

    return ret;
}

static int
float_response_cb(struct http_data *mdata, struct sol_buffer *content,
    bool json)
{
    if (json) {
        int r;

        r = sol_buffer_append_slice(content,
            sol_str_slice_from_str("{\"value\":"));
        SOL_INT_CHECK(r, < 0, r);
        r = sol_json_double_to_str(mdata->value.d.val, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content,
            sol_str_slice_from_str(",\"min\":"));
        SOL_INT_CHECK(r, < 0, r);
        r = sol_json_double_to_str(mdata->value.d.min, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content,
            sol_str_slice_from_str(",\"max\":"));
        SOL_INT_CHECK(r, < 0, r);
        r = sol_json_double_to_str(mdata->value.d.max, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content,
            sol_str_slice_from_str(",\"step\":"));
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_double_to_str(mdata->value.d.step, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_char(content, '}');
        SOL_INT_CHECK(r, < 0, r);

        return 0;
    }

    return sol_json_double_to_str(mdata->value.d.val, content);
}

static void
float_send_packet_cb(struct http_data *mdata, struct sol_flow_node *node)
{
    sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_FLOAT__OUT__OUT, &mdata->value.d);
}

static int
float_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    struct sol_drange d;
    int r;

    r = sol_flow_packet_get_drange(packet, &d);
    SOL_INT_CHECK(r, < 0, r);

    if (sol_drange_equal(&mdata->value.d, &d))
        return 0;

    memcpy(&mdata->value.d, &d, sizeof(d));
    return 1;
}

static void
rgb_send_packet_cb(struct http_data *mdata, struct sol_flow_node *node)
{
    sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_RGB__OUT__OUT, &mdata->value.rgb);
}

static int
rgb_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    if (json) {
        return sol_buffer_append_printf(content,
            "{\"red\":%" PRIu32 ",\"green\":%" PRIu32 ",\"blue\":%" PRIu32
            ",\"red_max\":%" PRIu32 ",\"green_max\":%" PRIu32
            ",\"blue_max\":%" PRIu32 "}",
            mdata->value.rgb.red, mdata->value.rgb.green, mdata->value.rgb.blue,
            mdata->value.rgb.red_max, mdata->value.rgb.green_max,
            mdata->value.rgb.blue_max);
    }

    return sol_buffer_append_printf(content, "#%02X%02X%02X",
        mdata->value.rgb.red, mdata->value.rgb.green, mdata->value.rgb.blue);
}

static int
rgb_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    int ret = 0;
    uint32_t i;

    if (sol_str_slice_str_eq(value->value.key_value.key, "red"))
        STRTOL(rgb.red, i, ret, true);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "green"))
        STRTOL(rgb.green, i, ret, true);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "blue"))
        STRTOL(rgb.blue, i, ret, true);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "red_max"))
        STRTOL(rgb.red_max, i, ret, true);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "green_max"))
        STRTOL(rgb.green_max, i, ret, true);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "blue_max"))
        STRTOL(rgb.blue_max, i, ret, true);
    else
        return -EINVAL;

    return ret;
}

static int
rgb_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    return sol_flow_packet_get_rgb(packet, &mdata->value.rgb);
}

static int
rgb_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_rgb_options *opts =
        (struct sol_flow_node_type_http_server_rgb_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_RGB_OPTIONS_API_VERSION,
        -EINVAL);

    r = common_open(node, mdata, opts->path, opts->allowed_methods, opts->port);
    SOL_INT_CHECK(r, < 0, r);

    mdata->value.rgb = opts->value;
    return 0;
}

static int
direction_vector_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    int ret = 0;

    if (sol_str_slice_str_eq(value->value.key_value.key, "x"))
        STRTOD(dir_vector.x, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "y"))
        STRTOD(dir_vector.y, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "z"))
        STRTOD(dir_vector.z, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "min"))
        STRTOD(dir_vector.min, ret);
    else if (sol_str_slice_str_eq(value->value.key_value.key, "max"))
        STRTOD(dir_vector.max, ret);
    else
        return -EINVAL;

    return ret;
}

static int
direction_vector_response_cb(struct http_data *mdata,
    struct sol_buffer *content, bool json)
{
    int r;

    if (json) {

        r = sol_buffer_append_slice(content, sol_str_slice_from_str("{\"x\":"));
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_double_to_str(mdata->value.dir_vector.x, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content, sol_str_slice_from_str(",\"y\":"));
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_double_to_str(mdata->value.dir_vector.y, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content, sol_str_slice_from_str(",\"z\":"));
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_double_to_str(mdata->value.dir_vector.z, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content,
            sol_str_slice_from_str(",\"min\":"));
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_double_to_str(mdata->value.dir_vector.min, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(content,
            sol_str_slice_from_str(",\"max\":"));
        SOL_INT_CHECK(r, < 0, r);

        r = sol_json_double_to_str(mdata->value.dir_vector.max, content);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_char(content, '}');
        SOL_INT_CHECK(r, < 0, r);

        return 0;
    }

    //Format (X;Y;Z)
    r = sol_buffer_append_char(content, '(');
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(mdata->value.dir_vector.x, content);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_char(content, ';');
    SOL_INT_CHECK(r, < 0, r);
    r = sol_json_double_to_str(mdata->value.dir_vector.y, content);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_char(content, ';');
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(mdata->value.dir_vector.z, content);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_char(content, ')');
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static void
direction_vector_send_packet_cb(struct http_data *mdata,
    struct sol_flow_node *node)
{
    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_DIRECTION_VECTOR__OUT__OUT,
        &mdata->value.dir_vector);
}

static int
direction_vector_process_cb(struct http_data *mdata,
    const struct sol_flow_packet *packet)
{
    return sol_flow_packet_get_direction_vector(packet,
        &mdata->value.dir_vector);
}

static int
direction_vector_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_direction_vector_options *opts =
        (struct sol_flow_node_type_http_server_direction_vector_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_DIRECTION_VECTOR_OPTIONS_API_VERSION,
        -EINVAL);

    r = common_open(node, mdata, opts->path, opts->allowed_methods, opts->port);
    SOL_INT_CHECK(r, < 0, r);

    mdata->value.dir_vector = opts->value;
    return 0;
}

static void
blob_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    if (mdata->value.blob)
        sol_blob_unref(mdata->value.blob);
    stop_server(mdata);
}

static int
blob_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_blob_options *opts =
        (struct sol_flow_node_type_http_server_blob_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_BLOB_OPTIONS_API_VERSION,
        -EINVAL);

    r = common_open(node, mdata, opts->path, opts->allowed_methods,
        opts->port);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
replace_json_blob(struct http_data *mdata, struct sol_blob *blob)
{
    if (mdata->value.blob)
        sol_blob_unref(mdata->value.blob);

    mdata->value.blob = sol_blob_ref(blob);
    SOL_NULL_CHECK(mdata->value.blob, -ENOMEM);
    return 0;
}

static int
blob_process_cb(struct http_data *mdata,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *blob;
    const struct sol_flow_packet_type *packet_type;

    packet_type = sol_flow_packet_get_type(packet);

    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)
        r = sol_flow_packet_get_json_object(packet, &blob);
    else if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)
        r = sol_flow_packet_get_json_array(packet, &blob);
    else
        r = sol_flow_packet_get_blob(packet, &blob);

    SOL_INT_CHECK(r, < 0, r);
    return replace_json_blob(mdata, blob);
}

static void
blob_send_packet_cb(struct http_data *mdata,
    struct sol_flow_node *node)
{
    sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_BLOB__OUT__OUT, mdata->value.blob);
}

static int
blob_handle_response_cb(struct sol_flow_node *node,
    struct sol_http_request *request, struct sol_http_response *response,
    bool *updated)
{
    int r;
    struct http_data *mdata = sol_flow_node_get_private_data(node);

    if (sol_http_request_get_method(request) == SOL_HTTP_METHOD_POST) {
        struct sol_blob *blob = NULL;
        void *blob_mem = NULL;
        struct sol_http_param_value *param;
        uint16_t i;

        SOL_HTTP_PARAMS_FOREACH_IDX (sol_http_request_get_params(request),
            param, i) {
            if (param->type != SOL_HTTP_PARAM_POST_DATA)
                continue;

            blob_mem = malloc(param->value.data.value.len);
            SOL_NULL_CHECK(blob_mem, -ENOMEM);
            memcpy(blob_mem, param->value.data.value.data,
                param->value.data.value.len);
            blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
                blob_mem, param->value.data.value.len);
            break;
        }

        if (!blob && blob_mem) {
            free(blob_mem);
            SOL_WRN("Could not create a blob to hold the json data");
            return -ENOMEM;
        } else if (!blob && !blob_mem) {
            SOL_WRN("The POST request has no POST data!");
            return -ENOENT;
        }

        r = replace_json_blob(mdata, blob);
        sol_blob_unref(blob);
        SOL_INT_CHECK(r, < 0, r);
        *updated = true;
    }

    if (!mdata->value.blob) {
        response->response_code = SOL_HTTP_STATUS_NOT_FOUND;
        return 0;
    }

    r = sol_buffer_append_bytes(&response->content,
        (uint8_t *)mdata->value.blob->mem, mdata->value.blob->size);
    SOL_INT_CHECK(r, < 0, r);

    if (!sol_http_param_add(&response->param, SOL_HTTP_REQUEST_PARAM_HEADER(
        HTTP_HEADER_CONTENT_TYPE, HTTP_HEADER_CONTENT_TYPE_JSON))) {
        SOL_WRN("Could not set the Content-Type:application/json");
        return -ENOMEM;
    }

    return 0;
}

static void
json_send_packet_cb(struct http_data *mdata,
    struct sol_flow_node *node)
{
    struct sol_json_scanner object_scanner, array_scanner;

    sol_json_scanner_init(&object_scanner, mdata->value.blob->mem,
        mdata->value.blob->size);
    sol_json_scanner_init(&array_scanner, mdata->value.blob->mem,
        mdata->value.blob->size);

    if (sol_json_is_valid_type(&object_scanner, SOL_JSON_TYPE_OBJECT_START)) {
        sol_flow_send_json_object_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_SERVER_JSON__OUT__OBJECT,
            mdata->value.blob);
    } else if (sol_json_is_valid_type(&array_scanner,
        SOL_JSON_TYPE_ARRAY_START)) {
        sol_flow_send_json_array_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_SERVER_JSON__OUT__ARRAY,
            mdata->value.blob);
    } else {
        sol_flow_send_error_packet(node, EINVAL,
            "Invalid json format - It will not be sent");
        SOL_WRN("Invalid json format - It will not be sent");
    }
}

/* ---------------------------  static files ----------------------------------------- */

static int
static_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_static_options *opts =
        (struct sol_flow_node_type_http_server_static_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_STATIC_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->sdata = server_ref(opts->port);
    SOL_NULL_CHECK(mdata->sdata, -ENOMEM);

    mdata->path = strdup(opts->path);
    SOL_NULL_CHECK_GOTO(mdata->path, err);
    mdata->basename = strdup(opts->basename);
    SOL_NULL_CHECK_GOTO(mdata->basename, err_basename);

    mdata->value.b = opts->enabled;

    if (opts->enabled) {
        int r;
        r = sol_http_server_add_dir(mdata->sdata->server,
            mdata->basename, mdata->path);
        SOL_INT_CHECK_GOTO(r, < 0, err_add);
    }

    return 0;

err_add:
    free(mdata->basename);
err_basename:
    free(mdata->path);
err:
    server_unref(mdata->sdata);
    return -ENOMEM;
}

static void
static_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    if (mdata->value.b)
        sol_http_server_remove_dir(mdata->sdata->server,
            mdata->basename, mdata->path);

    server_unref(mdata->sdata);
    free(mdata->path);
    free(mdata->basename);
}

static int
static_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    bool val;
    struct http_data *mdata = data;

    r = sol_flow_packet_get_boolean(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->value.b == val)
        return 0;

    mdata->value.b = val;
    if (mdata->value.b)
        return sol_http_server_add_dir(mdata->sdata->server, mdata->basename, mdata->path);
    else
        return sol_http_server_remove_dir(mdata->sdata->server, mdata->basename, mdata->path);
}

#include "http-server-gen.c"
