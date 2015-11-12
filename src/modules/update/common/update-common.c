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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "auto-update");

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "update-common.h"

#include "sol-json.h"
#include "sol-http-client.h"
#include "sol-lib-loader.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-update-modules.h"
#include "sol-util.h"

void
delete_handle(struct sol_update_handle *handle)
{
    free(handle->hash_algorithm);
    free(handle->hash);
    free(handle->url);
    free(handle->file_path);
    if (handle->timeout)
        sol_timeout_del(handle->timeout);
    if (handle->conn)
        sol_http_client_connection_cancel(handle->conn);
    free(handle);
}

static void
free_response_members(struct sol_update_info *info)
{
    free((void *)info->version);
    free((void *)info->url);
    free((void *)info->hash);
    free((void *)info->hash_algorithm);
}

static void
task_check_response(void *data, const struct sol_http_client_connection *conn,
    struct sol_http_response *http_response)
{
    struct sol_update_handle *handle = data;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    struct sol_update_info response = { };

    handle->conn = NULL;

    if (http_response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Invalid response code from [%s] when checking for update: %d",
            handle->url, http_response->response_code);
        handle->cb_check((void *)handle->user_data, -http_response->response_code, NULL);
        delete_handle(handle);
        return;
    }

    if (!streq(http_response->content_type, "application/json")) {
        SOL_WRN("Invalid content type of response: [%s] expected [application/json]",
            http_response->content_type);
        handle->cb_check((void *)handle->user_data, -EINVAL, NULL);
        delete_handle(handle);
        return;
    }

    if (!http_response->content.used) {
        SOL_WRN("Empty response for version check");
        handle->cb_check((void *)handle->user_data, -EINVAL, NULL);
        delete_handle(handle);
        return;
    }

    sol_json_scanner_init(&scanner, http_response->content.data, http_response->content.used);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "url"))
            response.url = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "version"))
            response.version = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "hash"))
            response.hash = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "hash-algorithm"))
            response.hash_algorithm = sol_json_token_get_unescaped_string_copy(&value);
        else
            SOL_WRN("Unknown response member: %.*s",
                SOL_STR_SLICE_PRINT(sol_json_token_to_slice(&token)));
    }

    /* If some member is empty, give up */
    if (!(response.url && response.version
        && response.hash && response.hash_algorithm)) {
        SOL_WRN("Malformed check response");
        handle->cb_check((void *)handle->user_data, -EINVAL, NULL);
        free_response_members(&response);
        delete_handle(handle);
        return;
    }

    handle->cb_check((void *)handle->user_data, 0, &response);
    free_response_members(&response);
    delete_handle(handle);

    return;
}

static void
on_digest_ready_cb(void *data, struct sol_message_digest *md, struct sol_blob *output)
{
    struct sol_update_handle *handle = data;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice slice = sol_str_slice_from_blob(output);
    int r = 0;

    r = sol_buffer_append_as_base16(&buffer, slice, false);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if (buffer.used != strlen(handle->hash) ||
        strncmp(buffer.data, handle->hash, buffer.used != 0)) {

        r = -EINVAL;
        SOL_WRN("Expected hash differs of download file hash, expected [%s], found [%.*s]",
            handle->hash, (int)buffer.used, (char *)buffer.data);
    }

end:
    handle->cb_hash(handle, r);
    sol_message_digest_del(md);
    sol_buffer_fini(&buffer);
}

static void
on_feed_done_cb(void *data, struct sol_message_digest *md, struct sol_blob *input)
{
    struct sol_update_handle *handle = data;
    char buf[CHUNK_SIZE], *blob_backend = NULL;
    struct sol_blob *blob = NULL;
    size_t size;
    bool last;
    int r;

    size = fread(buf, 1, sizeof(buf), handle->file);
    if (ferror(handle->file)) {
        SOL_WRN("Could not read file for feed hash algorithm");
        goto err;
    }

    last = feof(handle->file);

    /* TODO Maybe this is a bug on sol_message_digest? Keeps calling on_feed_done
     * after send last chunk */
    if (!size && last) {
        SOL_WRN("Nothing more to feed hash algorithm, ignoring on_feed_done request");
        return;
    }

    blob_backend = malloc(size);
    SOL_NULL_CHECK_GOTO(blob_backend, err);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, blob_backend, size);
    SOL_NULL_CHECK_GOTO(blob, err);

    memcpy(blob_backend, buf, size);

    r = sol_message_digest_feed(md, blob, last);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    sol_blob_unref(blob);
    return;

err:
    SOL_WRN("Could not feed data to check update file hash");
    free(blob_backend);
    sol_blob_unref(blob);
    sol_message_digest_del(md);
    handle->cb_hash(handle, -EINVAL);
}

static bool
check_hash(struct sol_update_handle *handle,
    void (*cb)(struct sol_update_handle *handle, int status))
{
    struct sol_message_digest_config cfg = {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = handle->hash_algorithm,
        .on_digest_ready = on_digest_ready_cb,
        .on_feed_done = on_feed_done_cb,
        .data = handle
    };
    struct sol_message_digest *md;

    md = sol_message_digest_new(&cfg);
    SOL_NULL_CHECK(md, false);

    rewind(handle->file);
    handle->cb_hash = cb;

    /* Start feeding */
    on_feed_done_cb(handle, md, NULL);

    return true;
}

static void
check_hash_cb(struct sol_update_handle *handle, int status)
{
    char *real_path;

    if (status < 0) {
        SOL_WRN("Invalid hash of update file");
        handle->cb_fetch((void *)handle->user_data, -EINVAL, NULL);
        goto end;
    }

    real_path = realpath(SOL_UPDATE_FILE_NAME, NULL);
    handle->cb_fetch((void *)handle->user_data, real_path ? 0 : -ENOMEM, real_path);
    free(real_path);

end:
    fclose(handle->file);
    handle->file = NULL;
    delete_handle(handle);
}

static void
task_fetch_response(void *data, const struct sol_http_client_connection *conn,
    struct sol_http_response *http_response)
{
    struct sol_update_handle *handle = data;
    FILE *f;

    handle->conn = NULL;

    if (http_response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Invalid response code from [%s] when fetching update: %d",
            handle->url, http_response->response_code);
        handle->cb_fetch((void *)handle->user_data, -http_response->response_code, NULL);
        delete_handle(handle);
        return;
    }

    if (!http_response->content.used) {
        SOL_WRN("Empty response for fetch update");
        handle->cb_fetch((void *)handle->user_data, -EINVAL, NULL);
        delete_handle(handle);
        return;
    }

    f = fopen(SOL_UPDATE_FILE_NAME, "w+e");
    if (!f) {
        SOL_WRN("Could not open file for writing: %s", sol_util_strerrora(errno));
        handle->cb_fetch((void *)handle->user_data, -errno, NULL);
        delete_handle(handle);
        return;
    }

    fwrite(http_response->content.data, http_response->content.used, 1, f);
    fflush(f);
    if (ferror(f)) {
        SOL_WRN("Could not write to file");
        handle->cb_fetch((void *)handle->user_data, -EIO, NULL);
        delete_handle(handle);
        goto err;
    }

    handle->file = f;
    if (!check_hash(handle, check_hash_cb)) {
        SOL_WRN("Could not check hash of update file");
        handle->cb_fetch((void *)handle->user_data, -EINVAL, NULL);
        goto err;
    }

    return;

err:
    fclose(f);
}

struct sol_update_handle *
common_check(const char *url,
    void (*cb)(void *data, int status, const struct sol_update_info *response),
    const void *data)
{
    struct sol_http_params params;
    struct sol_update_handle *handle;

    SOL_DBG("Check");

    SOL_NULL_CHECK(url, NULL);
    SOL_NULL_CHECK(cb, NULL);

    handle = calloc(1, sizeof(struct sol_update_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->task = TASK_CHECK;
    handle->cb_check = cb;
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
        task_check_response, handle);
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

struct sol_update_handle *
common_fetch(const struct sol_update_info *info,
    void (*cb)(void *data, int status, const char *file_path),
    const void *data, bool resume)
{
    struct sol_update_handle *handle;

    /* TODO handle resume stuff */

    SOL_NULL_CHECK(info, NULL);
    SOL_NULL_CHECK(info->url, NULL);
    SOL_NULL_CHECK(info->hash, NULL);
    SOL_NULL_CHECK(info->hash_algorithm, NULL);
    SOL_NULL_CHECK(cb, NULL);
    SOL_DBG("Fetch %s", info->url);

    handle = calloc(1, sizeof(struct sol_update_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->task = TASK_FETCH;
    handle->cb_fetch = cb;
    handle->user_data = data;
    handle->url = strdup(info->url);
    SOL_NULL_CHECK_GOTO(handle->url, err_url);
    handle->hash = strdup(info->hash);
    SOL_NULL_CHECK_GOTO(handle->hash, err_hash);
    handle->hash_algorithm = strdup(info->hash_algorithm);
    SOL_NULL_CHECK_GOTO(handle->hash_algorithm, err_hash_algorithm);

    handle->conn = sol_http_client_request(SOL_HTTP_METHOD_GET, handle->url, NULL,
        task_fetch_response, handle);
    SOL_NULL_CHECK_GOTO(handle->conn, err_conn);

    return handle;

err_conn:
    free(handle->hash_algorithm);
err_hash_algorithm:
    free(handle->hash);
err_hash:
    free(handle->url);
err_url:
    free(handle);

    return NULL;
}

bool
common_cancel(struct sol_update_handle *handle)
{
    SOL_NULL_CHECK(handle, false);

    if (handle->task != TASK_UPDATE) {
        delete_handle(handle);
        return true;
    }

    return false;
}

int
common_get_progress(struct sol_update_handle *handle)
{
    SOL_DBG("Progress");
    return 0;
}

int
common_move_file(const char *old_path, const char *new_path)
{
    FILE *new, *old;
    char buf[CHUNK_SIZE];
    size_t size, w_size;
    int r;

    /* First, try to rename */
    r = rename(old_path, new_path);
    SOL_INT_CHECK(r, == 0, r);

    /* If failed, try the hard way */
    errno = 0;

    old = fopen(old_path, "re");
    SOL_NULL_CHECK(old, -errno);

    new = fopen(new_path, "we");
    r = -errno;
    SOL_NULL_CHECK_GOTO(new, err_new);

    while ((size = fread(buf, 1, sizeof(buf), old))) {
        w_size = fwrite(buf, 1, size, new);
        if (w_size != size) {
            r = -EIO;
            goto err;
        }
    }
    if (ferror(old)) {
        r = -EIO;
        goto err;
    }

    if (fflush(new) != 0) {
        r = -errno;
        goto err;
    }
    fclose(new);

    /* Remove old file */
    fclose(old);
    unlink(old_path);

    return 0;

err:
    fclose(new);
    unlink(new_path);
err_new:
    fclose(old);

    /* So errno is not influenced by fclose and unlink above*/
    errno = -r;

    return r;
}
