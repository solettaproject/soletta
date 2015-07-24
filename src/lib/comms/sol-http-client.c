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

#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sol-arena.h"
#include "sol-buffer.h"
#include "sol-http-client.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

static struct {
    CURLM *multi;
    struct sol_timeout *multi_perform_timeout;
    long timeout_ms;
    unsigned int fds;
    int ref;
} global = {
    .timeout_ms = 100,
    .ref = 0
};

struct connection {
    CURL *curl;
    struct sol_fd *watch;
    struct sol_arena *arena;
    struct sol_buffer buffer;

    void (*cb)(void *data, struct sol_http_response *response);
    void *data;

    bool error;
    bool pending_error_cb;
};

static void
call_connection_finish_cb(struct connection *connection)
{
    struct sol_http_response response = {
        .api_version = SOL_HTTP_RESPONSE_API_VERSION,
        .content = connection->buffer
    };
    struct sol_http_response *param;
    CURLcode r;
    char *tmp;

    if (!connection || !connection->cb)
        return;

    if (connection->error) {
        param = NULL;
        goto out;
    } else {
        param = &response;
    }

    r = curl_easy_getinfo(connection->curl, CURLINFO_CONTENT_TYPE, &tmp);
    if (r == CURLE_OK) {
        response.content_type = tmp ? strdupa(tmp) : "application/octet-stream";
        r = curl_easy_getinfo(connection->curl, CURLINFO_EFFECTIVE_URL, &tmp);
    }
    if (r == CURLE_OK && tmp) {
        long response_code;

        response.url = strdupa(tmp);
        r = curl_easy_getinfo(connection->curl, CURLINFO_RESPONSE_CODE,
            &response_code);
        if (r == CURLE_OK)
            response.response_code = (int)response_code;
    }
    if (r != CURLE_OK)
        param = NULL;

out:
    connection->cb(connection->data, param);
    connection->cb = NULL; /* Don't call again. */
}

static size_t
write_cb(char *data, size_t size, size_t nmemb, void *connp)
{
    struct connection *connection = connp;
    size_t data_size;
    int r;

    r = sol_util_size_mul(size, nmemb, &data_size);
    if (r < 0)
        return 0;

    r = sol_buffer_append_slice(&connection->buffer,
        SOL_STR_SLICE_STR(data, data_size));
    if (r < 0)
        return r;

    return data_size;
}

static void
pump_multi_info_queue(void)
{
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(global.multi, &msgs_left))) {
        struct connection *conn;

        if (msg->msg != CURLMSG_DONE)
            continue;

        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &conn);
        call_connection_finish_cb(conn);
    }
}

static bool
multi_perform_cb(void *data SOL_ATTR_UNUSED)
{
    int running;

    if (!global.multi)
        goto out;

    pump_multi_info_queue();

    if (curl_multi_perform(global.multi, &running) == CURLM_OK) {
        if (running > 0)
            return true;
    }

out:
    global.multi_perform_timeout = NULL;
    return false;
}

static bool
error_cb(void *data)
{
    struct connection *connection = data;

    call_connection_finish_cb(connection);

    curl_multi_remove_handle(global.multi, connection->curl);
    curl_easy_cleanup(connection->curl);

    sol_buffer_fini(&connection->buffer);
    sol_arena_del(connection->arena);

    /* FIXME: This should happen but GLib doesn't like this. */
    /* sol_fd_del(connection->watch); */

    free(connection);
    return false;
}

static bool
connection_watch_cb(void *data SOL_ATTR_UNUSED, int fd, unsigned int flags)
{
    struct connection *connection = data;
    int action = 0;

    if (flags & SOL_FD_FLAGS_IN)
        action |= CURL_CSELECT_IN;
    if (flags & SOL_FD_FLAGS_OUT)
        action |= CURL_CSELECT_OUT;
    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP))
        action |= CURL_CSELECT_ERR;

    if (action & CURL_CSELECT_ERR || connection->error) {
        connection->error = flags & (SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR);

        /* Cleanup is performed in an idler to avoid race conditions */
        if (!connection->pending_error_cb) {
            connection->pending_error_cb = true;
            if (!sol_idle_add(error_cb, connection))
                SOL_WRN("Could not create error idler, this may leak");
        }

        return false;
    }

    if (action) {
        int running;
        curl_multi_socket_action(global.multi, fd, action, &running);
    }

    return true;
}

static curl_socket_t
open_socket_cb(void *clientp, curlsocktype purpose, struct curl_sockaddr *addr)
{
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    struct connection *connection = clientp;
    int fd;

    if (purpose != CURLSOCKTYPE_IPCXN) {
        errno = -EINVAL;
        return -1;
    }

    fd = socket(addr->family, addr->socktype | SOCK_CLOEXEC, addr->protocol);
    if (fd < 0)
        return -1;

    connection->watch = sol_fd_add(fd, fd_flags, connection_watch_cb,
        connection);
    if (!connection->watch) {
        close(fd);
        return -1;
    }

    global.fds++;
    return fd;
}

static int
xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal SOL_ATTR_UNUSED, curl_off_t ulnow SOL_ATTR_UNUSED)
{
    struct connection *connection = clientp;

    if (dltotal > 0 && unlikely(dltotal < dlnow)) {
        SOL_WRN("Received more than expected, aborting transfer (%ld < %ld)",
            dltotal, dlnow);
        connection->error = true;
        return 1;
    }

    if (dltotal && dltotal == dlnow)
        call_connection_finish_cb(connection);

    return 0;
}

static bool
perform_multi(CURL *curl, struct sol_arena *arena,
    void (*cb)(void *data, struct sol_http_response *response),
    void *data)
{
    struct connection *connection;
    int running;

    SOL_INT_CHECK(global.ref, <= 0, false);
    SOL_NULL_CHECK(curl, false);

    connection = calloc(1, sizeof(*connection));
    SOL_NULL_CHECK(connection, false);

    connection->arena = arena;
    connection->curl = curl;
    connection->cb = cb;
    connection->data = data;
    connection->error = false;

    sol_buffer_init(&connection->buffer);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, connection);

    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, open_socket_cb);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, connection);

    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, connection);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    curl_easy_setopt(curl, CURLOPT_PRIVATE, connection);

    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);

    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
        CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
        CURLPROTO_HTTP | CURLPROTO_HTTPS);

    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    if (curl_multi_add_handle(global.multi, connection->curl) != CURLM_OK) {
        free(connection);
        return false;
    }

    if (global.multi_perform_timeout)
        return true;

    /* Apparently this is required to kick off cURL's internal main loop. */
    curl_multi_socket_action(global.multi, CURL_SOCKET_TIMEOUT, 0, &running);

    /* This timeout will be recreated if cURL changes the timeout value. */
    global.multi_perform_timeout = sol_timeout_add(global.timeout_ms,
        multi_perform_cb, NULL);
    if (!global.multi_perform_timeout) {
        curl_multi_remove_handle(global.multi, connection->curl);
        free(connection);
        return false;
    }

    return true;
}

static char *
encode_key_values(CURL *curl, enum sol_http_param_type type,
    const struct sol_http_param *params, char *initial_value)
{
    struct sol_http_param_value *iter;
    uint16_t idx;
    bool first = true;

    if (type != SOL_HTTP_PARAM_QUERY_PARAM &&
        type != SOL_HTTP_PARAM_POST_FIELD &&
        type != SOL_HTTP_PARAM_COOKIE) {
        errno = EINVAL;
        return NULL;
    }

    SOL_VECTOR_FOREACH_IDX (&params->params, iter, idx) {
        const char *key = iter->value.key_value.key;
        const char *value = iter->value.key_value.value;
        char *tmp, *encoded_key, *encoded_value;
        int r;

        if (iter->type != type)
            continue;

        encoded_key = curl_easy_escape(curl, key, strlen(key));
        if (!encoded_key)
            goto cleanup;
        encoded_value = curl_easy_escape(curl, value, strlen(value));
        if (!encoded_value) {
            curl_free(encoded_key);
            goto cleanup;
        }

        if (type == SOL_HTTP_PARAM_COOKIE) {
            r = asprintf(&tmp, "%s%s%s=%s;", initial_value, first ? "" : " ",
                encoded_key, encoded_value);
        } else {
            r = asprintf(&tmp, "%s%s%s=%s", initial_value, first ? "" : "&",
                encoded_key, encoded_value);
        }

        curl_free(encoded_key);
        curl_free(encoded_value);

        if (r < 0)
            goto cleanup;

        free(initial_value);
        initial_value = tmp;
        first = false;
    }

    return initial_value;

cleanup:
    free(initial_value);
    errno = ENOMEM;
    return NULL;
}

static char *
build_uri(CURL *curl, const char *base, const struct sol_http_param *params)
{
    char *initial_value;
    char *built_uri;

    if (asprintf(&initial_value, "%s?", base) < 0) {
        errno = ENOMEM;
        return NULL;
    }

    built_uri = encode_key_values(curl, SOL_HTTP_PARAM_QUERY_PARAM, params,
        initial_value);
    if (built_uri == initial_value) {
        free(initial_value);
        return strdup(base);
    }

    return built_uri;
}

static char *
build_cookies(CURL *curl, const struct sol_http_param *params)
{
    return encode_key_values(curl, SOL_HTTP_PARAM_COOKIE, params, strdup(""));
}

static char *
build_post_fields(CURL *curl, const struct sol_http_param *params)
{
    return encode_key_values(curl, SOL_HTTP_PARAM_POST_FIELD, params,
        strdup(""));
}

static bool
set_headers_from_params(CURL *curl, struct sol_arena *arena,
    const struct sol_http_param *params, struct curl_slist **headers)
{
    struct sol_http_param_value *iter;
    struct curl_slist *list = NULL;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (&params->params, iter, idx) {
        const char *key = iter->value.key_value.key;
        const char *value = iter->value.key_value.value;
        struct curl_slist *tmp_list;
        char key_colon_value[512];
        char *tmp;
        int r;

        if (iter->type != SOL_HTTP_PARAM_HEADER)
            continue;

        r = snprintf(key_colon_value, sizeof(key_colon_value),
            "%s: %s", key, value);
        if (r < 0 || r >= (int)sizeof(key_colon_value))
            goto fail;

        tmp = sol_arena_strdup(arena, key_colon_value);
        if (!tmp)
            goto fail;

        tmp_list = curl_slist_append(list, tmp);
        if (!tmp_list)
            goto fail;
        list = tmp_list;
    }

    if (list) {
        if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list) != CURLE_OK)
            goto fail;
    }

    *headers = list;
    return true;

fail:
    curl_slist_free_all(list);
    return false;
}

static bool
set_auth_basic(CURL *curl, struct sol_arena *arena,
    const struct sol_http_param_value *value)
{
    char *user = sol_arena_strdup(arena, value->value.auth.user);
    char *password = sol_arena_strdup(arena, value->value.auth.password);

    if (!user || !password)
        return false;
    if (curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC | CURLAUTH_ONLY) != CURLE_OK)
        return false;
    if (curl_easy_setopt(curl, CURLOPT_USERNAME, user) != CURLE_OK)
        return false;
    if (curl_easy_setopt(curl, CURLOPT_PASSWORD, password) != CURLE_OK)
        return false;
    return true;
}

static bool
set_allow_redir(CURL *curl, bool setting)
{
    return curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long)setting) == CURLE_OK;
}

static bool
set_timeout(CURL *curl, long setting)
{
    return curl_easy_setopt(curl, CURLOPT_TIMEOUT, setting) == CURLE_OK;
}

static bool
set_verbose(CURL *curl, bool setting)
{
    return curl_easy_setopt(curl, CURLOPT_VERBOSE, setting ? 1L : 0L) == CURLE_OK;
}

static bool
set_string_option(CURL *curl, CURLoption option, struct sol_arena *arena,
    char *value)
{
    char *tmp;

    if (!value)
        return false;
    if (!*value) {
        free(value);
        return false;
    }

    tmp = sol_arena_strdup(arena, value);
    free(value);
    if (tmp && *tmp)
        return curl_easy_setopt(curl, option, tmp) == CURLE_OK;

    return true;
}

static bool
set_cookies_from_params(CURL *curl, struct sol_arena *arena,
    const struct sol_http_param *params)
{
    char *cookies = build_cookies(curl, params);

    if (cookies && !*cookies) {
        free(cookies);
        return true;
    }
    return set_string_option(curl, CURLOPT_COOKIE, arena, cookies);
}

static bool
set_uri_from_params(CURL *curl, struct sol_arena *arena, const char *base,
    const struct sol_http_param *params)
{
    char *full_uri = build_uri(curl, base, params);

    return set_string_option(curl, CURLOPT_URL, arena, full_uri);
}

static bool
set_post_fields_from_params(CURL *curl, struct sol_arena *arena,
    const struct sol_http_param *params)
{
    char *post = build_post_fields(curl, params);

    return set_string_option(curl, CURLOPT_POSTFIELDS, arena, post);
}

static bool
check_param_api_version(const struct sol_http_param *params)
{
    struct sol_http_param_value *value;
    uint16_t idx;

    if (unlikely(params->api_version != SOL_HTTP_PARAM_API_VERSION)) {
        SOL_ERR("Parameter has an invalid API version. Expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }

    SOL_VECTOR_FOREACH_IDX (&params->params, value, idx) {
        if (unlikely(value->api_version != SOL_HTTP_PARAM_API_VERSION)) {
            SOL_ERR("Parameter has an invalid API version. Expected %u, got %u",
                SOL_HTTP_PARAM_API_VERSION, value->api_version);
            return false;
        }
    }

    return true;
}

SOL_API int
sol_http_client_request(enum sol_http_method method,
    const char *base_uri, const struct sol_http_param *params,
    void (*cb)(void *data, struct sol_http_response *response),
    void *data)
{
    static CURLoption sol_to_curl_method[] = {
        [SOL_HTTP_METHOD_GET] = CURLOPT_HTTPGET,
        [SOL_HTTP_METHOD_POST] = CURLOPT_HTTPPOST,
        [SOL_HTTP_METHOD_HEAD] = CURLOPT_NOBODY,
    };
    struct sol_http_param_value *value;
    struct sol_arena *arena;
    struct curl_slist *headers = NULL;
    CURL *curl;
    uint16_t idx;

    if (!streqn(base_uri, "http://", sizeof("http://") - 1)
        && !streqn(base_uri, "https://", sizeof("https://") - 1)) {
        SOL_WRN("Invalid protocol for URI: %s", base_uri);
        return -EINVAL;
    }

    if (!check_param_api_version(params)) {
        SOL_WRN("Parameter API version mismatch");
        return -EINVAL;
    }

    arena = sol_arena_new();
    if (!arena) {
        SOL_WRN("Could not create arena");
        return -ENOMEM;
    }

    curl = curl_easy_init();
    if (!curl) {
        SOL_WRN("Could not create cURL handle");
        goto no_curl_easy;
    }

    if (curl_easy_setopt(curl, sol_to_curl_method[method], 1L) != CURLE_OK) {
        SOL_WRN("Could not set HTTP method");
        goto invalid_option;
    }

    if (!set_uri_from_params(curl, arena, base_uri, params)) {
        SOL_WRN("Could not set URI from params");
        goto invalid_option;
    }

    if (!set_cookies_from_params(curl, arena, params)) {
        SOL_WRN("Could not set cookies from params");
        goto invalid_option;
    }

    if (!set_headers_from_params(curl, arena, params, &headers)) {
        SOL_WRN("Could not set custom headers from params");
        goto invalid_option;
    }

    if (method == SOL_HTTP_METHOD_POST) {
        if (!set_post_fields_from_params(curl, arena, params)) {
            SOL_WRN("Could not set POST fields from params");
            goto invalid_option;
        }
    }

    SOL_VECTOR_FOREACH_IDX (&params->params, value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_POST_FIELD:
            /* already handled by set_post_fields_from_params() */
            continue;
        case SOL_HTTP_PARAM_QUERY_PARAM:
            /* already handled by set_uri_from_params() */
            continue;
        case SOL_HTTP_PARAM_COOKIE:
            /* already handled by set_cookies_from_params() */
            continue;
        case SOL_HTTP_PARAM_HEADER:
            /* already handled by set_header_from_params() */
            continue;
        case SOL_HTTP_PARAM_AUTH_BASIC:
            if (!set_auth_basic(curl, arena, value))
                goto invalid_option;
            continue;
        case SOL_HTTP_PARAM_ALLOW_REDIR:
            if (!set_allow_redir(curl, value->value.boolean.value))
                goto invalid_option;
            continue;
        case SOL_HTTP_PARAM_TIMEOUT:
            if (!set_timeout(curl, value->value.integer.value))
                goto invalid_option;
            continue;
        case SOL_HTTP_PARAM_VERBOSE:
            if (!set_verbose(curl, value->value.boolean.value))
                goto invalid_option;
            continue;
        }
    }

    if (perform_multi(curl, arena, cb, data))
        return 0;

invalid_option:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
no_curl_easy:
    sol_arena_del(arena);
    return -EINVAL;
}

static int
timer_cb(CURLM *multi SOL_ATTR_UNUSED, long timeout_ms,
    void *userp SOL_ATTR_UNUSED)
{
    if (global.timeout_ms == timeout_ms)
        return 0;

    if (timeout_ms > 0) {
        if (timeout_ms < 100)
            timeout_ms = 100;

        /* cURL requested a timeout value change. */
        global.timeout_ms = timeout_ms;

        if (global.multi_perform_timeout) {
            /* Change sol_timeout if there's already one in place. */
            sol_timeout_del(global.multi_perform_timeout);
            global.multi_perform_timeout = sol_timeout_add(global.timeout_ms,
                multi_perform_cb, NULL);

            return global.multi_perform_timeout ? 0 : -1;
        }
    } else if (!timeout_ms) {
        /* Timer expired; pump cURL immediately. */
        return multi_perform_cb(NULL) ? 0 : -1;
    }

    return 0;
}

SOL_API bool
sol_http_init(void)
{
    if (global.ref) {
        global.ref++;
        return true;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    global.multi = curl_multi_init();
    SOL_NULL_CHECK(global.multi, false);

    curl_multi_setopt(global.multi, CURLMOPT_TIMERFUNCTION, timer_cb);

    global.multi_perform_timeout = NULL;

    global.ref++;

    return true;
}

static bool
cleanup_multi_cb(void *data)
{
    CURLM *multi = data;

    if (global.fds)
        return true;

    curl_multi_cleanup(multi);
    curl_global_cleanup();
    return false;
}

SOL_API bool
sol_http_shutdown(void)
{
    if (!global.ref)
        return false;
    global.ref--;
    if (global.ref)
        return true;

    if (global.multi_perform_timeout) {
        sol_timeout_del(global.multi_perform_timeout);
        global.multi_perform_timeout = NULL;
    }

    /* Cleanup in an idler as there might be easy handles in the flight. */
    if (!sol_idle_add(cleanup_multi_cb, global.multi)) {
        SOL_WRN("Could defer cURL cleanup");
        return false;
    }
    global.multi = NULL;

    return true;
}

SOL_API bool
sol_http_param_add(struct sol_http_param *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;

    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }

    ptr = sol_vector_append(&params->params);
    if (!ptr) {
        SOL_WRN("Could not append option to parameter vector");
        return false;
    }

    memcpy(ptr, &value, sizeof(value));
    return true;
}

SOL_API void
sol_http_param_free(struct sol_http_param *params)
{
    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return;
    }
    sol_vector_clear(&params->params);
}
