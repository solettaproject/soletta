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
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "sol-flow/http.h"
#include "sol-flow.h"
#include "sol-http.h"
#include "sol-vector.h"
#include "sol-http-client.h"
#include "sol-log.h"
#include "sol-util.h"

struct http_data {
    struct sol_flow_node *node;
    char *content_type;
    char *url;
    bool strict;
    struct sol_ptr_vector pending_conns;
};

struct http_node_type {
    struct sol_flow_node_type base;
    void (*response_process_func) (struct sol_http_response *response,
        struct http_data *mdata);
};

static void
http_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;
    uint16_t i;
    struct sol_http_client_connection *conn;

    free(mdata->url);
    free(mdata->content_type);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_conns, conn, i)
        sol_http_client_connection_cancel(conn);
    sol_ptr_vector_clear(&mdata->pending_conns);
}

static int
http_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct http_data *mdata = data;

    struct sol_flow_node_type_http_get_string_options *opts =
        (struct sol_flow_node_type_http_get_string_options *)options;

    if (opts->url) {
        mdata->url = strdup(opts->url);
        SOL_NULL_CHECK(mdata->url, -ENOMEM);
    }

    if (opts->content_type) {
        mdata->content_type = strdup(opts->content_type);
        SOL_NULL_CHECK_GOTO(mdata->content_type, err_content_type);
    }

    mdata->strict = opts->strict;
    sol_ptr_vector_init(&mdata->pending_conns);
    mdata->node = node;
    return 0;

err_content_type:
    free(mdata->url);
    return -ENOMEM;
}

static int
url_process(struct sol_flow_node *node, void *data, uint16_t port,
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

static void
get_string_process(struct sol_http_response *response, struct http_data *mdata)
{
    char *result;

    SOL_DBG("String process");
    result = strndup(response->content.data, response->content.used);

    if (!result) {
        sol_flow_send_error_packet(mdata->node, ENOMEM,
            "Could not alloc memory for the response");
        return;
    }
    sol_flow_send_string_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_HTTP_GET_STRING__OUT__OUT, result);
}

static void
get_blob_process(struct sol_http_response *response, struct http_data *mdata)
{
    struct sol_blob *blob;

    SOL_DBG("Blob process");

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, response->content.data,
        response->content.used);
    if (!blob) {
        sol_flow_send_error_packet(mdata->node, ENOMEM,
            "Could not alloc memory for the response");
        return;
    }

    sol_flow_send_blob_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_HTTP_GET_BLOB__OUT__OUT, blob);
}

static void
http_response_completed(void *data,
    const struct sol_http_client_connection *conn,
    struct sol_http_response *response)
{
    struct http_data *mdata = data;
    const struct http_node_type *type;

    SOL_DBG("Received response");

    if (sol_ptr_vector_remove(&mdata->pending_conns, conn))
        SOL_WRN("Could not remove the pending connection from the array");

    if (!response) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Could not reach %s", mdata->url);
        return;
    }

    SOL_HTTP_RESPONSE_CHECK_API(response);

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Error, HTTP response code:%d for URL:%s",
            response->response_code, mdata->url);
        return;
    }

    if (!response->content.used) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Empty response from %s", mdata->url);
        return;
    }

    if (mdata->strict && mdata->content_type && response->content_type &&
        !streq(response->content_type, mdata->content_type)) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Response has different content type. Received:%s - Desired:%s",
            response->content_type,
            mdata->content_type);
        return;
    }

    type = (const struct http_node_type *)
        sol_flow_node_get_type(mdata->node);

    type->response_process_func(response, mdata);
}

static int
trigger_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_data *mdata = data;
    struct sol_http_client_connection *conn;
    struct sol_http_param params;
    int r;

    if (!mdata->url) {
        SOL_ERR("The URL is NULL, could not execute a GET request");
        return -EINVAL;
    }

    SOL_DBG("Making http GET request.");

    sol_http_param_init(&params);
    if (mdata->content_type && !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", mdata->content_type))) {
        SOL_ERR("Could not add the HTTP params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    conn = sol_http_client_request(SOL_HTTP_METHOD_GET, mdata->url, &params,
        http_response_completed, mdata);
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
}

#include "http-gen.c"
