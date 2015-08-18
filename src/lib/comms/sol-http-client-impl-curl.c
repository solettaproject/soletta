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

int sol_http_client_init(void);
void sol_http_client_shutdown(void);

static struct {
    CURLM *multi;
    struct sol_timeout *multi_perform_timeout;
    struct sol_ptr_vector connections;
    long timeout_ms;
    int ref;
} global = {
    .timeout_ms = 100,
    .ref = 0,
    .connections = SOL_PTR_VECTOR_INIT,
};

struct sol_http_client_pending {
    CURL *curl;
    struct sol_fd *watch;
    struct sol_idle *idle;
    struct sol_arena *arena;
    struct curl_slist *headers;
    struct sol_buffer buffer;

    void (*cb)(void *data, struct sol_http_response *response);
    const void *data;

    bool error;
    bool pending_error_cb;
};

static void
destroy_connection(struct sol_http_client_pending *c)
{
    curl_multi_remove_handle(global.multi, c->curl);
    curl_slist_free_all(c->headers);
    curl_easy_cleanup(c->curl);

    sol_buffer_fini(&c->buffer);
    sol_arena_del(c->arena);

    if (c->watch)
        sol_fd_del(c->watch);
    if (c->idle)
        sol_idle_del(c->idle);

    free(c);
}

void
sol_http_client_shutdown(void)
{
    struct sol_http_client_pending *c;
    uint16_t i;

    if (!global.ref)
        return;
    global.ref--;
    if (global.ref)
        return;

    if (global.multi_perform_timeout) {
        sol_timeout_del(global.multi_perform_timeout);
        global.multi_perform_timeout = NULL;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&global.connections, c, i) {
        destroy_connection(c);
    }

    sol_ptr_vector_clear(&global.connections);

    curl_multi_cleanup(global.multi);
    curl_global_cleanup();

    global.multi = NULL;
}

static void
call_connection_finish_cb(struct sol_http_client_pending *connection)
{
    struct sol_http_response *param;
    CURLcode r;
    long response_code;
    char *tmp;

    if (!connection->cb)
        return;

    if (connection->error) {
        param = NULL;
        goto out;
    }

    param = &(struct sol_http_response) {
        .api_version = SOL_HTTP_RESPONSE_API_VERSION,
        .content = connection->buffer
    };

    r = curl_easy_getinfo(connection->curl, CURLINFO_CONTENT_TYPE, &tmp);
    if (r != CURLE_OK) {
        param = NULL;
        goto out;
    }
    param->content_type = tmp ? strdupa(tmp) : "application/octet-stream";

    r = curl_easy_getinfo(connection->curl, CURLINFO_EFFECTIVE_URL, &tmp);
    if (r != CURLE_OK || !tmp) {
        param = NULL;
        goto out;
    }
    param->url = strdupa(tmp);

    r = curl_easy_getinfo(connection->curl, CURLINFO_RESPONSE_CODE,
        &response_code);
    if (r != CURLE_OK) {
        param = NULL;
        goto out;
    }
    param->response_code = (int)response_code;

out:
    connection->cb((void *)connection->data, param);
    connection->cb = NULL; /* Don't call again. */
}

static size_t
write_cb(char *data, size_t size, size_t nmemb, void *connp)
{
    struct sol_http_client_pending *connection = connp;
    size_t data_size;
    int r;

    r = sol_util_size_mul(size, nmemb, &data_size);
    SOL_INT_CHECK(r, < 0, 0);

    r = sol_buffer_append_slice(&connection->buffer,
        SOL_STR_SLICE_STR(data, data_size));
    SOL_INT_CHECK(r, < 0, 0);

    return data_size;
}

static void
pump_multi_info_queue(void)
{
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(global.multi, &msgs_left))) {
        char *priv;
        struct sol_http_client_pending *conn;
        CURLcode r;

        if (msg->msg != CURLMSG_DONE)
            continue;

        r = curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv);
        if (r == CURLE_OK && priv) {
            /* CURLINFO_PRIVATE is defined as a string and CURL_DISABLE_TYPECHECK
             * will barf about us handling a different pointer.
             */
            conn = (struct sol_http_client_pending *)priv;
            call_connection_finish_cb(conn);
        } else {
            SOL_ERR("Could not obtain private connection data from cURL. Bug?");
        }
    }
}

static bool
multi_perform_cb(void *data)
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

static int
timer_cb(CURLM *multi, long timeout_ms, void *userp)
{
    if (timeout_ms > 0) {
        if (timeout_ms < 100)
            timeout_ms = 100;
        if (global.timeout_ms == timeout_ms)
            return 0;

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

int
sol_http_client_init(void)
{
    if (global.ref) {
        global.ref++;
        return 0;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    global.multi = curl_multi_init();
    SOL_NULL_CHECK_GOTO(global.multi, cleanup);

    curl_multi_setopt(global.multi, CURLMOPT_TIMERFUNCTION, timer_cb);

    global.multi_perform_timeout = NULL;

    global.ref++;

    return 0;

cleanup:
    curl_global_cleanup();
    return -EINVAL;
}

static bool
error_cb(void *data)
{
    struct sol_http_client_pending *connection = data;

    call_connection_finish_cb(connection);

    sol_ptr_vector_remove(&global.connections, connection);

    destroy_connection(connection);

    return false;
}

static bool
connection_watch_cb(void *data, int fd, unsigned int flags)
{
    struct sol_http_client_pending *connection = data;
    int action = 0;

    if (flags & SOL_FD_FLAGS_IN)
        action |= CURL_CSELECT_IN;
    if (flags & SOL_FD_FLAGS_OUT)
        action |= CURL_CSELECT_OUT;
    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP))
        action |= CURL_CSELECT_ERR;

    if (action & CURL_CSELECT_ERR || connection->error) {
        connection->watch = NULL;
        connection->error = flags & (SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR);

        /* Cleanup is performed in an idler to avoid race conditions */
        if (!connection->pending_error_cb) {
            connection->pending_error_cb = true;
            connection->idle = sol_idle_add(error_cb, connection);
            if (!connection->idle)
                SOL_WRN("Could not create error idler, this may leak");
        }
    }

    if (action) {
        int running;
        curl_multi_socket_action(global.multi, fd, action, &running);
    }

    return !(action & CURL_CSELECT_ERR);
}

static void
print_connection_info_wrn(struct sol_http_client_pending *connection)
{
    const char *tmp_str;
    long tmp_long;

    if (curl_easy_getinfo(connection->curl, CURLINFO_EFFECTIVE_URL, &tmp_str) == CURLE_OK)
        SOL_WRN("  Effective URL: %s", tmp_str);
    if (curl_easy_getinfo(connection->curl, CURLINFO_RESPONSE_CODE, &tmp_long) == CURLE_OK)
        SOL_WRN("  Response code: %ld", tmp_long);
}

static curl_socket_t
open_socket_cb(void *clientp, curlsocktype purpose, struct curl_sockaddr *addr)
{
    /* FIXME: Should the easy handle be removed from multi on failure? */
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    struct sol_http_client_pending *connection = clientp;
    int fd;

    if (purpose != CURLSOCKTYPE_IPCXN) {
        errno = -EINVAL;
        return -1;
    }

    fd = socket(addr->family, addr->socktype | SOCK_CLOEXEC, addr->protocol);
    if (fd < 0) {
        SOL_WRN("Could not create socket (family %d, type %d, protocol %d)",
            addr->family, addr->socktype, addr->protocol);
        print_connection_info_wrn(connection);
        return -1;
    }

    connection->watch = sol_fd_add(fd, fd_flags, connection_watch_cb,
        connection);
    if (!connection->watch) {
        close(fd);
        return -1;
    }

    return fd;
}

static int
xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow)
{
    struct sol_http_client_pending *connection = clientp;

    if (dltotal > 0 && unlikely(dltotal < dlnow)) {
        SOL_WRN("Received more than expected, aborting transfer ("
            CURL_FORMAT_OFF_T "< " CURL_FORMAT_OFF_T ")",
            dltotal, dlnow);
        print_connection_info_wrn(connection);
        connection->error = true;
        return 1;
    }

    if (dltotal && dltotal == dlnow)
        call_connection_finish_cb(connection);

    return 0;
}

static struct sol_http_client_pending *
perform_multi(CURL *curl, struct sol_arena *arena, struct curl_slist *headers,
    void (*cb)(void *data, struct sol_http_response *response),
    const void *data)
{
    struct sol_http_client_pending *connection;
    int running;

    SOL_INT_CHECK(global.ref, <= 0, NULL);
    SOL_NULL_CHECK(curl, NULL);

    connection = calloc(1, sizeof(*connection));
    SOL_NULL_CHECK(connection, NULL);

    connection->arena = arena;
    connection->headers = headers;
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

    if (curl_multi_add_handle(global.multi, connection->curl) != CURLM_OK)
        goto free_buffer;

    if (global.multi_perform_timeout)
        return connection;

    /* Apparently this is required to kick off cURL's internal main loop. */
    curl_multi_socket_action(global.multi, CURL_SOCKET_TIMEOUT, 0, &running);

    /* This timeout will be recreated if cURL changes the timeout value. */
    global.multi_perform_timeout = sol_timeout_add(global.timeout_ms,
        multi_perform_cb, NULL);
    if (!global.multi_perform_timeout)
        goto remove_handle;

    if (sol_ptr_vector_append(&global.connections, connection) < 0)
        goto remove_handle;

    return connection;

remove_handle:
    curl_multi_remove_handle(global.multi, connection->curl);

free_buffer:
    sol_buffer_fini(&connection->buffer);
    free(connection);
    return NULL;
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
        free(initial_value);
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
set_allow_redir(CURL *curl, long setting)
{
    return curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, setting) == CURLE_OK;
}

static bool
set_timeout(CURL *curl, long setting)
{
    return curl_easy_setopt(curl, CURLOPT_TIMEOUT, setting) == CURLE_OK;
}

static bool
set_verbose(CURL *curl, long setting)
{
    return curl_easy_setopt(curl, CURLOPT_VERBOSE, setting) == CURLE_OK;
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
    if (unlikely(params->api_version != SOL_HTTP_PARAM_API_VERSION)) {
        SOL_ERR("Parameter has an invalid API version. Expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }

    return true;
}

SOL_API struct sol_http_client_pending *
sol_http_client_request(enum sol_http_method method,
    const char *base_uri, const struct sol_http_param *params,
    void (*cb)(void *data, struct sol_http_response *response),
    const void *data)
{
    static const struct sol_http_param empty_params = {
        .params = SOL_VECTOR_INIT(struct sol_http_param_value)
    };
    static const CURLoption sol_to_curl_method[] = {
        [SOL_HTTP_METHOD_GET] = CURLOPT_HTTPGET,
        [SOL_HTTP_METHOD_POST] = CURLOPT_HTTPPOST,
        [SOL_HTTP_METHOD_HEAD] = CURLOPT_NOBODY,
    };
    struct sol_http_param_value *value;
    struct sol_arena *arena;
    struct curl_slist *headers = NULL;
    struct sol_http_client_pending *pending;
    CURL *curl;
    uint16_t idx;

    if (!strstartswith(base_uri, "http://")
        && !strstartswith(base_uri, "https://")) {
        SOL_WRN("Invalid protocol for URI: %s", base_uri);
        return NULL;
    }

    if (params) {
        if (!check_param_api_version(params)) {
            SOL_WRN("Parameter API version mismatch");
            return NULL;
        }
    } else {
        params = &empty_params;
    }

    arena = sol_arena_new();
    if (!arena) {
        SOL_WRN("Could not create arena");
        return NULL;
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

    pending = perform_multi(curl, arena, headers, cb, data);
    if (pending)
        return pending;

invalid_option:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
no_curl_easy:
    sol_arena_del(arena);
    return NULL;
}

SOL_API void
sol_http_client_pending_cancel(struct sol_http_client_pending *pending)
{
    SOL_NULL_CHECK(pending);

    SOL_INT_CHECK(sol_ptr_vector_remove(&global.connections, pending), < 0);
    destroy_connection(pending);
}


SOL_API bool
sol_http_param_add(struct sol_http_param *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;

    SOL_NULL_CHECK(params, -EINVAL);

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
    SOL_NULL_CHECK(params);

    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return;
    }
    sol_vector_clear(&params->params);
}
