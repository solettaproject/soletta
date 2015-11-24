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

#include "sol-flow/twitter.h"
#include "sol-flow.h"
#include "sol-http-client.h"
#include "sol-http.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-random.h"
#include "sol-util.h"
#include "sol-vector.h"

#define BASE_POST_URL "https://api.twitter.com/1.1/statuses/update.json"
#define BASE_TIMELINE_URL "https://api.twitter.com/1.1/statuses/home_timeline.json"

struct twitter_data {
    struct sol_ptr_vector pending_conns;
    struct sol_str_slice token, token_secret,
        consumer_secret,
        escaped_post_url, escaped_get_url;
    char *consumer_key;
};

struct callback_data {
    struct sol_flow_node *node;
    char *nonce, *status, *key;
    struct sol_http_client_connection *(*cb)(struct callback_data *cb_data,
        const char *authorization_header);
    long int timestamp;
};

static char *
generate_nonce(void)
{
    ssize_t size;
    char *nonce = NULL;
    struct sol_random *engine;
    struct sol_str_slice slice;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;

    engine = sol_random_new(SOL_RANDOM_DEFAULT, 0);
    SOL_NULL_CHECK(engine, NULL);

    size = sol_random_fill_buffer(engine, &buffer, 16);
    sol_random_del(engine);
    SOL_INT_CHECK_GOTO(size, < 16, end);

    slice = SOL_STR_SLICE_STR(buffer.data, buffer.used);
    size = sol_util_base16_calculate_encoded_len(slice);
    SOL_INT_CHECK_GOTO(size, < 0, end);

    nonce = malloc(size + 1);
    SOL_NULL_CHECK_GOTO(nonce, end);

    size = sol_util_base16_encode(nonce, size, slice, false);
    if (size < 0) {
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

static void
twitter_request_finished(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    int r = EINVAL;
    struct sol_flow_node *node = data;
    struct sol_blob *blob;
    struct sol_json_scanner object_scanner, array_scanner;
    struct twitter_data *mdata = sol_flow_node_get_private_data(node);

    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0) {
        sol_flow_send_error_packet(node, EINVAL,
            "Failed to find pending connection");
        return;
    }

    SOL_NULL_CHECK_GOTO(response, err);
    SOL_HTTP_RESPONSE_CHECK_API_GOTO(response, err);
    SOL_INT_CHECK_GOTO(response->content.used, == 0, err);

    sol_json_scanner_init(&object_scanner, response->content.data,
        response->content.used);
    sol_json_scanner_init(&array_scanner, response->content.data,
        response->content.used);

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Response from %s - %d",
            (char *)response->content.data, response->response_code);
        r = response->response_code;
        goto err;
    }

    r = ENOMEM;
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
        sol_buffer_steal(&response->content, NULL), response->content.used);
    SOL_NULL_CHECK_GOTO(blob, err);

    if (sol_json_is_valid_type(&object_scanner, SOL_JSON_TYPE_OBJECT_START)) {
        r = sol_flow_send_json_object_packet(node,
            SOL_FLOW_NODE_TYPE_TWITTER_CLIENT__OUT__OBJECT, blob);
    } else if (sol_json_is_valid_type(&array_scanner,
        SOL_JSON_TYPE_ARRAY_START)) {
        r = sol_flow_send_json_array_packet(node,
            SOL_FLOW_NODE_TYPE_TWITTER_CLIENT__OUT__ARRAY, blob);
    } else {
        sol_flow_send_error_packet(node, EINVAL, "The json received from:%s"
            " is not valid json-object or json-array", response->url);
        SOL_ERR("The json received from:%s is not valid json-object or"
            " json-array", response->url);
    }

    sol_blob_unref(blob);
    return;

err:
    sol_flow_send_error_packet(node, r,
        "Invalid response from twitter %s", response->url);
}

static struct sol_http_client_connection *
post_request(struct callback_data *cb_data, const char *authorization_header)
{
    struct sol_http_client_connection *connection = NULL;
    struct sol_http_param params;

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("status", cb_data->status)) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Content-Type",
        "application/x-www-form-urlencoded")) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Authorization", authorization_header))) {
        SOL_WRN("Failed to set params");
        goto err;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, BASE_POST_URL,
        &params, twitter_request_finished, cb_data->node);

err:
    sol_http_param_free(&params);
    return connection;
}

static struct sol_http_client_connection *
timeline_request(struct callback_data *cb_data, const char *authorization_header)
{
    struct sol_http_client_connection *connection = NULL;
    struct sol_http_param params;

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Content-Type",
        "application/x-www-form-urlencoded")) ||
        !sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Authorization", authorization_header))) {
        SOL_WRN("Failed to set params");
        goto err;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, BASE_TIMELINE_URL,
        &params, twitter_request_finished, cb_data->node);

err:
    sol_http_param_free(&params);
    return connection;
}

static void
digest_ready_cb(void *data, struct sol_message_digest *handle, struct sol_blob *output)
{
    int r;
    char *authorization_header;
    struct callback_data *cb_data = data;
    struct sol_http_client_connection *connection;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_buffer encoded_buf;
    struct twitter_data *mdata = sol_flow_node_get_private_data(cb_data->node);

    r = sol_buffer_append_as_base64(&buffer,
        sol_str_slice_from_blob(output), SOL_BASE64_MAP);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    r = sol_http_encode_slice(&encoded_buf,
        sol_str_slice_from_str((char *)buffer.data));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape);

    r = asprintf(&authorization_header, "OAuth "
        "oauth_consumer_key=\"%s\", "
        "oauth_nonce=\"%s\", "
        "oauth_signature=\"%.*s\", "
        "oauth_signature_method=\"HMAC-SHA1\", "
        "oauth_timestamp=\"%li\", "
        "oauth_token=\"%.*s\", "
        "oauth_version=\"1.0\"",
        mdata->consumer_key, cb_data->nonce,
        (unsigned int)encoded_buf.used, (char *)encoded_buf.data,
        cb_data->timestamp, SOL_STR_SLICE_PRINT(mdata->token));
    SOL_INT_CHECK_GOTO(r, < 0, err_header);

    connection = cb_data->cb(cb_data, authorization_header);
    SOL_NULL_CHECK_GOTO(connection, err_connection);

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);

    goto end;

err_append:
    sol_http_client_connection_cancel(connection);
end:
err_connection:
    free(authorization_header);
err_header:
    sol_buffer_fini(&encoded_buf);
err_escape:
    sol_buffer_fini(&buffer);
err:
    free(cb_data->status);
    free(cb_data->nonce);
    free(cb_data);
}

static int
post_status(struct sol_flow_node *node, const char *status)
{
    int r = -EINVAL;
    struct sol_blob *blob;
    struct callback_data *cb_data;
    struct sol_message_digest *digest;
    struct sol_buffer buf;
    struct twitter_data *mdata = sol_flow_node_get_private_data(node);
    char *signature, *params;
    struct sol_message_digest_config *digest_config =
        &(struct sol_message_digest_config) {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = "hmac(sha1)",
        .on_digest_ready = digest_ready_cb,
    };

    cb_data = calloc(1, sizeof(*cb_data));
    SOL_NULL_CHECK(cb_data, -ENOMEM);

    cb_data->node = node;
    cb_data->cb = post_request;

    cb_data->nonce = generate_nonce();
    SOL_NULL_CHECK_GOTO(cb_data->nonce, err);

    cb_data->timestamp = (long int)time(NULL);
    SOL_INT_CHECK_GOTO(cb_data->timestamp, < 0, err_timestamp);

    r = asprintf(&cb_data->key, "%.*s&%.*s",
        SOL_STR_SLICE_PRINT(mdata->consumer_secret),
        SOL_STR_SLICE_PRINT(mdata->token_secret));
    SOL_INT_CHECK_GOTO(r, < 0, err_key);

    cb_data->status = strdup(status);
    SOL_NULL_CHECK_GOTO(cb_data->status, err_status);

    r = sol_http_encode_slice(&buf,
        sol_str_slice_from_str(cb_data->status));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape_status);

    digest_config->data = cb_data;
    digest_config->key = sol_str_slice_from_str(cb_data->key);

    digest = sol_message_digest_new(digest_config);
    SOL_NULL_CHECK_GOTO(digest, err_digest);

    r  = asprintf(&params,
        "oauth_consumer_key=%s"
        "&oauth_nonce=%s"
        "&oauth_signature_method=HMAC-SHA1"
        "&oauth_timestamp=%li"
        "&oauth_token=%.*s"
        "&oauth_version=1.0"
        "&status=%.*s",
        mdata->consumer_key, cb_data->nonce, cb_data->timestamp,
        SOL_STR_SLICE_PRINT(mdata->token),
        (unsigned int)buf.used, (char *)buf.data);
    SOL_INT_CHECK_GOTO(r, < 0, err_params);

    sol_buffer_fini(&buf);
    r = sol_http_encode_slice(&buf,
        sol_str_slice_from_str(params));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape_params);

    r = asprintf(&signature, "POST&%.*s&%.*s",
        SOL_STR_SLICE_PRINT(mdata->escaped_post_url),
        (unsigned int)buf.used, (char *)buf.data);
    SOL_INT_CHECK_GOTO(r, < 0, err_signature);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
        signature, strlen(signature));
    SOL_NULL_CHECK_GOTO(blob, err_blob);

    r = sol_message_digest_feed(digest, blob, true);
    SOL_INT_CHECK_GOTO(r, < 0, err_feed);

    sol_blob_unref(blob);
    sol_buffer_fini(&buf);
    free(params);

    return 0;

err_feed:
    sol_blob_unref(blob);
err_blob:
    free(signature);
err_signature:
err_escape_params:
    free(params);
err_params:
    sol_message_digest_del(digest);
err_digest:
    sol_buffer_fini(&buf);
err_escape_status:
    free(cb_data->status);
err_status:
    free(cb_data->key);
err_key:
err_timestamp:
    free(cb_data->nonce);
err:
    free(cb_data);
    return r;
}

static int
token_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    uint16_t idx;
    const char *val;
    size_t token_len, token_secret_len;
    struct sol_buffer buf;
    struct sol_vector tokens;
    struct sol_str_slice *slice;
    struct twitter_data *mdata = data;

    r = sol_flow_packet_get_string(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

#define SLICE_FREE(_s) \
    do { \
        free((char *)(_s).data); \
        (_s).data = NULL; \
        (_s).len = 0; \
    } while (0)

    token_len = sizeof("oauth_token=") - 1;
    token_secret_len = sizeof("oauth_token_secret=") - 1;

    tokens = sol_util_str_split(
        sol_str_slice_from_str(val), "&", 0);
    SOL_VECTOR_FOREACH_IDX (&tokens, slice, idx) {
        struct sol_str_slice token;
        if (sol_str_slice_str_starts_with(*slice, "oauth_token_secret=")) {
            SLICE_FREE(mdata->token_secret);

            token = SOL_STR_SLICE_STR(slice->data + token_secret_len,
                slice->len - token_secret_len);
            r = sol_http_encode_slice(&buf, token);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            mdata->token_secret.data =
                sol_buffer_steal_or_copy(&buf, &mdata->token_secret.len);
            sol_buffer_fini(&buf);
        } else if (sol_str_slice_str_starts_with(*slice, "oauth_token=")) {
            SLICE_FREE(mdata->token);

            token = SOL_STR_SLICE_STR(slice->data +
                token_len, slice->len - token_len);
            r = sol_http_encode_slice(&buf, token);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            mdata->token.data =
                sol_buffer_steal_or_copy(&buf, &mdata->token.len);
            sol_buffer_fini(&buf);
        }
    }

    if (!mdata->token.len || !mdata->token_secret.len) {
        r = -EINVAL;
        goto err;
    }

    sol_vector_clear(&tokens);
    return 0;

err:
    SLICE_FREE(mdata->token);
    SLICE_FREE(mdata->token_secret);
#undef SLICE_FREE
    sol_vector_clear(&tokens);
    return r;
}

static int
timeline_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r = -EINVAL;
    struct twitter_data *mdata = data;
    struct sol_buffer buf;
    struct sol_blob *blob;
    struct callback_data *cb_data;
    struct sol_message_digest *digest;
    char *signature, *params;
    struct sol_message_digest_config *digest_config =
        &(struct sol_message_digest_config) {
        .api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION,
        .algorithm = "hmac(sha1)",
        .on_digest_ready = digest_ready_cb,
    };

    if (!(mdata->token.len && mdata->token_secret.len)) {
        sol_flow_send_error_packet(node, EINVAL,
            "There is no access token");
        return -EINVAL;
    }

    cb_data = calloc(1, sizeof(*cb_data));
    SOL_NULL_CHECK(cb_data, -ENOMEM);

    cb_data->node = node;
    cb_data->cb = timeline_request;

    cb_data->nonce = generate_nonce();
    SOL_NULL_CHECK_GOTO(cb_data->nonce, err);

    cb_data->timestamp = time(NULL);
    SOL_INT_CHECK_GOTO(cb_data->timestamp, < 0, err_timestamp);

    r = asprintf(&cb_data->key, "%.*s&%.*s",
        SOL_STR_SLICE_PRINT(mdata->consumer_secret),
        SOL_STR_SLICE_PRINT(mdata->token_secret));
    SOL_INT_CHECK_GOTO(r, < 0, err_key);

    digest_config->data = cb_data;
    digest_config->key = sol_str_slice_from_str(cb_data->key);

    digest = sol_message_digest_new(digest_config);
    SOL_NULL_CHECK_GOTO(digest, err_digest);

    r  = asprintf(&params,
        "oauth_consumer_key=%s"
        "&oauth_nonce=%s"
        "&oauth_signature_method=HMAC-SHA1"
        "&oauth_timestamp=%li"
        "&oauth_token=%.*s"
        "&oauth_version=1.0",
        mdata->consumer_key, cb_data->nonce,
        cb_data->timestamp, SOL_STR_SLICE_PRINT(mdata->token));
    SOL_INT_CHECK_GOTO(r, < 0, err_params);

    r = sol_http_encode_slice(&buf, sol_str_slice_from_str(params));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape_params);

    r = asprintf(&signature, "GET&%.*s&%.*s",
        SOL_STR_SLICE_PRINT(mdata->escaped_get_url),
        (unsigned int)buf.used, (char *)buf.data);
    sol_buffer_fini(&buf);
    SOL_INT_CHECK_GOTO(r, < 0, err_signature);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT,
        NULL, signature, strlen(signature));
    SOL_NULL_CHECK_GOTO(blob, err_blob);

    r = sol_message_digest_feed(digest, blob, true);
    SOL_INT_CHECK_GOTO(r, < 0, err_feed);

    sol_blob_unref(blob);
    free(params);

    return 0;

err_feed:
    sol_blob_unref(blob);
err_blob:
    free(signature);
err_signature:
err_escape_params:
    free(params);
err_params:
    sol_message_digest_del(digest);
err_digest:
    free(cb_data->key);
err_key:
err_timestamp:
    free(cb_data->nonce);
err:
    free(cb_data);
    return r;
}

static int
post_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *val;
    struct twitter_data *mdata = data;

    if (!(mdata->token.len && mdata->token_secret.len)) {
        sol_flow_send_error_packet(node, EINVAL,
            "There is no access token");
        return -EINVAL;
    }

    r = sol_flow_packet_get_string(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return post_status(node, val);
}

static void
twitter_close(struct sol_flow_node *node, void *data)
{
    uint16_t i;
    struct twitter_data *mdata = data;
    struct sol_http_client_connection *connection;

    free(mdata->consumer_key);
    free((char *)mdata->consumer_secret.data);
    free((char *)mdata->token.data);
    free((char *)mdata->token_secret.data);
    free((char *)mdata->escaped_post_url.data);
    free((char *)mdata->escaped_get_url.data);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_conns, connection, i)
        sol_http_client_connection_cancel(connection);
}

static int
twitter_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct twitter_data *mdata = data;
    struct sol_buffer buf;
    struct sol_flow_node_type_twitter_client_options *opts =
        (struct sol_flow_node_type_twitter_client_options *)options;

    mdata->consumer_key = strdup(opts->consumer_key);
    SOL_NULL_CHECK(mdata->consumer_key, -ENOMEM);

    r = sol_http_encode_slice(&buf,
        sol_str_slice_from_str(opts->consumer_secret));
    SOL_INT_CHECK_GOTO(r, < 0, err);
    mdata->consumer_secret.data = sol_buffer_steal_or_copy(&buf,
        &mdata->consumer_secret.len);
    sol_buffer_fini(&buf);
    SOL_NULL_CHECK_GOTO(mdata->consumer_secret.data, err);

    r = sol_http_encode_slice(&buf,
        sol_str_slice_from_str(BASE_POST_URL));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape);
    mdata->escaped_post_url.data =
        sol_buffer_steal_or_copy(&buf, &mdata->escaped_post_url.len);
    sol_buffer_fini(&buf);
    SOL_NULL_CHECK_GOTO(mdata->escaped_post_url.data, err_escape);

    r = sol_http_encode_slice(&buf,
        sol_str_slice_from_str(BASE_TIMELINE_URL));
    SOL_INT_CHECK_GOTO(r, < 0, err_escape_get);
    mdata->escaped_get_url.data =
        sol_buffer_steal_or_copy(&buf, &mdata->escaped_get_url.len);
    sol_buffer_fini(&buf);
    SOL_NULL_CHECK_GOTO(mdata->escaped_get_url.data, err_escape_get);

    sol_ptr_vector_init(&mdata->pending_conns);

    return 0;

err_escape_get:
    free((char *)mdata->escaped_post_url.data);
err_escape:
    free((char *)mdata->consumer_secret.data);
err:
    free(mdata->consumer_key);
    return -ENOMEM;
}

#include "twitter-gen.c"
