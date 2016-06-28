/*
 * This file is part of the Soletta (TM) Project
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

#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sol-flow/http-client.h"
#include "sol-flow.h"
#include "sol-http.h"
#include "sol-http-client.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-platform.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-str-table.h"
#include "sol-flow-internal.h"

struct http_data {
    struct sol_ptr_vector pending_conns;
    struct sol_str_slice key;
    enum sol_http_method method;
    struct sol_http_params url_params;
    char *url;
    char *accept;
    char *last_modified;
    bool machine_id;
    bool strict;
};

struct http_request_data {
    struct http_data base;
    struct sol_blob *content;
    struct sol_http_params params;
    bool allow_redir;
    int32_t timeout;
    char *user;
    char *password;
};

struct http_response_get_data {
    char *key;
};

struct create_url_data {
    char *scheme;
    char *host;
    char *path;
    char *fragment;
    char *user;
    char *password;
    uint32_t port;
    struct sol_http_params params;
};

struct http_client_node_type {
    struct sol_flow_node_type base;
    int (*process_json)(struct sol_flow_node *node, const struct sol_str_slice slice);
    int (*process_data)(struct sol_flow_node *node, struct sol_buffer *buf);
    void (*close_node)(struct sol_flow_node *node, void *data);
    int (*setup_params)(struct http_data *mdata, struct sol_http_params *params);
    void (*http_response)(void *data,
        struct sol_http_client_connection *conn,
        struct sol_http_response *response);
};

#define DOUBLE_STRING_LEN (64)

static int
set_basic_url_info(struct http_data *mdata, const char *full_uri)
{
    struct sol_http_url url, base_url;
    struct sol_buffer new_url = SOL_BUFFER_INIT_EMPTY;
    int r;

    r = sol_http_split_uri(sol_str_slice_from_str(full_uri), &url);
    SOL_INT_CHECK(r, < 0, r);

    memset(&base_url, 0, sizeof(struct sol_http_url));

    base_url.scheme = url.scheme;
    base_url.host = url.host;
    base_url.path = url.path;
    base_url.port = url.port;

    r = sol_http_create_full_uri(&new_url, base_url, NULL);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->url);
    mdata->url = sol_buffer_steal(&new_url, NULL);

    sol_http_params_clear(&mdata->url_params);
    r = sol_http_decode_params(url.query,
        SOL_HTTP_PARAM_QUERY_PARAM, &mdata->url_params);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if ((url.user.len || url.password.len) &&
        (sol_http_params_add_copy(&mdata->url_params,
        ((struct sol_http_param_value) {
            .type = SOL_HTTP_PARAM_AUTH_BASIC,
            .value.auth.user = url.user,
            .value.auth.password = url.password
        })) < 0 )) {
        SOL_WRN("Could not add the user: %.*s and password: %.*s as"
            " parameters", SOL_STR_SLICE_PRINT(url.user),
            SOL_STR_SLICE_PRINT(url.password));
        r = -ENOMEM;
        goto err_exit;
    }

    if (url.fragment.len && (sol_http_params_add_copy(&mdata->url_params,
        ((struct sol_http_param_value) {
            .type = SOL_HTTP_PARAM_FRAGMENT,
            .value.key_value.key = url.fragment,
            .value.key_value.value = SOL_STR_SLICE_EMPTY
        })) < 0 )) {
        SOL_WRN("Could not add the fragment: %.*s paramenter",
            SOL_STR_SLICE_PRINT(url.fragment));
        r = -ENOMEM;
        goto err_exit;
    }

    return 0;

err_exit:
    sol_http_params_clear(&mdata->url_params);
    free(mdata->url);
    return r;
}

static int
set_basic_url_info_from_packet(struct http_data *mdata, const struct sol_flow_packet *packet)
{
    int r;
    const char *url;

    r = sol_flow_packet_get_string(packet, &url);
    SOL_INT_CHECK(r, < 0, r);
    return set_basic_url_info(mdata, url);
}

static int
replace_string_from_packet(const struct sol_flow_packet *packet, char **dst)
{
    const char *s;
    int r;

    r = sol_flow_packet_get_string(packet, &s);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_util_replace_str_if_changed(dst, s);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
machine_id_header_add(struct sol_http_params *params)
{
    int r;
    const char *id;

    id = sol_platform_get_machine_id();
    SOL_NULL_CHECK(id, -errno);

    r = sol_http_params_add(params,
        SOL_HTTP_REQUEST_PARAM_HEADER("X-Soletta-Machine-ID", id));
    SOL_INT_CHECK(r, < 0, -ENOMEM);

    return 0;
}

static void
common_close(struct sol_flow_node *node, void *data)
{
    struct sol_http_client_connection *connection;
    struct http_data *mdata = data;
    uint16_t i;
    const struct http_client_node_type *type;

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);

    if (type->close_node)
        type->close_node(node, data);

    free(mdata->url);
    free(mdata->accept);
    free(mdata->last_modified);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_conns, connection, i)
        sol_http_client_connection_cancel(connection);
    sol_ptr_vector_clear(&mdata->pending_conns);
    sol_http_params_clear(&mdata->url_params);
}

static int
common_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;
    int r;
    struct sol_flow_node_type_http_client_boolean_options *opts =
        (struct sol_flow_node_type_http_client_boolean_options *)options;

    mdata->machine_id = opts->machine_id;
    mdata->strict = opts->strict;

    sol_ptr_vector_init(&mdata->pending_conns);
    sol_http_params_init(&mdata->url_params);

    if (opts->url && opts->url[0] != '\0') {
        r = set_basic_url_info(mdata, opts->url);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (opts->accept) {
        mdata->accept = strdup(opts->accept);
        SOL_NULL_CHECK_GOTO(mdata->accept, err_accept);
    }

    return 0;

err_accept:
    free(mdata->url);
    return -ENOMEM;
}

static int
common_url_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;

    r = set_basic_url_info_from_packet(data, packet);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static void
remove_connection(struct http_data *mdata,
    struct sol_http_client_connection *connection)
{
    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);
}

/**
 * returns -errno on error, 0 if status HTTP status OK and content.used > 0
 * or 1 if HTTP_STATUS_NOT_MODIFIED
 */
static int
check_response(struct http_data *mdata, struct sol_flow_node *node,
    struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{

    remove_connection(mdata, connection);
    if (!response) {
        sol_flow_send_error_packet(node, EINVAL,
            "Error while reaching %s", mdata->url);
        return -EINVAL;
    }

    if (response->response_code == SOL_HTTP_STATUS_NOT_MODIFIED)
        return 1;

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(node, EINVAL,
            "%s returned an unhandled response code: %d",
            mdata->url, response->response_code);
        return -EINVAL;
    }

    return 0;
}

static int
get_last_modified_date(struct http_data *mdata,
    struct sol_http_response *response)
{
    struct sol_http_param_value *param_value;
    uint16_t i;

    SOL_HTTP_PARAMS_FOREACH_IDX (&response->param, param_value, i) {
        int r;

        if (param_value->type != SOL_HTTP_PARAM_HEADER)
            continue;
        if (!sol_str_slice_str_eq(param_value->value.key_value.key,
            "Last-Modified"))
            continue;
        r = sol_util_replace_str_from_slice_if_changed(&mdata->last_modified,
            param_value->value.key_value.value);
        SOL_INT_CHECK(r, < 0, r);
        break;
    }

    return 0;
}

static bool
is_accepted_content_type(const char *content_type, const char *accept)
{
    bool r = false;
    int err;
    uint16_t i;
    struct sol_vector priorities;
    struct sol_http_content_type_priority *pri;
    struct sol_str_slice type, sub_type;
    char *sep;

    sep = strchr(content_type, '/');
    SOL_NULL_CHECK(sep, false);

    type.data = content_type;
    type.len = sep - type.data;

    sub_type.data = sep + 1;
    sub_type.len = strlen(content_type) - type.len - 1;

    err = sol_http_parse_content_type_priorities(sol_str_slice_from_str(accept),
        &priorities);
    SOL_INT_CHECK(err, < 0, false);

    SOL_VECTOR_FOREACH_IDX (&priorities, pri, i) {
        if (sol_str_slice_str_eq(pri->content_type, content_type) ||
            sol_str_slice_str_eq(pri->content_type, "*/*") ||
            (sol_str_slice_eq(pri->type, type) &&
            sol_str_slice_str_eq(pri->sub_type, "*")) ||
            (sol_str_slice_str_eq(pri->type, "*") &&
            sol_str_slice_eq(pri->sub_type, sub_type))) {
            r = true;
            break;
        }
    }

    sol_http_content_type_priorities_array_clear(&priorities);
    return r;
}

static void
request_finished(void *data,
    struct sol_http_client_connection *connection,
    struct sol_http_response *response, bool accept_empty_response)
{
    int ret;
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_client_node_type *type;

    ret = check_response(mdata, node, connection, response);
    if (ret < 0) {
        SOL_WRN("Invalid HTTP response - Url: %s", mdata->url);
        return;
    }

    //Not modified
    if (ret == 1)
        return;

    if (!accept_empty_response && !response->content.used) {
        sol_flow_send_error_packet(node, ENOENT,
            "Received empty response from: %s", mdata->url);
        return;
    }

    ret = get_last_modified_date(mdata, response);
    SOL_INT_CHECK_GOTO(ret, < 0, err);

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);

    if (mdata->strict && mdata->accept && response->content_type &&
        !is_accepted_content_type(response->content_type, mdata->accept)) {
        sol_flow_send_error_packet(node, EINVAL,
            "Response has different content type. Received: %s - Desired: %s",
            response->content_type, mdata->accept);
        return;
    }

    if (response->content_type && type->process_json &&
        (streq(response->content_type, "application/json") ||
        streq(response->content_type, "text/stream"))) {
        ret = type->process_json(node, sol_buffer_get_slice(&response->content));
        if (ret < 0)
            goto err;
    } else {
        //Json and blob nodes will always fallback to process_data()
        ret = type->process_data(node, &response->content);
        if (ret < 0)
            goto err;
    }

    return;

err:
    sol_flow_send_error_packet(node, ret,
        "%s Could not parse url contents ", mdata->url);
}

static void
common_request_finished(void *data,
    struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    request_finished(data, connection, response, false);
}

static ssize_t
sse_received_data_cb(void *data, struct sol_http_client_connection *conn,
    const struct sol_buffer *buf)
{
    struct sol_flow_node *node = data;
    const struct http_client_node_type *type;
    const struct sol_str_slice prefix = SOL_STR_SLICE_LITERAL("data: ");
    const struct sol_str_slice suffix = SOL_STR_SLICE_LITERAL("\n\n");
    struct sol_str_slice slice;
    size_t consumed = 0;

    SOL_DBG("Received SSE Data - *%.*s*",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(buf)));

    slice = sol_buffer_get_slice(buf);
    if (!sol_str_slice_contains(slice, suffix))
        return 0;

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);

    while (slice.len) {
        char *start, *end, *content;
        struct sol_buffer content_buf;
        size_t content_len, total_len;
        int r;

        start = sol_str_slice_contains(slice, prefix);
        SOL_NULL_CHECK(start, -EINVAL);

        end = sol_str_slice_contains(slice, suffix);
        if (!end) //Wait for more data
            goto exit;

        content = start + prefix.len;
        content_len = end - content;

        sol_buffer_init_flags(&content_buf, content, content_len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        content_buf.used = content_len;
        total_len = content_len + prefix.len + suffix.len;
        consumed += total_len;

        SOL_DBG("Parsed SSE data:*%.*s*",
            SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&content_buf)));

        if (type->process_json)
            r = type->process_json(node, sol_buffer_get_slice(&content_buf));
        else
            r = type->process_data(node, &content_buf); //Used by the http-client/json node
        sol_buffer_fini(&content_buf);
        SOL_INT_CHECK(r, < 0, r);
        slice.len -= total_len;
        slice.data += total_len;
    }

exit:
    SOL_DBG("Buf len: %zu - Consumed: %zu", buf->used, consumed);
    return consumed;
}

static void
sse_response_end_cb(void *data,
    struct sol_http_client_connection *conn,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    const struct http_client_node_type *type;

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    SOL_DBG("SSE finished - url: %s", response->url);

    if (type->http_response)
        type->http_response(data, conn, response);
    else
        request_finished(data, conn, response, true);
}

static int
common_get_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_http_params params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;
    uint16_t i;
    const struct http_client_node_type *type;
    struct sol_http_param_value *param;
    static const struct sol_http_request_interface req_iface = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_REQUEST_INTERFACE_API_VERSION, )
        .on_data = sse_received_data_cb,
        .on_response = sse_response_end_cb,
    };


    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);

    if (!mdata->url) {
        sol_flow_send_error_packet_str(node, ENOENT, "Missing URL");
        return -ENOENT;
    }

    sol_http_params_init(&params);

    if (mdata->accept) {
        if (sol_http_params_add(&params,
            SOL_HTTP_REQUEST_PARAM_HEADER("Accept", mdata->accept)) < 0) {
            SOL_WRN("Failed to set the 'Accept' header with value: %s",
                mdata->accept);
            r = -ENOMEM;
            goto err;
        }
    }


    if (mdata->last_modified && (sol_http_params_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("If-Since-Modified",
        mdata->last_modified)) < 0 )) {
        SOL_WRN("Failed to set query params");
        r = -ENOMEM;
        goto err;
    }

    SOL_HTTP_PARAMS_FOREACH_IDX (&mdata->url_params, param, i) {
        if (sol_http_params_add(&params, *param) < 0) {
            SOL_WRN("Could not append the param - %.*s:%.*s",
                SOL_STR_SLICE_PRINT(param->value.key_value.key),
                SOL_STR_SLICE_PRINT(param->value.key_value.value));
            r = -EINVAL;
            goto err;
        }
    }


    if (type->setup_params) {
        r = type->setup_params(mdata, &params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    if (mdata->machine_id) {
        r = machine_id_header_add(&params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    connection = sol_http_client_request_with_interface(SOL_HTTP_METHOD_GET,
        mdata->url, &params, &req_iface, node);

    sol_http_params_clear(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;

err:
    sol_http_params_clear(&params);
    return r;
}

static int
common_post_process(struct sol_flow_node *node, struct http_data *mdata,
    struct sol_blob *blob, ...)
{
    int r;
    va_list ap;
    char *key, *value;
    struct sol_http_params params;
    struct sol_http_client_connection *connection;

    sol_http_params_init(&params);

    if (mdata->accept && (sol_http_params_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", mdata->accept)) < 0)) {
        SOL_WRN("Could not add the header '%s:%s' into request to %s",
            "Accept", mdata->accept, mdata->url);
        goto err;
    }

    if (mdata->machine_id) {
        r = machine_id_header_add(&params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    if (!blob) {
        va_start(ap, blob);
        while ((key = va_arg(ap, char *))) {
            value = va_arg(ap, char *);
            if ((sol_http_params_add(&params,
                SOL_HTTP_REQUEST_PARAM_POST_FIELD(key, value))) < 0) {
                SOL_WRN("Could not add header '%s:%s' into request to %s",
                    key, value, mdata->url);
                va_end(ap);
                goto err;
            }
        }
        va_end(ap);
    } else {
        struct sol_str_slice slice;

        slice.data = blob->mem;
        slice.len = blob->size;
        if (sol_http_params_add(&params,
            SOL_HTTP_REQUEST_PARAM_POST_DATA_CONTENTS("data", slice)) < 0) {
            SOL_WRN("Could not add the post data contents!");
            goto err;
        }
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, common_request_finished, node);
    sol_http_params_clear(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection for %s", mdata->url);
        sol_http_client_connection_cancel(connection);
        return r;
    }

    return 0;

err:
    sol_http_params_clear(&params);
    return -ENOMEM;
}

static int
boolean_process_json(struct sol_flow_node *node, const struct sol_str_slice slice)
{
    bool result;
    struct sol_json_token value;

    sol_json_token_init_from_slice(&value, slice);

    if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_TRUE)
        result = true;
    else if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_FALSE)
        result = false;
    else
        return -EINVAL;

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
}

static int
boolean_process_data(struct sol_flow_node *node, struct sol_buffer *buf)
{
    bool result;

    if (!strncasecmp("true", buf->data, buf->used))
        result = true;
    else if (!strncasecmp("false", buf->data, buf->used))
        result = false;
    else
        return -EINVAL;

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
}

static int
boolean_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    bool b;

    r = sol_flow_packet_get_bool(packet, &b);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, NULL, "value", b ? "true" : "false",
        NULL);
}

/*
 * --------------------------------- string node -----------------------------
 */

static int
string_process_json(struct sol_flow_node *node, const struct sol_str_slice slice)
{
    char *result = NULL;
    struct sol_json_token value;

    sol_json_token_init_from_slice(&value, slice);

    if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_STRING)
        result = sol_json_token_get_unescaped_string_copy(&value);
    else
        result = sol_str_slice_to_str(slice);
    SOL_NULL_CHECK(result, -ENOMEM);

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_STRING__OUT__OUT, result);
}

static int
string_process_data(struct sol_flow_node *node, struct sol_buffer *buf)
{
    char *result;

    result = strndup(buf->data, buf->used);

    if (result) {
        return sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_STRING__OUT__OUT, result);
    }

    return -ENOMEM;
}

static int
string_post(struct sol_flow_node *node, const struct sol_flow_packet *packet,
    struct http_data *mdata, bool serialize)
{
    int r;
    const char *value;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (!serialize)
        return common_post_process(node, mdata, NULL, "value", value, NULL);

    r = sol_json_serialize_string(&buf, value);
    SOL_INT_CHECK(r, < 0, r);
    r = common_post_process(node, mdata, NULL, "value", buf.data, NULL);
    sol_buffer_fini(&buf);
    return r;
}

static int
string_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return string_post(node, packet, data, false);
}

/*
 * --------------------------------- irange node -----------------------------
 */
static int
int_process_json(struct sol_flow_node *node, const struct sol_str_slice slice)
{
    struct sol_irange irange = SOL_IRANGE_INIT();
    enum sol_json_loop_status reason;
    struct sol_json_scanner sub_scanner;
    struct sol_json_token sub_key, sub_value, token;

    sol_json_scanner_init_from_slice(&sub_scanner, slice);
    SOL_JSON_SCANNER_OBJECT_LOOP (&sub_scanner, &token, &sub_key, &sub_value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "value")) {
            if (sol_json_token_get_int32(&sub_value, &irange.val) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "min")) {
            if (sol_json_token_get_int32(&sub_value, &irange.min) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "max")) {
            if (sol_json_token_get_int32(&sub_value, &irange.max) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "step")) {
            if (sol_json_token_get_int32(&sub_value, &irange.step) < 0)
                return -EINVAL;
        }
    }

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_INT__OUT__OUT, &irange);
}

static int
int_process_data(struct sol_flow_node *node, struct sol_buffer *buf)
{
    int r;
    long int value;

    r = sol_str_slice_to_int(sol_buffer_get_slice(buf), &value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_INT__OUT__OUT, value);
}

static int
int_post(struct sol_flow_node *node, const struct sol_flow_packet *packet,
    struct http_data *mdata, bool all_fields)
{
    int r;
    struct sol_irange value;
    char min[3 * sizeof(int32_t)], max[3 * sizeof(int32_t)],
        val[3 * sizeof(int32_t)], step[3 * sizeof(int32_t)];

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = snprintf(val, sizeof(val), "%" PRId32, value.val);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(r, >= (int)sizeof(val), -ENOMEM);

    if (!all_fields)
        return common_post_process(node, mdata, NULL, "value", val, NULL);

    r = snprintf(min, sizeof(min), "%" PRId32, value.min);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(r, >= (int)sizeof(min), -ENOMEM);
    r = snprintf(max, sizeof(max), "%" PRId32, value.max);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(r, >= (int)sizeof(max), -ENOMEM);
    r = snprintf(step, sizeof(step), "%" PRId32, value.step);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(r, >= (int)sizeof(step), -ENOMEM);

    return common_post_process(node, mdata, NULL, "value", val, "min", min,
        "max", max, "step", step, NULL);
}

static int
int_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return int_post(node, packet, data, true);
}

/*
 * --------------------------------- drange node -----------------------------
 */
static int
float_process_json(struct sol_flow_node *node, const struct sol_str_slice slice)
{
    struct sol_drange drange = SOL_DRANGE_INIT();
    enum sol_json_loop_status reason;
    struct sol_json_scanner sub_scanner;
    struct sol_json_token token, sub_key, sub_value;

    sol_json_scanner_init_from_slice(&sub_scanner, slice);
    SOL_JSON_SCANNER_OBJECT_LOOP (&sub_scanner, &token, &sub_key, &sub_value, reason) {
        int r;
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "value")) {
            r = sol_json_token_get_double(&sub_value, &drange.val);
            SOL_INT_CHECK(r, != 0, r);
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "min")) {
            r = sol_json_token_get_double(&sub_value, &drange.min);
            SOL_INT_CHECK(r, != 0, r);
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "max")) {
            r = sol_json_token_get_double(&sub_value, &drange.max);
            SOL_INT_CHECK(r, != 0, r);
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "step")) {
            r = sol_json_token_get_double(&sub_value, &drange.step);
            SOL_INT_CHECK(r, != 0, r);
        }
    }

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_FLOAT__OUT__OUT, &drange);
}

static int
parse_float(const struct sol_str_slice slice, double *value)
{
    errno = 0;
    *value = sol_util_strtod_n(slice.data, NULL, slice.len, false);
    if (errno)
        return -errno;
    return 0;
}

static int
float_process_data(struct sol_flow_node *node, struct sol_buffer *buf)
{
    double value;
    int r;

    r = parse_float(sol_buffer_get_slice(buf), &value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_FLOAT__OUT__OUT, value);
}

static int
float_post(struct sol_flow_node *node, const struct sol_flow_packet *packet,
    struct http_data *mdata, bool all_fields)
{
    int r;
    struct sol_drange value;

    SOL_BUFFER_DECLARE_STATIC(val, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(min, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(max, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(step, DOUBLE_STRING_LEN);

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.val, &val);
    SOL_INT_CHECK(r, < 0, r);

    if (!all_fields)
        return common_post_process(node, mdata, NULL, "value", val.data, NULL);

    r = sol_json_double_to_str(value.min, &min);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.max, &max);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.step, &step);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, mdata, NULL, "value", val.data, "min",
        min.data, "max", max.data, "step", step.data, NULL);
}

static int
float_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return float_post(node, packet, data, true);
}

/*
 * --------------------------------- rgb node  -----------------------------
 */

static int
rgb_process_json(struct sol_flow_node *node, const struct sol_str_slice slice)
{
    struct sol_rgb rgb = { 0 };
    enum sol_json_loop_status reason;
    struct sol_json_scanner sub_scanner;
    struct sol_json_token sub_key, sub_value, token;

    sol_json_scanner_init_from_slice(&sub_scanner, slice);
    rgb.red_max = rgb.green_max = rgb.blue_max = 255;

    SOL_JSON_SCANNER_OBJECT_LOOP (&sub_scanner, &token, &sub_key,
        &sub_value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "red")) {
            if (sol_json_token_get_uint32(&sub_value, &rgb.red) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "green")) {
            if (sol_json_token_get_uint32(&sub_value, &rgb.green) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "blue")) {
            if (sol_json_token_get_uint32(&sub_value, &rgb.blue) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "red_max")) {
            if (sol_json_token_get_uint32(&sub_value, &rgb.red_max) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "green_max")) {
            if (sol_json_token_get_uint32(&sub_value, &rgb.green_max) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "blue_max")) {
            if (sol_json_token_get_uint32(&sub_value, &rgb.blue_max) < 0)
                return -EINVAL;
        }
    }

    if (rgb.red > rgb.red_max) {
        SOL_WRN("Red value '%" PRIu32 "' is bigger than red max '%" PRIu32,
            rgb.red, rgb.red_max);
        return -EINVAL;
    }
    if (rgb.blue > rgb.blue_max) {
        SOL_WRN("Blue value '%" PRIu32 "' is bigger than blue max '%" PRIu32,
            rgb.blue, rgb.blue_max);
        return -EINVAL;
    }
    if (rgb.green > rgb.green_max) {
        SOL_WRN("Green value '%" PRIu32 "' is bigger than green max '%" PRIu32,
            rgb.green, rgb.green_max);
        return -EINVAL;
    }

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_RGB__OUT__OUT, &rgb);
}

static int
hex_str_to_decimal(const char *start, ssize_t len, uint32_t *value)
{
    char *endptr = NULL;

    errno = 0;
    *value = sol_util_strtoul_n(start, &endptr, len, 16);

    if (errno != 0 || endptr == start) {
        SOL_WRN("Could not convert the string '%.*s' to decimal",
            (int)len, start);
        return -EINVAL;
    }

    return 0;
}

static int
rgb_process_data(struct sol_flow_node *node, struct sol_buffer *buf)
{
    struct sol_rgb rgb = { 0 };
    struct sol_str_slice rgb_str;
    int r;

    rgb_str = sol_buffer_get_slice(buf);

    if (rgb_str.len != 7 || rgb_str.data[0] != '#') {
        SOL_WRN("Expected format #RRGGBB. Received: %.*s",
            SOL_STR_SLICE_PRINT(rgb_str));
        return -EINVAL;
    }

    //Skip '#'
    rgb_str.data++;
    rgb_str.len--;

    r = hex_str_to_decimal(rgb_str.data, 2, &rgb.red);
    SOL_INT_CHECK(r, < 0, r);

    r = hex_str_to_decimal(rgb_str.data + 2, 2, &rgb.green);
    SOL_INT_CHECK(r, < 0, r);

    r = hex_str_to_decimal(rgb_str.data + 4, 2, &rgb.blue);
    SOL_INT_CHECK(r, < 0, r);
    rgb.green_max = rgb.red_max = rgb.blue_max = 255;

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_RGB__OUT__OUT, &rgb);
}

static int
rgb_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_rgb rgb;

#define INT32_STR_LEN (12)

    SOL_BUFFER_DECLARE_STATIC(red, INT32_STR_LEN);
    SOL_BUFFER_DECLARE_STATIC(green, INT32_STR_LEN);
    SOL_BUFFER_DECLARE_STATIC(blue, INT32_STR_LEN);
    SOL_BUFFER_DECLARE_STATIC(red_max, INT32_STR_LEN);
    SOL_BUFFER_DECLARE_STATIC(green_max, INT32_STR_LEN);
    SOL_BUFFER_DECLARE_STATIC(blue_max, INT32_STR_LEN);

    r = sol_flow_packet_get_rgb(packet, &rgb);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_printf(&red, "%" PRIu32, rgb.red);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_printf(&green, "%" PRIu32, rgb.green);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_printf(&blue, "%" PRIu32, rgb.blue);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_printf(&red_max, "%" PRIu32, rgb.red_max);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_printf(&green_max, "%" PRIu32, rgb.green_max);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_printf(&blue_max, "%" PRIu32, rgb.blue_max);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, NULL, "red", red.data,
        "green", green.data, "blue", blue.data, "red_max", red_max.data,
        "green_max", green_max.data, "blue_max", blue_max.data, NULL);

#undef INT32_STR_LEN
}

/*
 * --------------------------------- direction vector node  -----------------------------
 */
static int
direction_vector_process_json(struct sol_flow_node *node, const struct sol_str_slice slice)
{
    struct sol_direction_vector dir_vector = { 0 };
    enum sol_json_loop_status reason;
    struct sol_json_scanner sub_scanner;
    struct sol_json_token sub_key, sub_value, token;

    sol_json_scanner_init_from_slice(&sub_scanner, slice);

    dir_vector.max = DBL_MAX;
    dir_vector.min = -DBL_MAX;

    SOL_JSON_SCANNER_OBJECT_LOOP (&sub_scanner, &token, &sub_key,
        &sub_value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "x")) {
            if (sol_json_token_get_double(&sub_value, &dir_vector.x) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "y")) {
            if (sol_json_token_get_double(&sub_value, &dir_vector.y) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "z")) {
            if (sol_json_token_get_double(&sub_value, &dir_vector.z) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "min")) {
            if (sol_json_token_get_double(&sub_value, &dir_vector.min) < 0)
                return -EINVAL;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&sub_key, "max")) {
            if (sol_json_token_get_double(&sub_value, &dir_vector.max) < 0)
                return -EINVAL;
        }
    }

    if (dir_vector.x > dir_vector.max || dir_vector.x < dir_vector.min) {
        SOL_WRN("Direction vector X compontent '%g' outside the range:[%g, %g]",
            dir_vector.x, dir_vector.min, dir_vector.max);
        return -EINVAL;
    }

    if (dir_vector.y > dir_vector.max || dir_vector.y < dir_vector.min) {
        SOL_WRN("Direction vector Y compontent '%g' outside the range:[%g, %g]",
            dir_vector.y, dir_vector.min, dir_vector.max);
        return -EINVAL;
    }

    if (dir_vector.z > dir_vector.max || dir_vector.z < dir_vector.min) {
        SOL_WRN("Direction vector Z compontent '%g' outside the range:[%g, %g]",
            dir_vector.z, dir_vector.min, dir_vector.max);
        return -EINVAL;
    }

    return sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_DIRECTION_VECTOR__OUT__OUT, &dir_vector);
}

static int
direction_vector_process_data(struct sol_flow_node *node,
    struct sol_buffer *buf)
{
    struct sol_direction_vector dir_vector;
    struct sol_str_slice token;
    double components[3];
    size_t i = 0;

    token = sol_buffer_get_slice(buf);

    if (!token.len || token.data[0] != '(' ||
        token.data[token.len - 1] != ')') {
        SOL_WRN("Invalid direction vector format. Received '%.*s'",
            SOL_STR_SLICE_PRINT(token));
        return -EINVAL;
    }

    token.data++;
    token.len -= 2;

    while (token.len) {
        char *sep = memchr(token.data, ';', token.len);
        size_t len;
        char *endptr;

        if (!sep)
            len = token.len;
        else
            len = sep - token.data;

        errno = 0;
        endptr = NULL;
        components[i] = sol_util_strtod_n(token.data, &endptr, len, false);

        if (errno != 0 || endptr == token.data) {
            SOL_WRN("Could not parse the component to double. '%.*s'",
                (int)len, token.data);
            return -EINVAL;
        }

        //Skip ,
        token.data += len + 1;
        token.len -= len == token.len ? len : len + 1;
        i++;
    }

    if (i != 3) {
        SOL_WRN("Could not parse all the direction vector components.");
        return -EINVAL;
    }

    dir_vector.x = components[0];
    dir_vector.y = components[1];
    dir_vector.z = components[2];
    dir_vector.max = DBL_MAX;
    dir_vector.min = -DBL_MAX;

    return sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_DIRECTION_VECTOR__OUT__OUT, &dir_vector);
}

static int
direction_vector_post_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_direction_vector dir;

    SOL_BUFFER_DECLARE_STATIC(x, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(z, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(y, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(min, DOUBLE_STRING_LEN);
    SOL_BUFFER_DECLARE_STATIC(max, DOUBLE_STRING_LEN);

    r = sol_flow_packet_get_direction_vector(packet, &dir);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(dir.x, &x);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(dir.y, &y);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(dir.z, &z);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(dir.min, &min);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(dir.max, &max);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, NULL, "x", x.data,
        "y", y.data, "z", z.data, "min", min.data,
        "max", max.data, NULL);
}

static int
generic_url_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_basic_url_info_from_packet(data, packet);
}

static struct sol_blob *
blob_from_buffer(struct sol_flow_node *node, struct sol_buffer *buf)
{
    struct sol_blob *blob;
    size_t size;
    void *data;

    data = sol_buffer_steal_or_copy(buf, &size);
    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, data, size);
    if (!blob) {
        sol_flow_send_error_packet(node, ENOMEM,
            "Could not alloc memory for the response");
        free(data);
        return NULL;
    }

    return blob;
}

static int
get_blob_process(struct sol_flow_node *node, struct sol_buffer *buf)
{
    struct sol_blob *blob;
    int r;

    blob = blob_from_buffer(node, buf);
    SOL_NULL_CHECK(blob, -ENOMEM);

    r = sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BLOB__OUT__OUT, blob);
    sol_blob_unref(blob);

    return r;
}

static int
blob_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *blob;

    r = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);
    return common_post_process(node, data, blob);
}

static int
json_post_array_or_object(struct sol_flow_node *node, void *data,
    const struct sol_flow_packet *packet, bool is_object)
{
    int r;
    struct sol_blob *blob;

    if (is_object)
        r = sol_flow_packet_get_json_object(packet, &blob);
    else
        r = sol_flow_packet_get_json_array(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);
    return common_post_process(node, data, blob);
}

static int
json_object_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return json_post_array_or_object(node, data, packet, true);
}

static int
json_array_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return json_post_array_or_object(node, data, packet, false);
}

static int
json_string_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return string_post(node, packet, data, true);
}

static int
json_float_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return float_post(node, packet, data, false);
}

static int
json_int_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return int_post(node, packet, data, false);
}

static int
json_null_post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return common_post_process(node, data, NULL, "value", "null", NULL);
}

static int
get_json_process(struct sol_flow_node *node, struct sol_buffer *buf)
{
    struct sol_json_token value;
    enum sol_json_type type;
    int r;

    sol_json_token_init_from_slice(&value, sol_buffer_get_slice(buf));

    type = sol_json_token_get_type(&value);

    if (type == SOL_JSON_TYPE_OBJECT_START) {
        struct sol_blob *blob;

        blob = blob_from_buffer(node, buf);
        SOL_NULL_CHECK(blob, -ENOMEM);
        r = sol_flow_send_json_object_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__OBJECT, blob);
        sol_blob_unref(blob);
    } else if (type == SOL_JSON_TYPE_ARRAY_START) {
        struct sol_blob *blob;

        blob = blob_from_buffer(node, buf);
        SOL_NULL_CHECK(blob, -ENOMEM);
        r = sol_flow_send_json_array_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__ARRAY, blob);
        sol_blob_unref(blob);
    } else if (type == SOL_JSON_TYPE_TRUE) {
        r = sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__BOOLEAN, true);
    } else if (type == SOL_JSON_TYPE_FALSE) {
        r = sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__BOOLEAN, false);
    } else if (type == SOL_JSON_TYPE_STRING) {
        char *str;

        str = sol_json_token_get_unescaped_string_copy(&value);
        SOL_NULL_CHECK(str, -ENOMEM);

        r = sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__STRING, str);
    } else if (type == SOL_JSON_TYPE_NUMBER) {
        double dvalue;

        r = parse_float(sol_buffer_get_slice(buf), &dvalue);
        SOL_INT_CHECK(r, < 0, r);
        r = sol_flow_send_drange_value_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__FLOAT, dvalue);
        SOL_INT_CHECK(r, < 0, r);

        if (dvalue >= INT32_MIN && dvalue <= INT32_MAX)
            r = sol_flow_send_irange_value_packet(node,
                SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__INT, (int32_t)dvalue);
    } else if (type == SOL_JSON_TYPE_NULL) {
        r = sol_flow_send_empty_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_JSON__OUT__NULL);
    } else {
        sol_flow_send_error_packet(node, EINVAL, "Unknown json type");
        r = -EINVAL;
    }

    return r;
}

/*
 * --------------------------------- generic nodes  -----------------------------
 */
static int
request_node_setup_params(struct http_data *data,
    struct sol_http_params *params)
{
    struct http_request_data *mdata = (struct http_request_data *)data;
    struct sol_http_param_value *param;
    uint16_t i;
    static const char *key = "blob";

    SOL_HTTP_PARAMS_FOREACH_IDX (&mdata->params, param, i) {
        if (sol_http_params_add(params, *param) < 0) {
            SOL_ERR("Could not append the param - %.*s:%.*s",
                SOL_STR_SLICE_PRINT(param->value.key_value.key),
                SOL_STR_SLICE_PRINT(param->value.key_value.value));
            return -ENOMEM;
        }
    }

    if ((mdata->user || mdata->password) && (sol_http_params_add(params,
        SOL_HTTP_REQUEST_PARAM_AUTH_BASIC(mdata->user, mdata->password)) < 0)) {
        SOL_ERR("Could not set user and password params");
        return -ENOMEM;
    }

    if (sol_http_params_add(params,
        SOL_HTTP_REQUEST_PARAM_ALLOW_REDIR(mdata->allow_redir)) < 0) {
        SOL_ERR("Could not set allow redirection param");
        return -ENOMEM;
    }

    if (sol_http_params_add(params,
        SOL_HTTP_REQUEST_PARAM_TIMEOUT(mdata->timeout)) < 0) {
        SOL_ERR("Could not set the timeout param");
        return -ENOMEM;
    }

    if (mdata->content && (sol_http_params_add(params,
        SOL_HTTP_REQUEST_PARAM_POST_DATA_CONTENTS(key,
        sol_str_slice_from_blob(mdata->content))) < 0)) {
        SOL_ERR("Could not set the post parameter");
        return -ENOMEM;
    }

    return 0;
}

static int
setup_response_headers_and_cookies(struct sol_http_params *params,
    struct sol_vector *cookies, struct sol_vector *headers)
{
    uint16_t i;
    struct sol_vector *to_append;
    struct sol_key_value *resp_param;
    struct sol_http_param_value *param;

    SOL_HTTP_PARAMS_FOREACH_IDX (params, param, i) {
        if (param->type == SOL_HTTP_PARAM_HEADER)
            to_append = headers;
        else if (param->type == SOL_HTTP_PARAM_COOKIE)
            to_append = cookies;
        else
            continue;

        resp_param = sol_vector_append(to_append);
        SOL_NULL_CHECK_GOTO(resp_param, err_exit);
        resp_param->key = sol_str_slice_to_str(param->value.key_value.key);
        resp_param->value =
            sol_str_slice_to_str(param->value.key_value.value);
    }

    return 0;

err_exit:
    return -ENOMEM;
}

static void
clear_sol_key_value_vector(struct sol_vector *vector)
{
    uint16_t i;
    struct sol_key_value *param;

    SOL_VECTOR_FOREACH_IDX (vector, param, i) {
        free((void *)param->key);
        free((void *)param->value);
    }

    sol_vector_clear(vector);
}

static void
request_node_http_response(void *data,
    struct sol_http_client_connection *conn,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_vector headers, cookies;
    struct sol_blob *blob;
    int r;

    remove_connection(mdata, conn);

    if (!response) {
        SOL_ERR("Empty response from:%s", mdata->url);
        sol_flow_send_error_packet(node, EINVAL, "Empty response from:%s",
            mdata->url);
        return;
    }

    blob = blob_from_buffer(node, &response->content);
    SOL_NULL_CHECK_GOTO(blob, err_blob);

    sol_vector_init(&cookies, sizeof(struct sol_key_value));
    sol_vector_init(&headers, sizeof(struct sol_key_value));

    r = setup_response_headers_and_cookies(&response->param,
        &cookies, &headers);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    r = sol_flow_send_http_response_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_REQUEST__OUT__OUT,
        response->response_code, response->url, response->content_type,
        blob, &cookies, &headers);

    if (r < 0) {
        SOL_ERR("Could not send the HTTP response packet from URL:%s",
            response->url);
    }

err_exit:
    clear_sol_key_value_vector(&cookies);
    clear_sol_key_value_vector(&headers);
    sol_blob_unref(blob);
err_blob:
    return;
}

static enum sol_http_method
translate_http_method(const char *method)
{
    static const struct sol_str_table http_methods[] = {
        SOL_STR_TABLE_ITEM("GET", SOL_HTTP_METHOD_GET),
        SOL_STR_TABLE_ITEM("HEAD", SOL_HTTP_METHOD_HEAD),
        SOL_STR_TABLE_ITEM("POST", SOL_HTTP_METHOD_POST),
        SOL_STR_TABLE_ITEM("PUT", SOL_HTTP_METHOD_PUT),
        SOL_STR_TABLE_ITEM("DELETE", SOL_HTTP_METHOD_DELETE),
        SOL_STR_TABLE_ITEM("CONNECT", SOL_HTTP_METHOD_CONNECT),
        SOL_STR_TABLE_ITEM("OPTIONS", SOL_HTTP_METHOD_OPTIONS),
        SOL_STR_TABLE_ITEM("TRACE", SOL_HTTP_METHOD_TRACE),
        SOL_STR_TABLE_ITEM("PATCH", SOL_HTTP_METHOD_PATCH),
        { }
    };

    return sol_str_table_lookup_fallback(http_methods,
        sol_str_slice_from_str(method), SOL_HTTP_METHOD_INVALID);
}

static int
request_node_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct http_request_data *mdata = data;
    struct sol_flow_node_type_http_client_request_options *opts =
        (struct sol_flow_node_type_http_client_request_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_HTTP_CLIENT_REQUEST_OPTIONS_API_VERSION,
        -EINVAL);
    SOL_INT_CHECK(opts->timeout, < 0, -EINVAL);
    mdata->timeout = opts->timeout;

    sol_http_params_init(&mdata->base.url_params);
    sol_http_params_init(&mdata->params);

    if (opts->url) {
        r = set_basic_url_info(&mdata->base, opts->url);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (opts->method &&
        ((mdata->base.method = translate_http_method(opts->method)) ==
        SOL_HTTP_METHOD_INVALID)) {
        r = -EINVAL;
        goto err_method;
    } else if (!opts->method)
        mdata->base.method = SOL_HTTP_METHOD_INVALID;

    sol_ptr_vector_init(&mdata->base.pending_conns);
    mdata->allow_redir = opts->allow_redir;
    mdata->base.machine_id = opts->machine_id;
    return 0;

err_method:
    sol_http_params_clear(&mdata->base.url_params);
    free(mdata->base.url);
    return r;
}

static void
request_node_clear_params(struct http_request_data *mdata)
{
    if (mdata->content) {
        sol_blob_unref(mdata->content);
        mdata->content = NULL;
    }

    sol_http_params_clear(&mdata->params);
}

static void
request_node_close(struct sol_flow_node *node, void *data)
{
    struct http_request_data *mdata = data;

    free(mdata->user);
    free(mdata->password);
    request_node_clear_params(mdata);
}

static int
request_node_method_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;
    const char *method;
    int r;

    r = sol_flow_packet_get_string(packet, &method);
    SOL_INT_CHECK(r, < 0, r);
    mdata->base.method = translate_http_method(method);
    if (mdata->base.method == SOL_HTTP_METHOD_INVALID)
        return -EINVAL;
    return 0;
}

static int
request_node_timeout_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;
    struct sol_irange irange;
    int r;

    r = sol_flow_packet_get_irange(packet, &irange);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(irange.val, < 0, -EINVAL);
    mdata->timeout = irange.val;
    return 0;
}

static int
param_process(const struct sol_flow_packet *packet, struct sol_http_params *params,
    enum sol_http_param_type type)
{
    const char *key, *value;
    uint16_t len;
    struct sol_flow_packet **children;
    struct sol_http_param_value param;
    int r;

    r = sol_flow_packet_get_composed_members(packet, &children, &len);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(len, != 2, -EINVAL);

    r = sol_flow_packet_get_string(children[0], &key);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_packet_get_string(children[1], &value);
    SOL_INT_CHECK(r, < 0, r);

    param.type = type;
    param.value.key_value.key = sol_str_slice_from_str(key);
    param.value.key_value.value = sol_str_slice_from_str(value);
    if (sol_http_params_add_copy(params, param) < 0) {
        SOL_ERR("Could not add the param %s : %s", key, value);
        return -ENOMEM;
    }

    return 0;
}

static int
request_node_param_query_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return param_process(packet, &mdata->params, SOL_HTTP_PARAM_QUERY_PARAM);
}

static int
request_node_param_cookie_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return param_process(packet, &mdata->params, SOL_HTTP_PARAM_COOKIE);
}

static int
request_node_param_post_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return param_process(packet, &mdata->params, SOL_HTTP_PARAM_POST_FIELD);
}

static int
request_node_param_header_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return param_process(packet, &mdata->params, SOL_HTTP_PARAM_HEADER);
}

static int
request_node_user_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->user);
}

static int
request_node_password_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->password);
}

static int
request_node_accept_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->base.accept);
}

static int
request_node_content_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->content)
        sol_blob_unref(mdata->content);
    mdata->content = sol_blob_ref(blob);
    SOL_NULL_CHECK(mdata->content, -ENOMEM);
    return 0;
}

static uint16_t
find_user_and_pass_pos_from_url_params(struct http_request_data *mdata)
{
    struct sol_http_param_value *param;
    uint16_t i;

    SOL_HTTP_PARAMS_FOREACH_IDX (&mdata->base.url_params, param, i) {
        if (param->type == SOL_HTTP_PARAM_AUTH_BASIC)
            return i;
    }
    return UINT16_MAX;
}

static int
request_node_trigger_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;
    uint16_t pos;
    int r;

    pos = find_user_and_pass_pos_from_url_params(mdata);

    if (pos != UINT16_MAX && (mdata->user || mdata->password)) {
        r = sol_vector_del(&mdata->base.url_params.params, pos);
        SOL_INT_CHECK(r, < 0, r);
    }

    return common_get_process(node, data, port, conn_id, packet);
}

static int
request_node_clear_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    if (mdata->content) {
        sol_blob_unref(mdata->content);
        mdata->content = NULL;
    }
    request_node_clear_params(mdata);
    return 0;
}

static int
get_response_code(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange out = SOL_IRANGE_INIT();
    int r;

    r = sol_flow_packet_get_http_response(packet, &out.val,
        NULL, NULL, NULL, NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);
    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_RESPONSE_CODE__OUT__OUT, &out);
}

static int
send_string_packet(struct sol_flow_node *node, uint16_t port,
    const char *to_send)
{
    return sol_flow_send_string_packet(node, port, to_send ? to_send : "null");
}

static int
get_url(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *url;

    r = sol_flow_packet_get_http_response(packet, NULL,
        &url, NULL, NULL, NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);
    return send_string_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_URL__OUT__OUT, url);
}

static int
get_content_type(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *content_type;

    r = sol_flow_packet_get_http_response(packet, NULL,
        NULL, &content_type, NULL, NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);
    return send_string_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_CONTENT_TYPE__OUT__OUT,
        content_type);
}

static int
get_blob(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const struct sol_blob *blob;

    r = sol_flow_packet_get_http_response(packet, NULL,
        NULL, NULL, &blob, NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);
    return sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_CONTENT__OUT__OUT, blob);
}

static void
common_get_close(struct sol_flow_node *node, void *data)
{
    struct http_response_get_data *mdata = data;

    free(mdata->key);
}

static int
common_get_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct http_response_get_data *mdata = data;
    struct sol_flow_node_type_http_client_get_headers_options *opts =
        (struct sol_flow_node_type_http_client_get_headers_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_HEADERS_OPTIONS_API_VERSION,
        -EINVAL);
    if (opts->key) {
        mdata->key = strdup(opts->key);
        SOL_NULL_CHECK(mdata->key, -ENOMEM);
    }
    return 0;
}

static int
send_filtered_key_value(struct sol_flow_node *node, uint16_t port,
    const char *key, struct sol_vector *vector)
{
    uint16_t i;
    struct sol_key_value *param;

    SOL_VECTOR_FOREACH_IDX (vector, param, i) {
        if (!strcasecmp(param->key, key))
            return send_string_packet(node, port, param->value);
    }

    return 0;
}

static int
get_headers_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_response_get_data *mdata = data;
    struct sol_vector headers;
    int r;

    if (!mdata->key)
        return 0;

    r = sol_flow_packet_get_http_response(packet, NULL,
        NULL, NULL, NULL, NULL, &headers);
    SOL_INT_CHECK(r, < 0, r);

    return send_filtered_key_value(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_HEADERS__OUT__OUT,
        mdata->key, &headers);
}

static int
get_cookies_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_response_get_data *mdata = data;
    struct sol_vector cookies;
    int r;

    if (!mdata->key)
        return 0;

    r = sol_flow_packet_get_http_response(packet, NULL,
        NULL, NULL, NULL, &cookies, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return send_filtered_key_value(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_COOKIES__OUT__OUT,
        mdata->key, &cookies);
}

static int
get_key_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct http_response_get_data *mdata = data;

    r = replace_string_from_packet(packet, &mdata->key);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static void
create_url_close(struct sol_flow_node *node, void *data)
{
    struct create_url_data *mdata = data;

    free(mdata->scheme);
    free(mdata->host);
    free(mdata->path);
    free(mdata->fragment);
    free(mdata->user);
    free(mdata->password);
    sol_http_params_clear(&mdata->params);
}

static int
add_query(struct sol_http_params *params,
    const struct sol_str_slice key, const struct sol_str_slice value)
{
    struct sol_http_param_value param;

    param.type = SOL_HTTP_PARAM_QUERY_PARAM;
    param.value.key_value.key = key;
    param.value.key_value.value = value;

    if (sol_http_params_add_copy(params, param) < 0) {
        SOL_ERR("Could not add the HTTP param %.*s:%.*s",
            SOL_STR_SLICE_PRINT(key), SOL_STR_SLICE_PRINT(value));
        return -ENOMEM;
    }

    return 0;
}

static int
replace_uri(struct create_url_data *mdata, const char *uri)
{
    struct sol_http_url url;
    int r;

    r = sol_http_split_uri(sol_str_slice_from_str(uri), &url);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_from_slice_if_changed(&mdata->scheme,
        url.scheme);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_from_slice_if_changed(&mdata->host, url.host);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_from_slice_if_changed(&mdata->fragment,
        url.fragment);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_from_slice_if_changed(&mdata->path, url.path);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_from_slice_if_changed(&mdata->user, url.user);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_from_slice_if_changed(&mdata->password,
        url.password);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_http_decode_params(url.query,
        SOL_HTTP_PARAM_QUERY_PARAM, &mdata->params);
    SOL_INT_CHECK(r, < 0, r);

    mdata->port = url.port;
    return 0;
}

static int
create_url_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct create_url_data *mdata = data;
    struct sol_flow_node_type_http_client_create_url_options *opts =
        (struct sol_flow_node_type_http_client_create_url_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_HTTP_CLIENT_CREATE_URL_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->params = SOL_HTTP_REQUEST_PARAMS_INIT;
    SOL_INT_CHECK(opts->port, < 0, -EINVAL);
    mdata->port = opts->port;
    r = -ENOMEM;

    if (opts->scheme) {
        mdata->scheme = strdup(opts->scheme);
        SOL_NULL_CHECK(mdata->scheme, -ENOMEM);
    }

    if (opts->host) {
        mdata->host = strdup(opts->host);
        SOL_NULL_CHECK_GOTO(mdata->host, err_host);
    }

    if (opts->path) {
        mdata->path = strdup(opts->path);
        SOL_NULL_CHECK_GOTO(mdata->path, err_path);
    }

    if (opts->fragment) {
        mdata->fragment = strdup(opts->fragment);
        SOL_NULL_CHECK_GOTO(mdata->fragment, err_fragment);
    }

    if (opts->query) {
        r = sol_http_split_query(opts->query, &mdata->params);
        SOL_INT_CHECK_GOTO(r, < 0, err_query);
    }

    if (opts->base_uri) {
        r = replace_uri(mdata, opts->base_uri);
        SOL_INT_CHECK_GOTO(r, < 0, err_query);
    }

    return 0;

err_query:
    sol_http_params_clear(&mdata->params);
    free(mdata->fragment);
err_fragment:
    free(mdata->path);
err_path:
    free(mdata->host);
err_host:
    free(mdata->scheme);
    return r;
}

static int
create_url_scheme_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->scheme);
}

static int
create_url_port_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;
    struct sol_irange irange;
    int r;

    r = sol_flow_packet_get_irange(packet, &irange);
    SOL_INT_CHECK(r, < 0, r);

    SOL_INT_CHECK(irange.val, < 0, -EINVAL);
    mdata->port = irange.val;
    return 0;
}

static int
create_url_host_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->host);
}

static int
create_url_path_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->path);
}

static int
create_url_fragment_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->fragment);
}


static int
create_url_user_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->user);
}

static int
create_url_password_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->password);
}

static int
create_url_base_uri_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;
    int r;
    const char *uri;

    r = sol_flow_packet_get_string(packet, &uri);
    SOL_INT_CHECK(r, < 0, r);
    return replace_uri(mdata, uri);
}

static int
create_url_query_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;
    const char *key, *value;
    uint16_t len;
    struct sol_flow_packet **children;
    int r;

    r = sol_flow_packet_get_composed_members(packet, &children, &len);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(len, != 2, -EINVAL);

    r = sol_flow_packet_get_string(children[0], &key);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_packet_get_string(children[1], &value);
    SOL_INT_CHECK(r, < 0, r);

    r = add_query(&mdata->params, sol_str_slice_from_str(key),
        sol_str_slice_from_str(value));
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
create_url_clear_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;

    sol_http_params_clear(&mdata->params);
    return 0;
}

static int
create_url_create_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct create_url_data *mdata = data;
    int r;
    struct sol_buffer uri = SOL_BUFFER_INIT_EMPTY;
    struct sol_http_url url;

    url.scheme = sol_str_slice_from_str(mdata->scheme ? : "http");
    url.user = sol_str_slice_from_str(mdata->user ? : "");
    url.password = sol_str_slice_from_str(mdata->password ? : "");
    url.host = sol_str_slice_from_str(mdata->host ? : "");
    url.path = sol_str_slice_from_str(mdata->path ? : "");
    url.fragment = sol_str_slice_from_str(mdata->fragment ? :  "");
    url.port = mdata->port;

    r = sol_http_create_full_uri(&uri, url, &mdata->params);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_CREATE_URL__OUT__OUT,
        sol_buffer_steal(&uri, NULL));
    return r;
}

#include "http-client-gen.c"
