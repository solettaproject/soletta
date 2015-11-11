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

#include "sol-flow/oauth.h"
#include "sol-flow.h"
#include "sol-http.h"
#include "sol-http-client.h"
#include "sol-http-server.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-network.h"
#include "sol-random.h"
#include "sol-util.h"
#include "sol-vector.h"
#include "sol_config.h"


struct v1_data {
    struct sol_ptr_vector pending_conns;
    struct sol_ptr_vector pending_digests;
    char *request_token_url, *authorize_token_url, *access_token_url,
    *consumer_key, *consumer_key_secret, *namespace,
    *start_handler_url, *callback_handler_url;
};

struct v1_request_data {
    struct sol_flow_node *node;
    struct sol_http_request *request;
    char *timestamp, *nonce, *callback_url;
};

struct oauth_node_type {
    struct sol_flow_node_type base;
    struct sol_http_server *server;
    uint16_t server_ref;
};

static int
server_ref(struct oauth_node_type *oauth)
{
    if (!oauth->server) {
        oauth->server = sol_http_server_new(HTTP_SERVER_PORT);
        SOL_NULL_CHECK(oauth->server, -ENOMEM);
    }
    oauth->server_ref++;
    return 0;
}

static void
server_unref(struct oauth_node_type *oauth)
{
    oauth->server_ref--;
    if (!oauth->server_ref) {
        sol_http_server_del(oauth->server);
        oauth->server = NULL;
    }
}

static void
v1_access_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    int r;
    const char *failed_message = "Authentication has failed";
    const char *success_message = "Authentication has worked";
    struct v1_request_data *req_data = data;
    struct v1_data *mdata = sol_flow_node_get_private_data(req_data->node);
    struct sol_http_response access_response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .content = SOL_BUFFER_INIT_EMPTY,
        .param = SOL_HTTP_REQUEST_PARAM_INIT,
        .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR,
    };

    r = sol_buffer_set_slice(&access_response.content,
        sol_str_slice_from_str(failed_message));
    if (r < 0)
        SOL_WRN("Could not set the reponse's message properly");

    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

    SOL_NULL_CHECK_GOTO(response, end);
    SOL_HTTP_RESPONSE_CHECK_API_GOTO(response, end);
    SOL_INT_CHECK_GOTO(response->content.used, == 0, end);

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Response from %s - %d", response->url, response->response_code);
        goto end;
    }

    access_response.response_code = SOL_HTTP_STATUS_OK;
    r = sol_buffer_set_slice(&access_response.content,
        sol_str_slice_from_str(success_message));
    if (r < 0)
        SOL_WRN("Could not set the reponse's message properly");

    r = sol_flow_send_string_packet(req_data->node,
        SOL_FLOW_NODE_TYPE_OAUTH_V1__OUT__TOKEN, response->content.data);
    if (r < 0)
        SOL_WRN("Could not send the packet with token: %s", (char *)response->content.data);

end:
    if (access_response.response_code != SOL_HTTP_STATUS_OK)
        sol_flow_send_error_packet(req_data->node, EINVAL,
            "Could not get access token");

    r = sol_http_server_send_response(req_data->request, &access_response);
    if (r < 0) {
        SOL_WRN("Could not send fail response for %s",
            sol_http_request_get_url(req_data->request));

    }
    sol_buffer_fini(&access_response.content);
    free(req_data->timestamp);
    free(req_data->callback_url);
    free(req_data->nonce);
    free(req_data);
}

static int
v1_authorize_response_cb(void *data, struct sol_http_request *request)
{
    int r;
    uint16_t idx;
    char *verifier = NULL, *token = NULL;
    struct sol_http_param_value *value;
    struct sol_http_client_connection *connection;
    struct sol_http_param params = SOL_HTTP_REQUEST_PARAM_INIT;
    struct sol_flow_node *node = data;
    struct v1_data *mdata = sol_flow_node_get_private_data(node);
    struct v1_request_data *req_data = calloc(1, sizeof(struct v1_request_data));

    SOL_NULL_CHECK_GOTO(req_data, err);

    SOL_HTTP_PARAM_FOREACH_IDX (sol_http_request_get_params(request), value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_QUERY_PARAM:
            if (sol_str_slice_str_eq(value->value.key_value.key, "oauth_verifier") && !verifier) {
                verifier = sol_str_slice_to_string(value->value.key_value.value);
            } else if (sol_str_slice_str_eq(value->value.key_value.key, "oauth_token") && !token) {
                token = sol_str_slice_to_string(value->value.key_value.value);
            }
            break;
        default:
            break;
        }
    }

    SOL_NULL_CHECK_GOTO(verifier, err);
    SOL_NULL_CHECK_GOTO(token, err);

    req_data->request = request;
    req_data->node = node;
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_token", token)) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_verifier", verifier))) {
        goto err_params;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->access_token_url,
        &params, v1_access_finished, req_data);
    SOL_NULL_CHECK_GOTO(connection, err_connection);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);

    sol_http_param_free(&params);
    free(verifier);
    free(token);
    return 0;

err_append:
    sol_http_client_connection_cancel(connection);
err_connection:
err_params:
    sol_http_param_free(&params);
err:
    free(req_data->timestamp);
    free(req_data->nonce);
    free(req_data);
    free(token);
    free(verifier);
    r = sol_http_server_send_response(request, &((struct sol_http_response) {
            SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
            .content = SOL_BUFFER_INIT_EMPTY,
            .param = SOL_HTTP_REQUEST_PARAM_INIT,
            .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR
        }));
    return r;
}

static void
v1_close(struct sol_flow_node *node, void *data)
{
    struct sol_http_client_connection *connection;
    struct sol_message_digest *digest;
    struct oauth_node_type *type;
    struct v1_data *mdata = data;
    uint16_t i;

    type = (struct oauth_node_type *)sol_flow_node_get_type(node);

    sol_http_server_unregister_handler(type->server, mdata->start_handler_url);
    sol_http_server_unregister_handler(type->server, mdata->callback_handler_url);
    server_unref(type);

    free(mdata->start_handler_url);
    free(mdata->callback_handler_url);
    free(mdata->authorize_token_url);
    free(mdata->request_token_url);
    free(mdata->access_token_url);
    free(mdata->namespace);
    free(mdata->consumer_key);
    free(mdata->consumer_key_secret);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_conns, connection, i)
        sol_http_client_connection_cancel(connection);
    sol_ptr_vector_clear(&mdata->pending_conns);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_digests, digest, i)
        sol_message_digest_del(digest);
    sol_ptr_vector_clear(&mdata->pending_digests);
}

static int
v1_parse_response(struct v1_request_data *req_data, const struct sol_http_response *response)
{
    int r;
    uint16_t idx;
    struct sol_vector tokens;
    struct sol_str_slice *slice;
    struct v1_data *mdata = sol_flow_node_get_private_data(req_data->node);
    char *token = NULL, *url = NULL;
    struct sol_http_response start_response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .content = SOL_BUFFER_INIT_EMPTY,
        .param = SOL_HTTP_REQUEST_PARAM_INIT,
        .response_code = SOL_HTTP_STATUS_FOUND,
    };

    tokens = sol_util_str_split(
        sol_str_slice_from_str(response->content.data), "&", 0);
    SOL_VECTOR_FOREACH_IDX (&tokens, slice, idx) {
        if (streqn(slice->data, "oauth_token=", sizeof("oauth_token=") - 1)) {
            token = strndup(slice->data, slice->len);
            break;
        }
    }
    sol_vector_clear(&tokens);
    SOL_NULL_CHECK(token, -EINVAL);

    r = asprintf(&url, "%s?%s", mdata->authorize_token_url, token);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    if (!sol_http_param_add(&start_response.param,
        SOL_HTTP_REQUEST_PARAM_HEADER("Location", url)))
        goto err_param;

    start_response.url = url;

    r = sol_http_server_send_response(req_data->request, &start_response);
    SOL_INT_CHECK_GOTO(r, < 0, err_send);

err_send:
    sol_http_param_free(&start_response.param);
err_param:
    free(url);
err:
    free(token);
    return r;
}

static void
v1_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    int r;
    struct v1_request_data *req_data = data;
    struct v1_data *mdata = sol_flow_node_get_private_data(req_data->node);

    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

    SOL_NULL_CHECK_GOTO(response, err);
    SOL_HTTP_RESPONSE_CHECK_API_GOTO(response, err);
    SOL_INT_CHECK_GOTO(response->content.used, == 0, err);

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Response from %s - %d", response->url, response->response_code);
        goto err;
    }

    r = v1_parse_response(req_data, response);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    goto end;

err:
    r = sol_http_server_send_response(req_data->request, &((struct sol_http_response) {
            SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
            .content = SOL_BUFFER_INIT_EMPTY,
            .param = SOL_HTTP_REQUEST_PARAM_INIT,
            .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR
        }));
    if (r < 0) {
        SOL_WRN("Could not send fail response for %s",
            sol_http_request_get_url(req_data->request));
    }
    sol_flow_send_error_packet(req_data->node, EINVAL,
        "Could not get temporary tokens");
end:
    free(req_data->timestamp);
    free(req_data->nonce);
    free(req_data);
}

static char *
get_callback_url(const struct sol_http_request *request, const char *basename)
{
    char *url, buf[SOL_INET_ADDR_STRLEN];
    struct sol_network_link_addr addr;
    int r;

    r = sol_http_request_get_interface_address(request, &addr);
    SOL_INT_CHECK(r, < 0, NULL);

    sol_network_addr_to_str(&addr, buf, sizeof(buf));
    r = asprintf(&url, "http://%s:%d/%s/oauth_callback",
        buf, addr.port, basename);

    SOL_INT_CHECK(r, < 0, NULL);
    return url;
}

static void
digest_ready_cb(void *data, struct sol_message_digest *handle, struct sol_blob *output)
{
    int r;
    struct sol_http_param params;
    struct sol_http_client_connection *connection;
    struct v1_request_data *req_data = data;
    struct v1_data *mdata = sol_flow_node_get_private_data(req_data->node);
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;

    if (sol_ptr_vector_remove(&mdata->pending_digests, handle) < 0)
        SOL_WRN("Failed to remove pending digest %p", handle);

    r = sol_buffer_append_as_base64(&buffer, sol_str_slice_from_blob(output), SOL_BASE64_MAP);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_QUERY("oauth_callback", req_data->callback_url)) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_consumer_key", mdata->consumer_key)) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_nonce", req_data->nonce)) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_signature_method", "HMAC-SHA1")) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_timestamp", req_data->timestamp)) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_version", "1.0")) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("oauth_signature", buffer.data))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        sol_buffer_fini(&buffer);
        goto err;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->request_token_url,
        &params, v1_request_finished, req_data);
    sol_buffer_fini(&buffer);
    sol_http_param_free(&params);
    SOL_NULL_CHECK_GOTO(connection, err);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);
    return;

err_append:
    sol_http_client_connection_cancel(connection);
err:
    free(req_data->timestamp);
    free(req_data->nonce);
    r = sol_http_server_send_response(req_data->request, &((struct sol_http_response) {
            SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
            .content = SOL_BUFFER_INIT_EMPTY,
            .param = SOL_HTTP_REQUEST_PARAM_INIT,
            .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR
        }));
    if (r < 0) {
        SOL_WRN("Could not send fail response for %s",
            sol_http_request_get_url(req_data->request));
    }
    sol_flow_send_error_packet(req_data->node, EINVAL,
        "Could not create the request to get temporary tokens");
    free(req_data->callback_url);
    free(req_data);
}

static char *
generate_nonce(void)
{
    int r;
    ssize_t size;
    char *nonce = NULL;
    struct sol_random *engine;
    struct sol_str_slice slice;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;

    engine = sol_random_new(SOL_RANDOM_URANDOM, 0);
    SOL_NULL_CHECK(engine, NULL);

    size = sol_random_fill_buffer(engine, &buffer, 16);
    sol_random_del(engine);
    SOL_INT_CHECK_GOTO(size, < 16, end);

    slice = SOL_STR_SLICE_STR(buffer.data, buffer.used);
    size = sol_util_base16_calculate_encoded_len(slice);
    SOL_INT_CHECK_GOTO(size, < 0, end);

    nonce = calloc(1, size + 1);
    SOL_NULL_CHECK_GOTO(nonce, end);

    r = sol_util_base16_encode(nonce, size, slice, false);
    if (r < 0) {
        SOL_WRN("Could not enconde the oauth_nonce");
        free(nonce);
        nonce = NULL;
    } else {
        nonce[size] = '\0';
    }

end:
    sol_buffer_fini(&buffer);
    return nonce;
}

static int
v1_request_start_cb(void *data, struct sol_http_request *request)
{
    int r;
    char *signature, *params;
    struct sol_flow_node *node = data;
    struct sol_buffer escaped_callback, escaped_params, escaped_url;
    struct v1_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_message_digest *digest;
    struct sol_blob *blob;
    struct sol_message_digest_config *digest_config = &(struct sol_message_digest_config) {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = "hmac(sha1)",
        .on_digest_ready = digest_ready_cb,
        .key = sol_str_slice_from_str(mdata->consumer_key_secret),
    };
    struct v1_request_data *req_data = calloc(1, sizeof(struct v1_request_data));

    SOL_NULL_CHECK_GOTO(req_data, err);

    req_data->callback_url = get_callback_url(request, mdata->namespace);
    SOL_NULL_CHECK_GOTO(req_data->callback_url, err_callback);

    req_data->node = node;
    req_data->request = request;
    req_data->nonce = generate_nonce();
    SOL_NULL_CHECK_GOTO(req_data->nonce, err_nonce);

    r = asprintf(&req_data->timestamp, "%li", (long int)time(NULL));
    SOL_INT_CHECK_GOTO(r, < 0, err_timestamp);

    digest_config->data = req_data;
    digest = sol_message_digest_new(digest_config);
    SOL_NULL_CHECK_GOTO(digest, err_digest);

    r = sol_ptr_vector_append(&mdata->pending_digests, digest);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);

    r = sol_http_encode_slice(&escaped_callback, sol_str_slice_from_str(req_data->callback_url));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape_callback);

    r  = asprintf(&params,
        "oauth_callback=%.*s"
        "&oauth_consumer_key=%s"
        "&oauth_nonce=%s"
        "&oauth_signature_method=HMAC-SHA1"
        "&oauth_timestamp=%s"
        "&oauth_version=1.0",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&escaped_callback)),
        mdata->consumer_key, req_data->nonce,
        req_data->timestamp);
    SOL_INT_CHECK_GOTO(r, < 0, err_params);

    r = sol_http_encode_slice(&escaped_params, sol_str_slice_from_str(params));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape_params);

    r = sol_http_encode_slice(&escaped_url, sol_str_slice_from_str(mdata->request_token_url));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape);

    r = asprintf(&signature, "POST&%.*s&%.*s",
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&escaped_url)),
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&escaped_params)));
    SOL_INT_CHECK_GOTO(r, < 0, err_signature);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, signature, strlen(signature));
    SOL_NULL_CHECK_GOTO(blob, err_blob);

    r = sol_message_digest_feed(digest, blob, true);
    SOL_INT_CHECK_GOTO(r, < 0, err_feed);

    sol_blob_unref(blob);
    sol_buffer_fini(&escaped_params);
    sol_buffer_fini(&escaped_callback);
    free(params);
    sol_buffer_fini(&escaped_url);

    return 0;

err_feed:
    sol_blob_unref(blob);
err_blob:
    free(signature);
err_signature:
    sol_buffer_fini(&escaped_url);
err_escape:
    sol_buffer_fini(&escaped_params);
err_escape_params:
    free(params);
err_params:
    sol_buffer_fini(&escaped_callback);
err_escape_callback:
    sol_ptr_vector_remove(&mdata->pending_digests, digest);
err_append:
    sol_message_digest_del(digest);
err_digest:
    free(req_data->timestamp);
err_timestamp:
    free(req_data->nonce);
err_nonce:
    free(req_data->callback_url);
err_callback:
    free(req_data);
err:
    r = sol_http_server_send_response(request, &((struct sol_http_response) {
            SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
            .content = SOL_BUFFER_INIT_EMPTY,
            .param = SOL_HTTP_REQUEST_PARAM_INIT,
            .response_code = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR
        }));
    return r;
}

static int
v1_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct v1_data *mdata = data;
    struct oauth_node_type *type;
    struct sol_flow_node_type_oauth_v1_options *opts =
        (struct sol_flow_node_type_oauth_v1_options *)options;

    type = (struct oauth_node_type *)sol_flow_node_get_type(node);

    r = server_ref(type);
    SOL_INT_CHECK(r, < 0, r);

    mdata->request_token_url = strdup(opts->request_token_url);
    SOL_NULL_CHECK_GOTO(mdata->request_token_url, url_err);

    mdata->authorize_token_url = strdup(opts->authorize_token_url);
    SOL_NULL_CHECK_GOTO(mdata->request_token_url, authorize_err);

    mdata->access_token_url = strdup(opts->access_token_url);
    SOL_NULL_CHECK_GOTO(mdata->request_token_url, access_err);

    mdata->namespace = strdup(opts->namespace);
    SOL_NULL_CHECK_GOTO(mdata->namespace, namespace_err);

    mdata->consumer_key = strdup(opts->consumer_key);
    SOL_NULL_CHECK_GOTO(mdata->consumer_key, consumer_err);

    mdata->consumer_key_secret = strdup(opts->consumer_key_secret);
    SOL_NULL_CHECK_GOTO(mdata->consumer_key_secret, secret_err);

    sol_ptr_vector_init(&mdata->pending_conns);
    sol_ptr_vector_init(&mdata->pending_digests);

    r = asprintf(&mdata->start_handler_url, "%s/oauth_start", mdata->namespace);
    SOL_INT_CHECK_GOTO(r, < 0, err_start);

    r = asprintf(&mdata->callback_handler_url, "%s/oauth_callback", mdata->namespace);
    SOL_INT_CHECK_GOTO(r, < 0, err_callback);

    r = sol_http_server_register_handler(type->server, mdata->start_handler_url,
        v1_request_start_cb, node);
    SOL_INT_CHECK_GOTO(r, < 0, err_register_handler);

    r = sol_http_server_register_handler(type->server, mdata->callback_handler_url,
        v1_authorize_response_cb, node);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return 0;

err:
    sol_http_server_unregister_handler(type->server,
        mdata->start_handler_url);
err_register_handler:
    free(mdata->callback_handler_url);
err_callback:
    free(mdata->start_handler_url);
err_start:
    free(mdata->consumer_key_secret);
secret_err:
    free(mdata->consumer_key);
consumer_err:
    free(mdata->namespace);
namespace_err:
    free(mdata->access_token_url);
access_err:
    free(mdata->authorize_token_url);
authorize_err:
    free(mdata->request_token_url);
url_err:
    server_unref(type);
    return r;
}

#include "oauth-gen.c"
