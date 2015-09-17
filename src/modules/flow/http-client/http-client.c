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
    struct sol_flow_node *node;
    struct sol_ptr_vector pending_conns;
    char *url;
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
common_open(struct http_data *mdata, const char *url)
{
    mdata->url = strdup(url);
    SOL_NULL_CHECK(mdata->url, -ENOMEM);

    sol_ptr_vector_init(&mdata->pending_conns);

    return 0;
}

static int
check_response(struct http_data *mdata,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

    if (!response) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Error while reaching %s", mdata->url);
        return -EINVAL;
    }
    SOL_HTTP_RESPONSE_CHECK_API(response, -EINVAL);

    if (!response->content.used) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Empty response from %s", mdata->url);
        return -EINVAL;
    }

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "%s returned an unknown response code: %d",
            mdata->url, response->response_code);
        return -EINVAL;
    }

    return 0;
}

static void
boolean_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct http_data *mdata = data;
    bool result = true;

    if (check_response(mdata, connection, response) < 0)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            if (!strncasecmp("true", value.start, value.end - value.start))
                result = true;
            else if (!strncasecmp("false", value.start, value.end - value.start))
                result = false;
            else
                goto err;
        }
    } else {
        if (!strncasecmp("true", response->content.data, response->content.used))
            result = true;
        else if (!strncasecmp("false", response->content.data, response->content.used))
            result = false;
        else
            goto err;

    }

    sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);

    return;

err:
    sol_flow_send_error_packet(mdata->node, EINVAL,
        "%s Could not parser the url's contents ", mdata->url);
}

static int
boolean_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url,
        &params, boolean_request_finished, mdata);

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
boolean_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    bool b;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

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
    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, boolean_request_finished, mdata);
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
boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_client_boolean_options *opts =
        (struct sol_flow_node_type_http_client_boolean_options *)options;
    mdata->node = node;

    return common_open(mdata, opts->url);
}


/*
 * --------------------------------- string node -----------------------------
 */
static void
string_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct http_data *mdata = data;
    char *result = NULL;

    if (check_response(mdata, connection, response) < 0)
        return;

    if (streq(response->content_type, "application/json")) {
        struct sol_json_scanner scanner;
        struct sol_json_token token, key, value;
        enum sol_json_loop_reason reason;

        sol_json_scanner_init(&scanner, response->content.data, response->content.used);
        SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
            result = strndup(value.start + 1, value.end - value.start - 2);
        }
    } else {
        result = strndup(response->content.data, response->content.used);
    }

    if (result) {
        sol_flow_send_string_take_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_HTTP_CLIENT_BOOLEAN__OUT__OUT, result);
    } else {
        sol_flow_send_error_packet(mdata->node, -ENOMEM,
            "%s Could not parser the url's contents ", mdata->url);
    }
}

static int
string_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url,
        &params, string_request_finished, mdata);

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
string_post_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    const char *value;
    struct sol_http_param params;
    struct http_data *mdata = data;
    struct sol_http_client_connection *connection;

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

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->url,
        &params, string_request_finished, mdata);
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
string_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_client_string_options *opts =
        (struct sol_flow_node_type_http_client_string_options *)options;
    mdata->node = node;

    return common_open(mdata, opts->url);
}
#include "http-client-gen.c"
