/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#include "sol-util.h"
#include "sol-vector.h"
#include "sol_config.h"

#define HTTP_HEADER_ACCEPT "Accept"
#define HTTP_HEADER_CONTENT_TYPE "Content-Type"
#define HTTP_HEADER_CONTENT_TYPE_TEXT "text/plain"
#define HTTP_HEADER_CONTENT_TYPE_JSON "application/json"

#define DOUBLE_STRING_LEN 64

struct http_data {
    union {
        struct sol_irange i;
        struct sol_drange d;
        bool b;
        char *s;
    } value;

    char *path;
    char *namespace;
};

struct http_server_node_type {
    struct sol_flow_node_type base;
    int (*post_cb)(struct http_data *mdata, struct sol_flow_node *node,
        struct sol_http_param_value *value);
    int (*response_cb)(struct http_data *mdata, struct sol_buffer *content, bool json);
    int (*process_cb)(struct http_data *mdata, const struct sol_flow_packet *packet);
    void (*send_packet_cb)(struct http_data *mdata, struct sol_flow_node *node);
};

static struct sol_http_server *server = NULL;
static int init_count = 0;

static int
server_ref(void)
{
    if (!server) {
        server = sol_http_server_new(HTTP_SERVER_PORT);
        SOL_NULL_CHECK(server, -ENOMEM);
    }
    init_count++;
    return 0;
}

static void
server_unref(void)
{
    init_count--;
    if (!init_count) {
        sol_http_server_del(server);
        server = NULL;
    }
}

static int
common_response_cb(void *data, struct sol_http_request *request)
{
    int r = 0;
    uint16_t idx;
    bool send_json = false;
    enum sol_http_method method;
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_server_node_type *type;
    struct sol_http_param_value *value;
    struct sol_http_response response = {
        .api_version = SOL_HTTP_RESPONSE_API_VERSION,
        .content = SOL_BUFFER_INIT_EMPTY,
        .param = SOL_HTTP_REQUEST_PARAM_INIT,
        .response_code = SOL_HTTP_STATUS_OK
    };

    type = (const struct http_server_node_type *)
        sol_flow_node_get_type(node);

    method = sol_http_request_get_method(request);
    response.url = sol_http_request_get_url(request);

    SOL_HTTP_PARAM_FOREACH_IDX (sol_http_request_get_params(request), value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_POST_FIELD:
            r = type->post_cb(mdata, node, value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_http_server_set_last_modified(server, mdata->path, time(NULL));
            SOL_INT_CHECK_GOTO(r, < 0, end);
            break;
        case SOL_HTTP_PARAM_HEADER:
            if (streq(value->value.key_value.key, HTTP_HEADER_ACCEPT)) {
                if (strstr(value->value.key_value.value, HTTP_HEADER_CONTENT_TYPE_JSON))
                    send_json = true;
            }
            break;
        default:
            break;
        }
    }

    if (send_json) {
        r = sol_buffer_append_printf(&response.content, "{\"%s\":", mdata->path);
        SOL_INT_CHECK_GOTO(r, < 0, end);
    }

    r = type->response_cb(mdata, &response.content, send_json);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if (send_json) {
        r = sol_buffer_append_char(&response.content, '}');
        SOL_INT_CHECK_GOTO(r, < 0, end);
    }

    r = sol_http_param_add(&response.param, SOL_HTTP_REQUEST_PARAM_HEADER(
        HTTP_HEADER_CONTENT_TYPE, (send_json) ? HTTP_HEADER_CONTENT_TYPE_JSON : HTTP_HEADER_CONTENT_TYPE_TEXT));
    SOL_INT_CHECK_GOTO(r, != true, end);

    r = sol_http_server_send_response(request, &response);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if ((method == SOL_HTTP_METHOD_POST) && type->send_packet_cb)
        type->send_packet_cb(mdata, node);

end:
    sol_buffer_fini(&response.content);
    sol_http_param_free(&response.param);

    return 0;
}

static int
start_server(struct http_data *http, struct sol_flow_node *node,
    const char *path)
{
    int r;

    r = server_ref();
    SOL_INT_CHECK(r, < 0, r);

    http->path = strdup(path);
    SOL_NULL_CHECK_GOTO(http->path, err);

    r = sol_http_server_register_handler(server, http->path, common_response_cb, node);
    SOL_INT_CHECK_GOTO(r, < 0, err_handler);

    return 0;

err_handler:
    free(http->path);
err:
    server_unref();
    return -1;
}

static void
stop_server(struct http_data *http)
{
    sol_http_server_unregister_handler(server, http->path);
    free(http->path);
    server_unref();
}

static void
common_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    stop_server(mdata);
}

static int
common_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_boolean_options *opts =
        (struct sol_flow_node_type_http_server_boolean_options *)options;

    r = start_server(mdata, node, opts->path);
    SOL_INT_CHECK(r, < 0, r);

    mdata->value.b = opts->value;

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

    r = sol_http_server_set_last_modified(server, mdata->path, time(NULL));
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
boolean_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    if (streq(value->value.key_value.value, "true"))
        mdata->value.b = true;
    else if (streq(value->value.key_value.value, "false"))
        mdata->value.b = false;
    else
        return -EINVAL;

    sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_BOOLEAN__OUT__OUT,
        mdata->value.b);

    return 0;
}

static int
boolean_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r;

    r = sol_buffer_append_printf(content, "%s", mdata->value.b == true ? "true" : "false");
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
boolean_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    return sol_flow_packet_get_boolean(packet, &mdata->value.b);
}

/* ------------------------------------- string ------------------------------------------- */

static int
string_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r = 0;

    if (json) {
        size_t escaped_len = sol_json_calculate_escaped_string_len(mdata->value.s);
        r = sol_buffer_ensure(content, content->used + escaped_len + 2);
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_char(content, '\"');
        SOL_INT_CHECK(r, < 0, r);

        sol_json_escape_string(mdata->value.s, sol_buffer_at_end(content), escaped_len);
        content->used += escaped_len - 1; /* remove \0 in the result */

        r = sol_buffer_append_char(content, '\"');
        SOL_INT_CHECK(r, < 0, r);
    } else {
        r = sol_buffer_append_slice(content, sol_str_slice_from_str(mdata->value.s));
    }

    return r;
}

static int
string_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
    if (streq(value->value.key_value.key, "value")) {
        int ret = sol_util_replace_str_if_changed(&mdata->value.s,
            value->value.key_value.value);
        SOL_INT_CHECK(ret, < 0, ret);
    } else {
        return -EINVAL;
    }

    sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_SERVER_STRING__OUT__OUT,
        mdata->value.s);

    return 0;
}

static int
string_process_cb(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    const char *val;
    int r;

    r = sol_flow_packet_get_string(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->value.s);
    mdata->value.s = strdup(val);
    SOL_NULL_CHECK(mdata->value.s, -ENOMEM);

    return 0;
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

    mdata->value.s = strdup(opts->value);
    SOL_NULL_CHECK(mdata->value.s, -ENOMEM);

    r = start_server(mdata, node, opts->path);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return 0;

err:
    free(mdata->value.s);
    free(mdata->path);
    return -1;
}

/* ----------------------------------- int ------------------------------------------- */

static int
int_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
#define STRTOL_(field_) \
    do { \
        errno = 0; \
        mdata->value.i.field_ = strtol(value->value.key_value.value, NULL, 0); \
        if (errno != 0) { \
            return -errno; \
        } \
    } while (0)

    if (streq(value->value.key_value.key, "value"))
        STRTOL_(val);
    else if (streq(value->value.key_value.key, "min"))
        STRTOL_(min);
    else if (streq(value->value.key_value.key, "max"))
        STRTOL_(max);
    else if (streq(value->value.key_value.key, "step"))
        STRTOL_(step);
    else
        return -EINVAL;
#undef STRTOL_

    return 0;
}

static int
int_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r;

    if (json) {
        r = sol_buffer_append_printf(content, "{\"value\":%d,\"min\":%d,\"max\":%d,\"step\":%d}",
            mdata->value.i.val, mdata->value.i.min, mdata->value.i.max, mdata->value.i.step);
    } else {
        r = sol_buffer_append_printf(content, "%d", mdata->value.i.val);
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
    return sol_flow_packet_get_irange(packet, &mdata->value.i);
}

/* ------------------------------------------- float ------------------------------------------------- */

static int
float_post_cb(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_param_value *value)
{
#define STRTOD_(field_) \
    do { \
        errno = 0; \
        mdata->value.d.field_ = sol_util_strtodn(value->value.key_value.value, NULL, \
            -1, false); \
        if ((fpclassify(mdata->value.d.field_) == FP_ZERO) && (errno != 0)) { \
            return -errno; \
        } \
    } while (0)

    if (streq(value->value.key_value.key, "value"))
        STRTOD_(val);
    else if (streq(value->value.key_value.key, "min"))
        STRTOD_(min);
    else if (streq(value->value.key_value.key, "max"))
        STRTOD_(max);
    else if (streq(value->value.key_value.key, "step"))
        STRTOD_(step);
    else
        return -EINVAL;
#undef STRTOD_

    return 0;
}

static int
float_response_cb(struct http_data *mdata, struct sol_buffer *content, bool json)
{
    int r;
    char val[DOUBLE_STRING_LEN], min[DOUBLE_STRING_LEN],
        max[DOUBLE_STRING_LEN], step[DOUBLE_STRING_LEN];

    r = sol_json_double_to_str(mdata->value.d.val, val, sizeof(val));
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(mdata->value.d.min, min, sizeof(min));
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(mdata->value.d.max, max, sizeof(max));
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(mdata->value.d.step, step, sizeof(step));
    SOL_INT_CHECK(r, < 0, r);

    if (json) {
        r = sol_buffer_append_printf(content, "{\"value\":%s,\"min\":%s,\"max\":%s,\"step\":%s}",
            val, min, max, step);
    } else {
        r = sol_buffer_append_slice(content, sol_str_slice_from_str(val));
    }

    return r;
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
    return sol_flow_packet_get_drange(packet, &mdata->value.d);
}

/* ---------------------------  static files ----------------------------------------- */

static int
static_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_static_options *opts =
        (struct sol_flow_node_type_http_server_static_options *)options;

    r = server_ref();
    SOL_INT_CHECK(r, < 0, r);

    mdata->path = strdup(opts->path);
    SOL_NULL_CHECK_GOTO(mdata->path, err);
    mdata->namespace = strdup(opts->namespace);
    SOL_NULL_CHECK_GOTO(mdata->namespace, err_namespace);

    mdata->value.b = opts->start;

    if (opts->start) {
        r = sol_http_server_add_dir(server, mdata->namespace, mdata->path);
        SOL_INT_CHECK_GOTO(r, < 0, err_add);
    }

    return 0;

err_add:
    free(mdata->namespace);
err_namespace:
    free(mdata->path);
err:
    server_unref();
    return -1;
}

static void
static_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    if (mdata->value.b)
        sol_http_server_remove_dir(server, mdata->namespace, mdata->path);

    server_unref();
    free(mdata->path);
    free(mdata->namespace);
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
        return sol_http_server_add_dir(server, mdata->namespace, mdata->path);
    else
        return sol_http_server_remove_dir(server, mdata->namespace, mdata->path);
}

#include "http-server-gen.c"
