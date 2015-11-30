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


#include "http.h"

#include "sol-http-client.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-update.h"
#include "sol-util.h"

struct update_http_handle {
    struct sol_http_client_connection *conn;
    char *url;
    union {
        void (*cb_get_metadata)(void *data, int status, const struct sol_buffer *metadata);
        struct {
            void (*cb_fetch_recv)(void *data, struct sol_buffer *buffer);
            void (*cb_fetch_end)(void *data, int status);
        };
    };
    const void *user_data;
    bool on_callback : 1;
    bool cancel : 1;
    bool cancelled : 1;
};

static void
delete_handle(struct update_http_handle *handle)
{
    free(handle->url);
    if (handle->conn)
        sol_http_client_connection_cancel(handle->conn);
    free(handle);
}

bool
metadata_to_update_info(const struct sol_buffer *metadata,
    struct sol_update_info *response)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    response->version = NULL;

    sol_json_scanner_init(&scanner, metadata->data, metadata->used);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "size")) {
            if (sol_json_token_get_uint64(&value, &response->size) != 0) {
                SOL_WRN("Could not get size of update file");
                goto err_size;
            }
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "version"))
            response->version = sol_json_token_get_unescaped_string_copy(&value);
        else
            SOL_DBG("Unknown response member: %.*s",
                SOL_STR_SLICE_PRINT(sol_json_token_to_slice(&token)));
    }

    if (!response->version) {
        SOL_WRN("Malformed check response");
        return false;
    }

    return true;

err_size:
    free((char *)response->version);
    return false;
}

static void
task_get_metadata_response(void *data, const struct sol_http_client_connection *conn,
    struct sol_http_response *http_response)
{
    struct update_http_handle *handle = data;

    handle->conn = NULL;
    handle->on_callback = true;

    if (http_response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Invalid response code from [%s] when checking for update: %d",
            handle->url, http_response->response_code);
        handle->cb_get_metadata((void *)handle->user_data, -http_response->response_code, NULL);
        delete_handle(handle);
        return;
    }

    if (!streq(http_response->content_type, "application/json")) {
        SOL_WRN("Invalid content type of response: [%s] expected [application/json]",
            http_response->content_type);
        handle->cb_get_metadata((void *)handle->user_data, -EINVAL, NULL);
        delete_handle(handle);
        return;
    }

    if (!http_response->content.used) {
        SOL_WRN("Empty response for version check");
        handle->cb_get_metadata((void *)handle->user_data, -EINVAL, NULL);
        delete_handle(handle);
        return;
    }

    handle->cb_get_metadata((void *)handle->user_data, 0, &http_response->content);
    delete_handle(handle);

    return;
}

struct update_http_handle *
http_get_metadata(const char *url,
    void (*cb)(void *data, int status, const struct sol_buffer *metadata),
    const void *data)
{
    struct sol_http_params params;
    struct update_http_handle *handle;

    SOL_NULL_CHECK(url, NULL);
    SOL_NULL_CHECK(cb, NULL);

    handle = calloc(1, sizeof(struct update_http_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->cb_get_metadata = cb;
    handle->user_data = data;
    handle->url = strdup(url);
    SOL_NULL_CHECK_GOTO(handle->url, err_url);

    sol_http_params_init(&params);

    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Accept", "application/json"))) {

        SOL_WRN("Could not add query parameter");
        goto err_param;
    }

    handle->conn = sol_http_client_request(SOL_HTTP_METHOD_GET, url, &params,
        task_get_metadata_response, handle);
    SOL_NULL_CHECK_GOTO(handle->conn, err_conn);

    sol_http_params_clear(&params);

    return handle;

err_conn:
    sol_http_params_clear(&params);
err_param:
    free(handle->url);
err_url:
    free(handle);

    return NULL;
}

static void
task_fetch_response(void *data, const struct sol_http_client_connection *conn,
    struct sol_http_response *http_response)
{
    struct update_http_handle *handle = data;

    /* We still get response even after cancelling connection. Here, we ignore it */
    if (handle->cancelled) {
        delete_handle(handle);
        return;
    }

    handle->conn = NULL;
    handle->on_callback = true;

    if (http_response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Invalid response code from [%s] when fetching update: %d",
            handle->url, http_response->response_code);
        handle->cb_fetch_end((void *)handle->user_data, -http_response->response_code);
        delete_handle(handle);
        return;
    }

    handle->cb_fetch_end((void *)handle->user_data, http_response->response_code);
    delete_handle(handle);

    return;
}

static ssize_t
task_fetch_recv(void *data, const struct sol_http_client_connection *conn,
    struct sol_buffer *buffer)
{
    struct update_http_handle *handle = data;

    handle->on_callback = true;

    handle->cb_fetch_recv((void *)handle->user_data, buffer);

    /* Cancelled task in middle of this callback */
    if (handle->cancel) {
        handle->cancelled = true;
        return -1; /* Cancel sol_http_client_connection */
    }

    handle->on_callback = false;

    return buffer->used;
}

struct update_http_handle *
http_fetch(const char *url,
    void (*recv_cb)(void *data, struct sol_buffer *buffer),
    void (*end_cb)(void *data, int status),
    const void *data, bool resume)
{
    struct update_http_handle *handle;
    struct sol_http_request_interface iface = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_REQUEST_INTERFACE_API_VERSION, )
        .recv_cb = task_fetch_recv,
        .response_cb = task_fetch_response
    };

    /* TODO handle resume stuff */

    SOL_NULL_CHECK(url, NULL);
    SOL_NULL_CHECK(recv_cb, NULL);
    SOL_NULL_CHECK(end_cb, NULL);

    handle = calloc(1, sizeof(struct update_http_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->cb_fetch_recv = recv_cb;
    handle->cb_fetch_end = end_cb;
    handle->user_data = data;
    handle->url = strdup(url);
    SOL_NULL_CHECK_GOTO(handle->url, err);

    /* TODO use chunked stuff when available */
    handle->conn = sol_http_client_request_with_interface(SOL_HTTP_METHOD_GET,
        handle->url, NULL, &iface, handle);
    SOL_NULL_CHECK_GOTO(handle->conn, err);

    return handle;

err:
    free(handle->url);
    free(handle);

    return NULL;
}

bool
http_cancel(struct update_http_handle *handle)
{
    if (!handle->on_callback)
        delete_handle(handle);
    else
        handle->cancel = true;

    return true;
}
