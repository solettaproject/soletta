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
#include "sol-util.h"
#include "sol-vector.h"
#include "sol_config.h"
#include "sol-str-table.h"

struct http_data {
    struct sol_ptr_vector pending_conns;
    struct sol_str_slice key;
    enum sol_http_method method;
    struct sol_http_params url_params;
    char *url;
    char *content_type;
    char *accept;
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
    int (*process_token)(struct sol_flow_node *node, struct sol_json_token *key,
        struct sol_json_token *value);
    int (*process_data)(struct sol_flow_node *node,
        struct sol_http_response *response);
    void (*close_node)(struct sol_flow_node *node, void *data);
    int (*setup_params)(struct http_data *mdata, struct sol_http_params *params);
    void (*http_response)(void *data,
        const struct sol_http_client_connection *conn,
        struct sol_http_response *response);
};

static int
set_basic_url_info(struct http_data *mdata, const char *full_uri)
{
    struct sol_http_url url, base_url;
    char *new_url;
    int r;

    r = sol_http_split_uri(sol_str_slice_from_str(full_uri), &url);
    SOL_INT_CHECK(r, < 0, r);

    memset(&base_url, 0, sizeof(struct sol_http_url));

    base_url.scheme = url.scheme;
    base_url.host = url.host;
    base_url.path = url.path;
    base_url.port = url.port;

    r = sol_http_create_uri(&new_url, base_url, NULL);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->url);
    mdata->url = new_url;

    sol_http_params_clear(&mdata->url_params);
    r = sol_http_decode_params(url.query,
        SOL_HTTP_PARAM_QUERY_PARAM, &mdata->url_params);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    if ((url.user.len || url.password.len) &&
        !sol_http_param_add_copy(&mdata->url_params,
        ((struct sol_http_param_value) {
            .type = SOL_HTTP_PARAM_AUTH_BASIC,
            .value.auth.user = url.user,
            .value.auth.password = url.password
        }))) {
        SOL_WRN("Could not add the user: %.*s and password: %.*s as"
            " parameters", SOL_STR_SLICE_PRINT(url.user),
            SOL_STR_SLICE_PRINT(url.password));
        r = -ENOMEM;
        goto err_exit;
    }

    if (url.fragment.len && !sol_http_param_add_copy(&mdata->url_params,
        ((struct sol_http_param_value) {
            .type = SOL_HTTP_PARAM_FRAGMENT,
            .value.key_value.key = url.fragment,
            .value.key_value.value = SOL_STR_SLICE_EMPTY
        }))) {
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

    r = sol_http_param_add(params,
        SOL_HTTP_REQUEST_PARAM_HEADER("X-Soletta-Machine-ID", id));
    SOL_INT_CHECK(r, != true, -ENOMEM);

    return 0;
}

static void
get_key(struct http_data *mdata)
{
    char *ptr = NULL;

    if (strstartswith(mdata->url, "http://"))
        ptr = strstr(mdata->url + strlen("http://"), "/");
    else if (strstartswith(mdata->url, "https://"))
        ptr = strstr(mdata->url + strlen("https://"), "/");

    if (ptr)
        mdata->key = sol_str_slice_from_str(ptr);
    else
        mdata->key = (struct sol_str_slice){.len = 0, .data = NULL };
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
    free(mdata->content_type);
    free(mdata->accept);
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

    sol_http_params_init(&mdata->url_params);

    if (opts->url) {
        r = set_basic_url_info(mdata, opts->url);
        SOL_INT_CHECK(r, < 0, r);
    }

    mdata->machine_id = opts->machine_id;

    get_key(mdata);
    sol_ptr_vector_init(&mdata->pending_conns);

    return 0;
}

static int
common_url_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct http_data *mdata = data;
    int r;

    r = set_basic_url_info_from_packet(data, packet);
    SOL_INT_CHECK(r, < 0, r);

    get_key(mdata);

    return 0;
}

static void
remove_connection(struct http_data *mdata,
    const struct sol_http_client_connection *connection)
{
    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);
}

static int
check_response(struct http_data *mdata, struct sol_flow_node *node,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{

    remove_connection(mdata, connection);
    if (!response) {
        sol_flow_send_error_packet(node, EINVAL,
            "Error while reaching %s", mdata->url);
        return -EINVAL;
    }
    SOL_HTTP_RESPONSE_CHECK_API(response, -EINVAL);

    if (!response->content.used) {
        sol_flow_send_error_packet(node, EINVAL,
            "Empty response from %s", mdata->url);
        return -EINVAL;
    }

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(node, EINVAL,
            "%s returned an unhandled response code: %d",
            mdata->url, response->response_code);
        return -EINVAL;
    }

    return 0;
}

static void
common_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    int ret;
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_client_node_type *type;

    if (check_response(mdata, node, connection, response) < 0)
        return;

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            if (!sol_json_token_str_eq(&token, mdata->key.data, mdata->key.len))
                continue;
            ret = type->process_token(node, &key, &value);
            if (ret < 0)
                goto err;
        }
    } else {
        ret = type->process_data(node, response);
        if (ret < 0)
            goto err;
    }

    return;

err:
    sol_flow_send_error_packet(node, ret,
        "%s Could not parse url contents ", mdata->url);
}

static int
common_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_http_params params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

    SOL_NULL_CHECK(mdata->url, -EINVAL);

    sol_http_params_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) {
        SOL_WRN("Failed to set query params");
        r = -ENOMEM;
        goto err;
    }

    if (mdata->machine_id) {
        r = machine_id_header_add(&params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url,
        &params, common_request_finished, node);

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
common_post_process(struct sol_flow_node *node, void *data, ...)
{
    int r;
    va_list ap;
    char *key, *value;
    struct sol_http_params params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

    sol_http_params_init(&params);
    if (!(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json")))) {
        SOL_WRN("Could not add the header '%s:%s' into request to %s",
            "Accept", "application/json", mdata->url);
        goto err;
    }

    if (mdata->machine_id) {
        r = machine_id_header_add(&params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    va_start(ap, data);
    while ((key = va_arg(ap, char *))) {
        value = va_arg(ap, char *);
        if (!(sol_http_param_add(&params, SOL_HTTP_REQUEST_PARAM_POST_FIELD(key, value)))) {
            SOL_WRN("Could not add header '%s:%s' into request to %s",
                key, value, mdata->url);
            va_end(ap);
            goto err;
        }
    }
    va_end(ap);

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
boolean_process_token(struct sol_flow_node *node,
    struct sol_json_token *key, struct sol_json_token *value)
{
    bool result;

    if (sol_json_token_get_type(value) == SOL_JSON_TYPE_TRUE)
        result = true;
    else if (sol_json_token_get_type(value) == SOL_JSON_TYPE_FALSE)
        result = false;
    else
        return -EINVAL;

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
}

static int
boolean_process_data(struct sol_flow_node *node,
    struct sol_http_response *response)
{
    bool result;

    if (!strncasecmp("true", response->content.data, response->content.used))
        result = true;
    else if (!strncasecmp("false", response->content.data, response->content.used))
        result = false;
    else
        return -EINVAL;

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
}

static int
boolean_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    bool b;

    r = sol_flow_packet_get_boolean(packet, &b);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, "value", b ? "true" : "false", NULL);
}

/*
 * --------------------------------- string node -----------------------------
 */
static int
string_process_token(struct sol_flow_node *node,
    struct sol_json_token *key, struct sol_json_token *value)
{
    char *result = NULL;

    result = strndup(value->start + 1, value->end - value->start - 2);
    if (result) {
        return sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
    }

    return -ENOMEM;
}

static int
string_process_data(struct sol_flow_node *node,
    struct sol_http_response *response)
{
    char *result = strndup(response->content.data, response->content.used);

    if (result) {
        return sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
    }

    return -ENOMEM;
}

static int
string_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    const char *value;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, "value", value, NULL);
}

/*
 * --------------------------------- irange node -----------------------------
 */
static int
int_process_token(struct sol_flow_node *node,
    struct sol_json_token *key, struct sol_json_token *value)
{
    struct sol_irange irange = SOL_IRANGE_INIT();
    enum sol_json_loop_reason reason;
    struct sol_json_scanner sub_scanner;
    struct sol_json_token sub_key, sub_value, token;

    sol_json_scanner_init(&sub_scanner, value->start, value->end - value->start);
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
int_process_data(struct sol_flow_node *node,
    struct sol_http_response *response)
{
    int value;

    errno = 0;
    value = strtol(response->content.data, NULL, 0);
    if (errno)
        return -EINVAL;

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_INT__OUT__OUT, value);
}

static int
int_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange value;
    char min[3 * sizeof(int)], max[3 * sizeof(int)],
        val[3 * sizeof(int)], step[3 * sizeof(int)];

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = snprintf(val, sizeof(val), "%d", value.val);
    SOL_INT_CHECK(r, < 0, r);
    r = snprintf(min, sizeof(min), "%d", value.min);
    SOL_INT_CHECK(r, < 0, r);
    r = snprintf(max, sizeof(max), "%d", value.max);
    SOL_INT_CHECK(r, < 0, r);
    r = snprintf(step, sizeof(step), "%d", value.step);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, "value", val, "min", min,
        "max", max, "step", step, NULL);
}

/*
 * --------------------------------- drange node -----------------------------
 */
static int
float_process_token(struct sol_flow_node *node,
    struct sol_json_token *key, struct sol_json_token *value)
{
    struct sol_drange drange = SOL_DRANGE_INIT();
    enum sol_json_loop_reason reason;
    struct sol_json_scanner sub_scanner;
    struct sol_json_token token, sub_key, sub_value;

    sol_json_scanner_init(&sub_scanner, value->start, value->end - value->start);
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
float_process_data(struct sol_flow_node *node,
    struct sol_http_response *response)
{
    double value;

    errno = 0;
    value = sol_util_strtodn(response->content.data, NULL, -1, false);
    if (errno)
        return -errno;

    return sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_FLOAT__OUT__OUT, value);
}

static int
float_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange value;
    char val[100], min[100], max[100], step[100];

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.val, val, 100);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.min, min, 100);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.max, max, 100);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_json_double_to_str(value.step, step, 100);
    SOL_INT_CHECK(r, < 0, r);

    return common_post_process(node, data, "value", val, "min", min,
        "max", max, "step", step, NULL);
}

/*
 * --------------------------------- generic nodes  -----------------------------
 */

static int
generic_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;
    int r;
    struct sol_flow_node_type_http_client_get_string_options *opts =
        (struct sol_flow_node_type_http_client_get_string_options *)options;

    sol_http_params_init(&mdata->url_params);

    if (opts->url) {
        r = set_basic_url_info(mdata, opts->url);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (opts->content_type) {
        mdata->content_type = strdup(opts->content_type);
        SOL_NULL_CHECK_GOTO(mdata->content_type, err_content_type);
    }

    if (opts->accept) {
        mdata->accept = strdup(opts->accept);
        SOL_NULL_CHECK_GOTO(mdata->accept, err_accept);
    }

    mdata->strict = opts->strict;
    mdata->machine_id = opts->machine_id;
    sol_ptr_vector_init(&mdata->pending_conns);
    mdata->method = SOL_HTTP_METHOD_GET;
    return 0;

err_accept:
    free(mdata->accept);
err_content_type:
    sol_http_params_clear(&mdata->url_params);
    free(mdata->url);
    return -ENOMEM;
}

static int
generic_url_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_basic_url_info_from_packet(data, packet);
}

static int
get_string_process(struct sol_flow_node *node, struct sol_http_response *response)
{
    char *result;

    SOL_DBG("String process - response from: %s", response->url);
    result = strndup(response->content.data, response->content.used);

    if (!result) {
        sol_flow_send_error_packet(node, ENOMEM,
            "Could not alloc memory for the response from: %s", response->url);
        return -ENOMEM;
    }

    sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_STRING__OUT__OUT, result);

    return 0;
}

static int
get_blob_process(struct sol_flow_node *node, struct sol_http_response *response)
{
    struct sol_blob *blob;
    size_t size;
    void *data;
    int r;

    SOL_DBG("Blob process - response from: %s", response->url);

    data = sol_buffer_steal(&response->content, &size);
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, data, size);
    if (!blob) {
        sol_flow_send_error_packet(node, ENOMEM,
            "Could not alloc memory for the response from %s", response->url);
        return -ENOMEM;
    }

    r = sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_BLOB__OUT__OUT, blob);
    sol_blob_unref(blob);

    return r;
}

static int
get_json_process(struct sol_flow_node *node, struct sol_http_response *response)
{
    struct sol_blob *blob;
    struct sol_json_scanner object_scanner, array_scanner;
    struct sol_str_slice trimmed_str;
    int r;

    SOL_DBG("Json process - response from: %s", response->url);

    trimmed_str = sol_str_slice_trim(sol_buffer_get_slice(&response->content));
    sol_json_scanner_init(&object_scanner, trimmed_str.data, trimmed_str.len);
    sol_json_scanner_init(&array_scanner, trimmed_str.data, trimmed_str.len);

    (void)sol_buffer_steal(&response->content, NULL);
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, trimmed_str.data,
        trimmed_str.len);

    if (!blob) {
        sol_flow_send_error_packet(node, ENOMEM,
            "Could not create the json blob packet from: %s", response->url);
        SOL_ERR("Could not create the json blob packet from: %s",
            response->url);
        return -ENOMEM;
    }

    if (sol_json_is_valid_type(&object_scanner, SOL_JSON_TYPE_OBJECT_START)) {
        r = sol_flow_send_json_object_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_JSON__OUT__OBJECT, blob);
    } else if (sol_json_is_valid_type(&array_scanner,
        SOL_JSON_TYPE_ARRAY_START)) {
        r = sol_flow_send_json_array_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_JSON__OUT__ARRAY, blob);
    } else {
        sol_flow_send_error_packet(node, EINVAL, "The json received from:%s"
            " is not valid json-object or json-array", response->url);
        SOL_ERR("The json received from:%s is not valid json-object or"
            " json-array", response->url);
        r = -EINVAL;
    }

    sol_blob_unref(blob);
    return r;
}

static void
generic_request_finished(void *data,
    const struct sol_http_client_connection *conn,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_client_node_type *type;

    if (check_response(mdata, node, conn, response) < 0) {
        SOL_ERR("Invalid http response from %s", mdata->url);
        return;
    }

    if (mdata->strict && mdata->accept && response->content_type &&
        !streq(response->content_type, mdata->accept)) {
        sol_flow_send_error_packet(node, EINVAL,
            "Response has different content type. Received: %s - Desired: %s",
            response->content_type,
            mdata->content_type);
        return;
    }

    type = (const struct http_client_node_type *)
        sol_flow_node_get_type(node);

    type->process_data(node, response);
}

static int
make_http_request(struct sol_flow_node *node, struct http_data *mdata)
{
    const struct http_client_node_type *type;
    struct sol_http_client_connection *conn;
    struct sol_http_params params;
    struct sol_http_param_value *param;
    uint16_t i;
    int r;

    type = (const struct http_client_node_type *)
        sol_flow_node_get_type(node);

    sol_http_params_init(&params);
    if (mdata->accept && !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", mdata->accept))) {
        SOL_ERR("Could not add the HTTP Accept param");
        goto err;
    }

    if ((mdata->method == SOL_HTTP_METHOD_POST ||
        mdata->method == SOL_HTTP_METHOD_PUT) &&
        mdata->content_type && !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Content-Type", mdata->content_type))) {
        SOL_ERR("Could tno add the HTTP Content-Type param");
        goto err;
    }

    SOL_HTTP_PARAMS_FOREACH_IDX(&mdata->url_params, param, i) {
        if (!sol_http_param_add(&params, *param)) {
            SOL_ERR("Could not append the param - %.*s:%.*s",
                SOL_STR_SLICE_PRINT(param->value.key_value.key),
                SOL_STR_SLICE_PRINT(param->value.key_value.value));
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

    conn = sol_http_client_request(mdata->method, mdata->url,
        &params, type->http_response ? type->http_response :
        generic_request_finished, node);
    sol_http_params_clear(&params);
    SOL_NULL_CHECK(conn, -ENOMEM);

    r = sol_ptr_vector_append(&mdata->pending_conns, conn);
    if (r < 0) {
        SOL_ERR("Could not add store the pending connection. Aborting");
        sol_http_client_connection_cancel(conn);
        return -ENOMEM;
    }
    SOL_DBG("Making request to: %s", mdata->url);
    return 0;

err:
    sol_http_params_clear(&params);
    return -ENOMEM;
}

static int
generic_get_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return make_http_request(node, data);
}

static int
request_node_setup_params(struct http_data *data, struct sol_http_params *params)
{
    struct http_request_data *mdata = (struct http_request_data *)data;
    struct sol_http_param_value *param;
    uint16_t i;

    SOL_HTTP_PARAMS_FOREACH_IDX(&mdata->params, param, i) {
        if (!sol_http_param_add(params, *param)) {
            SOL_ERR("Could not append the param - %.*s:%.*s",
                SOL_STR_SLICE_PRINT(param->value.key_value.key),
                SOL_STR_SLICE_PRINT(param->value.key_value.value));
            return -ENOMEM;
        }
    }

    if (mdata->user && mdata->password && !sol_http_param_add(params,
        SOL_HTTP_REQUEST_PARAM_AUTH_BASIC(mdata->user, mdata->password))) {
        SOL_ERR("Could not set user and password params");
        return -ENOMEM;
    }

    if (!sol_http_param_add(params,
        SOL_HTTP_REQUEST_PARAM_ALLOW_REDIR(mdata->allow_redir))) {
        SOL_ERR("Could not set allow redirection param");
        return -ENOMEM;
    }

    if (!sol_http_param_add(params,
        SOL_HTTP_REQUEST_PARAM_TIMEOUT(mdata->timeout))) {
        SOL_ERR("Could not set the timeout param");
        return -ENOMEM;
    }

    if (mdata->content && !sol_http_param_add(params,
        SOL_HTTP_REQUEST_PARAM_POST_DATA(
        sol_str_slice_from_blob(mdata->content)))) {
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

    SOL_HTTP_PARAMS_FOREACH_IDX(params, param, i) {
        if (param->type == SOL_HTTP_PARAM_HEADER)
            to_append = headers;
        else if (param->type == SOL_HTTP_PARAM_COOKIE)
            to_append = cookies;
        else
            continue;

        resp_param = sol_vector_append(to_append);
        SOL_NULL_CHECK_GOTO(resp_param, err_exit);
        resp_param->key = sol_str_slice_to_string(param->value.key_value.key);
        resp_param->value =
            sol_str_slice_to_string(param->value.key_value.value);
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
    const struct sol_http_client_connection *conn,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_vector headers, cookies;
    struct sol_blob *blob;
    size_t buf_size;
    void *mem;
    int r;

    remove_connection(mdata, conn);

    if (!response) {
        SOL_ERR("Empty response from:%s", mdata->url);
        sol_flow_send_error_packet(node, EINVAL, "Empty response from:%s",
            mdata->url);
        return;
    }

    mem = sol_buffer_steal(&response->content, &buf_size);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, mem,
        buf_size);
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
    return;

err_blob:
    free(mem);
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

    free(mdata->base.content_type);
    mdata->base.content_type = NULL;
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
    if (!sol_http_param_add_copy(params, param)) {
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
request_node_content_type_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;

    return replace_string_from_packet(packet, &mdata->base.content_type);
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

static bool
get_user_and_pass_pos_from_url_params(struct http_request_data *mdata, uint16_t *pos)
{
    struct sol_http_param_value *param;
    uint16_t i;

    SOL_HTTP_PARAMS_FOREACH_IDX(&mdata->base.url_params, param, i) {
        if (param->type == SOL_HTTP_PARAM_AUTH_BASIC) {
            *pos = i;
            return true;
        }
    }
    return false;
}

static int
request_node_trigger_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_request_data *mdata = data;
    uint16_t pos;
    bool has_auth;
    int r;

    has_auth = get_user_and_pass_pos_from_url_params(mdata, &pos);

    if (has_auth && (mdata->user || mdata->password)) {
        r = sol_vector_del(&mdata->base.url_params.params, pos);
        SOL_INT_CHECK(r, < 0, r);
    }

    return make_http_request(node, data);
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

    mdata->key = strdup(opts->key);
    SOL_NULL_CHECK(mdata->key, -ENOMEM);
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

    if (!sol_http_param_add_copy(params, param)) {
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
split_query(const char *query, struct sol_http_params *params)
{
    struct sol_vector tokens;
    struct sol_str_slice *token;
    char *sep;
    uint16_t i;
    int r;

    tokens = sol_util_str_split(sol_str_slice_from_str(query), "&", 0);

    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        struct sol_str_slice key, value;

        sep = memchr(token->data, '=', token->len);
        key.data = token->data;
        if (sep) {
            key.len = sep - key.data;
            value.data = sep + 1;
            value.len = token->len - key.len - 1;
        } else {
            key.len = token->len;
            value.data = NULL;
            value.len = 0;
        }

        r = add_query(params, key, value);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

exit:
    sol_vector_clear(&tokens);
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
        r = split_query(opts->query, &mdata->params);
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
    char *uri;
    struct sol_http_url url;

    url.scheme = sol_str_slice_from_str(mdata->scheme ? : "http");
    url.user = sol_str_slice_from_str(mdata->user ? : "");
    url.password = sol_str_slice_from_str(mdata->password ? : "");
    url.host = sol_str_slice_from_str(mdata->host ? : "");
    url.path = sol_str_slice_from_str(mdata->path ? : "");
    url.fragment = sol_str_slice_from_str(mdata->fragment ? :  "");
    url.port = mdata->port;

    r = sol_http_create_uri(&uri, url, &mdata->params);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_CREATE_URL__OUT__OUT, uri);
    free(uri);
    return r;
}

#include "http-client-gen.c"
