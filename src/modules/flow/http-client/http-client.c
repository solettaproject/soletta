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
#include "sol-util.h"
#include "sol-vector.h"
#include "sol_config.h"

struct http_data {
    struct sol_ptr_vector pending_conns;
    char *url;
};

struct http_client_node_type {
    struct sol_flow_node_type base;
    void (*request_finished_cb) (void *data, const struct sol_http_client_connection *connection,
        struct sol_http_response *response);
};

static void
common_close(struct sol_flow_node *node, void *data)
{
    struct sol_http_client_connection *connection;
    struct http_data *mdata = data;
    uint16_t i;

    free(mdata->url);
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
    SOL_NULL_CHECK(mdata->url, -EINVAL);

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

    mdata->url = strdup(url);
    SOL_NULL_CHECK(mdata->url, -ENOMEM);

    return 0;
}

static int
common_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;
    const struct http_client_node_type *type;

    SOL_NULL_CHECK(mdata->url, -EINVAL);

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url,
        &params, type->request_finished_cb, node);

    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

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
            "%s returned an unknown response code: %d",
            mdata->url, response->response_code);
        return -EINVAL;
    }

    return 0;
}

static int
check_key(const char *url, struct sol_json_token *token)
{
    char *ptr;

    if (strncmp(url, "http://", sizeof("http://") - 1) == 0)
        ptr = strstr(url + sizeof("http://") - 1, "/");
    else
        ptr = strstr(url, "/");
    SOL_NULL_CHECK(ptr, -EINVAL);

    return !sol_json_token_str_eq(token, ptr, strlen(ptr));
}

static void
boolean_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    bool result = true;

    if (check_response(mdata, node, connection, response) < 0)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            if (check_key(mdata->url, &key))
                continue;
            if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_TRUE)
                result = true;
            else if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_FALSE)
                result = false;
            else
                goto err;
            sol_flow_send_boolean_packet(node,
                SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
            return;
        }
    } else {
        if (!strncasecmp("true", response->content.data, response->content.used))
            result = true;
        else if (!strncasecmp("false", response->content.data, response->content.used))
            result = false;
        else
            goto err;
        sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
        return;
    }

err:
    sol_flow_send_error_packet(node, EINVAL,
        "%s Could not parser the url's contents ", mdata->url);
}

static int
boolean_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    bool b;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;
    const struct http_client_node_type *type;

    SOL_NULL_CHECK(mdata->url, -EINVAL);

    r = sol_flow_packet_get_boolean(packet, &b);
    SOL_INT_CHECK(r, < 0, r);

    sol_http_param_init(&params);
    if (!(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("value", b ? "true" : "false")))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, type->request_finished_cb, node);
    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;
}

/*
 * --------------------------------- string node -----------------------------
 */
static void
string_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    char *result = NULL;

    if (check_response(mdata, node, connection, response) < 0)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            if (check_key(mdata->url, &key))
                continue;
            result = strndup(value.start + 1, value.end - value.start - 2);
            break;
        }
    } else {
        result = strndup(response->content.data, response->content.used);
    }

    if (result) {
        sol_flow_send_string_take_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
    } else {
        sol_flow_send_error_packet(node, -ENOMEM,
            "%s Could not parser the url's contents ", mdata->url);
    }
}

static int
string_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    const char *value;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;
    const struct http_client_node_type *type;

    SOL_NULL_CHECK(mdata->url, -EINVAL);

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    sol_http_param_init(&params);
    if (!(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("value", value)))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, type->request_finished_cb, node);
    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;
}

/*
 * --------------------------------- irange node -----------------------------
 */
static void
int_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_irange irange;

    if (check_response(mdata, node, connection, response) < 0)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            struct sol_json_scanner sub_scanner;
            if (check_key(mdata->url, &key))
                continue;
            sol_json_scanner_init(&sub_scanner, value.start, value.end - value.start);
            SOL_JSON_SCANNER_OBJECT_LOOP (&sub_scanner, &token, &key, &value, reason) {
                if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "value")) {
                    if (sol_json_token_get_int32(&value, &irange.val) < 0)
                        goto error;
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "min")) {
                    if (sol_json_token_get_int32(&value, &irange.min) < 0)
                        goto error;
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "max")) {
                    if (sol_json_token_get_int32(&value, &irange.max) < 0)
                        goto error;
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "step")) {
                    if (sol_json_token_get_int32(&value, &irange.step) < 0)
                        goto error;
                }
            }
            sol_flow_send_irange_packet(node,
                SOL_FLOW_NODE_TYPE_HTTP_CLIENT_INT__OUT__OUT, &irange);
            return;
        }
    } else {
        errno = 0;
        irange.val = strtol(response->content.data, NULL, 0);
        if (errno)
            goto error;
        sol_flow_send_irange_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_INT__OUT__OUT, &irange);
        return;
    }

error:
    sol_flow_send_error_packet(node, -EINVAL,
        "%s Could not parser the url's contents ", mdata->url);
}

static int
int_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange value;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;
    char min[100], max[100], val[100], step[100];
    const struct http_client_node_type *type;

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

    sol_http_param_init(&params);
    if (!(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("value", val))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("min", min))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("max", max))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("step", step)))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, type->request_finished_cb, node);
    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;
}

/*
 * --------------------------------- drange node -----------------------------
 */
static void
float_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct sol_flow_node *node = data;
    struct http_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange drange;

    if (check_response(mdata, node, connection, response) < 0)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            struct sol_json_scanner sub_scanner;
            if (check_key(mdata->url, &key))
                continue;
            sol_json_scanner_init(&sub_scanner, value.start, value.end - value.start);
            SOL_JSON_SCANNER_OBJECT_LOOP (&sub_scanner, &token, &key, &value, reason) {
                int r;
                if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "value")) {
                    r = sol_json_token_get_double(&value, &drange.val);
                    SOL_INT_CHECK_GOTO(r, != 0, error);
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "min")) {
                    r = sol_json_token_get_double(&value, &drange.min);
                    SOL_INT_CHECK_GOTO(r, != 0, error);
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "max")) {
                    r = sol_json_token_get_double(&value, &drange.max);
                    SOL_INT_CHECK_GOTO(r, != 0, error);
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "step")) {
                    r = sol_json_token_get_double(&value, &drange.step);
                    SOL_INT_CHECK_GOTO(r, != 0, error);
                }
            }
            sol_flow_send_drange_packet(node,
                SOL_FLOW_NODE_TYPE_HTTP_CLIENT_FLOAT__OUT__OUT, &drange);
            return;
        }
    } else {
        errno = 0;
        drange.val = sol_util_strtodn(response->content.data, NULL, -1, false);
        if (errno)
            goto error;
        sol_flow_send_drange_packet(node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_FLOAT__OUT__OUT, &drange);
        return;
    }

error:
    sol_flow_send_error_packet(node, -EINVAL,
        "%s Could not parser the url's contents ", mdata->url);
}

static int
float_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange value;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;
    char val[100], min[100], max[100], step[100];
    const struct http_client_node_type *type;

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

    sol_http_param_init(&params);
    if (!(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("value", val))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("min", min))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("max", max))) ||
        !(sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("step", step)))) {
        SOL_WRN("Failed to set query params for url: %s", mdata->url);
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    type = (const struct http_client_node_type *)sol_flow_node_get_type(node);
    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, type->request_finished_cb, node);
    sol_http_param_free(&params);

    SOL_NULL_CHECK(connection, -ENOTCONN);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return r;
    }

    return 0;
}

#include "http-client-gen.c"
