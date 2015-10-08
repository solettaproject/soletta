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

struct http_data {
    struct sol_ptr_vector pending_conns;
    struct sol_str_slice key;
    char *url;
    char *content_type;
    bool machine_id;
    bool strict;
};

struct http_client_node_type {
    struct sol_flow_node_type base;
    int (*process_token)(struct sol_flow_node *node, struct sol_json_token *key, struct sol_json_token *value);
    int (*process_data)(struct sol_flow_node *node, struct sol_http_response *response);
};

static int
machine_id_header_add(struct sol_http_param *params)
{
    int r;
    const char *id;

    id = sol_platform_get_machine_id();
    SOL_NULL_CHECK(id, -errno);

    r = sol_http_param_add(params,
        SOL_HTTP_REQUEST_PARAM_HEADER("X-Soletta-Machine-ID", id));
    SOL_INT_CHECK(r, != true, -1);

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

    free(mdata->url);
    free(mdata->content_type);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_conns, connection, i)
        sol_http_client_connection_cancel(connection);
    sol_ptr_vector_clear(&mdata->pending_conns);
}

static int
common_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_client_boolean_options *opts =
        (struct sol_flow_node_type_http_client_boolean_options *)options;

    mdata->url = strdup(opts->url);
    SOL_NULL_CHECK(mdata->url, -ENOMEM);

    mdata->machine_id = opts->machine_id;

    get_key(mdata);
    sol_ptr_vector_init(&mdata->pending_conns);

    return 0;
}

static int
common_url_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    const char *url;
    struct http_data *mdata = data;

    r = sol_flow_packet_get_string(packet, &url);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->url);
    mdata->url = strdup(url);
    SOL_NULL_CHECK(mdata->url, -ENOMEM);

    get_key(mdata);

    return 0;
}

static int
check_response(struct http_data *mdata, struct sol_flow_node *node,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

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
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

    SOL_NULL_CHECK(mdata->url, -EINVAL);

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) {
        SOL_WRN("Failed to set query params");
        goto err;
    }

    if (mdata->machine_id) {
        r = machine_id_header_add(&params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url,
        &params, common_request_finished, node);

    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;

err:
    sol_http_param_free(&params);
    return -ENOMEM;
}

static int
common_post_process(struct sol_flow_node *node, void *data, ...)
{
    int r;
    va_list ap;
    char *key, *value;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

    sol_http_param_init(&params);
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
    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection for %s", mdata->url);
        sol_http_client_connection_cancel(connection);
        return r;
    }

    return 0;

err:
    sol_http_param_free(&params);
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

    struct sol_flow_node_type_http_client_get_string_options *opts =
        (struct sol_flow_node_type_http_client_get_string_options *)options;

    if (opts->url) {
        mdata->url = strdup(opts->url);
        SOL_NULL_CHECK(mdata->url, -ENOMEM);
    }

    if (opts->content_type) {
        mdata->content_type = strdup(opts->content_type);
        SOL_NULL_CHECK_GOTO(mdata->content_type, err_content_type);
    }

    mdata->strict = opts->strict;
    mdata->machine_id = opts->machine_id;
    sol_ptr_vector_init(&mdata->pending_conns);
    return 0;

err_content_type:
    free(mdata->url);
    return -ENOMEM;
}

static int
generic_url_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_data *mdata = data;
    const char *url;
    int r;

    r = sol_flow_packet_get_string(packet, &url);
    SOL_INT_CHECK(r, < 0, r);

    SOL_DBG("New URL received:%s - old URL:%s", url, mdata->url);
    free(mdata->url);
    mdata->url = strdup(url);
    SOL_NULL_CHECK(mdata->url, -ENOMEM);
    return 0;
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

    SOL_DBG("Blob process - response from: %s", response->url);

    data = sol_buffer_steal(&response->content, &size);
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, data, size);
    if (!blob) {
        sol_flow_send_error_packet(node, ENOMEM,
            "Could not alloc memory for the response from %s", response->url);
        return -ENOMEM;
    }

    sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_GET_BLOB__OUT__OUT, blob);
    sol_blob_unref(blob);

    return 0;
}

static void
generic_request_finished(void *data,
    const struct sol_http_client_connection *conn,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    const struct http_client_node_type *type;

    SOL_INT_CHECK(check_response(mdata, node, conn, response), < 0);

    if (mdata->strict && mdata->content_type && response->content_type &&
        !streq(response->content_type, mdata->content_type)) {
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
generic_get_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_data *mdata = data;
    struct sol_http_client_connection *conn;
    struct sol_http_param params;
    int r;

    sol_http_param_init(&params);
    if (mdata->content_type && !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", mdata->content_type))) {
        SOL_ERR("Could not add the HTTP Accept's param");
        goto err;
    }

    if (mdata->machine_id) {
        r = machine_id_header_add(&params);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    conn = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url, &params,
        generic_request_finished, node);
    sol_http_param_free(&params);
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
    sol_http_param_free(&params);
    return -ENOMEM;
}

#include "http-client-gen.c"
