/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

int sol_http_client_init(void);
void sol_http_client_shutdown(void);

static int sol_http_client_init_lazy(void);
static void sol_http_client_shutdown_lazy(void);

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

static bool did_curl_init = false;

struct curl_http_method_opt {
    CURLoption method;
    union {
        long enabled;
        const char *request_name;
    } args;
};

struct connection_watch {
    struct sol_fd *watch;
    int fd;
};

struct sol_http_client_connection {
    CURL *curl;
    struct curl_slist *headers;
    struct curl_httppost *formpost;
    struct sol_timeout *del_timeout;
    struct sol_buffer buffer;
    struct sol_vector watches;
    struct sol_http_params response_params;
    struct sol_http_request_interface interface;
    const void *data;

    bool error;
    bool in_use;
};

static void destroy_connection(struct sol_http_client_connection *c);

static bool
schedule_del(void *data)
{
    struct sol_http_client_connection *c = data;

    c->del_timeout = NULL;
    sol_ptr_vector_remove(&global.connections, c);
    destroy_connection(c);
    return false;
}

static void
destroy_connection(struct sol_http_client_connection *c)
{
    uint16_t idx;
    struct connection_watch *cwatch;

    curl_multi_remove_handle(global.multi, c->curl);
    curl_slist_free_all(c->headers);
    curl_easy_cleanup(c->curl);
    curl_formfree(c->formpost);

    sol_buffer_fini(&c->buffer);

    sol_http_params_clear(&c->response_params);

    SOL_VECTOR_FOREACH_IDX (&c->watches, cwatch, idx)
        sol_fd_del(cwatch->watch);

    sol_vector_clear(&c->watches);

    if (c->del_timeout)
        sol_timeout_del(c->del_timeout);

    free(c);
    sol_http_client_shutdown_lazy();
}

static void
sol_http_client_shutdown_lazy(void)
{
    if (!global.ref)
        return;
    global.ref--;
    if (global.ref)
        return;

    if (global.multi_perform_timeout) {
        sol_timeout_del(global.multi_perform_timeout);
        global.multi_perform_timeout = NULL;
    }

    if (global.connections.base.len) {
        SOL_WRN("lazy shutdown with %" PRIu16 " existing connections. Leaking memory",
            global.connections.base.len);
    }

    sol_ptr_vector_clear(&global.connections);

    curl_multi_cleanup(global.multi);
    global.multi = NULL;
}

static void
call_connection_finish_cb(struct sol_http_client_connection *connection)
{
    struct sol_http_response *response;
    CURLcode r;
    long response_code;
    char *tmp;
    size_t size = 0;
    void *buffer;

    if (sol_ptr_vector_remove(&global.connections, connection) < 0)
        return;

    buffer = sol_buffer_steal(&connection->buffer, &size);
    response = &(struct sol_http_response) {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .content = SOL_BUFFER_INIT_DATA(buffer, size)
    };

    if (connection->error)
        goto err;

    r = curl_easy_getinfo(connection->curl, CURLINFO_RESPONSE_CODE,
        &response_code);
    if (r != CURLE_OK)
        goto err;
    response->response_code = (int)response_code;

    r = curl_easy_getinfo(connection->curl, CURLINFO_CONTENT_TYPE, &tmp);
    if (r != CURLE_OK)
        goto err;

    response->content_type = tmp ? strdupa(tmp) : "application/octet-stream";

    r = curl_easy_getinfo(connection->curl, CURLINFO_EFFECTIVE_URL, &tmp);
    if (r != CURLE_OK || !tmp)
        goto err;

    response->url = strdupa(tmp);

    response->param = connection->response_params;

    if (connection->interface.on_response) {
        connection->in_use = true;
        connection->interface.on_response((void *)connection->data, connection, response);
        connection->in_use = false;
    }
    goto end;

err:
    if (connection->interface.on_response) {
        connection->in_use = true;
        connection->interface.on_response((void *)connection->data, connection, NULL);
        connection->in_use = false;
    }
end:
    sol_buffer_fini(&response->content);
    destroy_connection(connection);
}

static size_t
write_cb(char *data, size_t size, size_t nmemb, void *connp)
{
    struct sol_http_client_connection *connection = connp;
    size_t data_size;
    int r;

    r = sol_util_size_mul(size, nmemb, &data_size);
    SOL_INT_CHECK(r, < 0, 0);

    r = sol_buffer_append_slice(&connection->buffer,
        SOL_STR_SLICE_STR(data, data_size));
    SOL_INT_CHECK(r, < 0, 0);

    if (connection->interface.on_data) {
        ssize_t ret;

        connection->in_use = true;
        ret = connection->interface.on_data((void *)connection->data, connection, &connection->buffer);
        connection->in_use = false;
        if (ret < 0)
            return 0;

        r = sol_buffer_remove_data(&connection->buffer, 0, ret);
        SOL_INT_CHECK(r, < 0, 0);
    }


    return data_size;
}

static size_t
read_cb(char *data, size_t size, size_t nitems, void *connp)
{
    struct sol_http_client_connection *connection = connp;
    struct sol_buffer buffer;
    size_t data_size;
    ssize_t ret;
    int r;

    r = sol_util_size_mul(size, nitems, &data_size);
    SOL_INT_CHECK(r, < 0, 0);

    buffer = SOL_BUFFER_INIT_FLAGS(data, data_size,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    connection->in_use = true;
    ret = connection->interface.on_send((void *)connection->data,
        connection, &buffer);
    connection->in_use = false;

    sol_buffer_fini(&buffer);

    return ret > 0 ? ret : CURL_READFUNC_ABORT;
}

static void
pump_multi_info_queue(void)
{
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(global.multi, &msgs_left))) {
        char *priv;
        struct sol_http_client_connection *conn;
        CURLcode r;

        if (msg->msg != CURLMSG_DONE)
            continue;

        r = curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv);
        if (r == CURLE_OK && priv) {
            /* CURLINFO_PRIVATE is defined as a string and CURL_DISABLE_TYPECHECK
             * will barf about us handling a different pointer.
             */
            conn = (struct sol_http_client_connection *)priv;
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

    if (curl_multi_socket_action(global.multi, CURL_SOCKET_TIMEOUT,
        0, &running) == CURLM_OK) {
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
    if (global.multi_perform_timeout) {
        sol_timeout_del(global.multi_perform_timeout);
        global.multi_perform_timeout = NULL;
    }

    if (timeout_ms > 0) {
        /* cURL requested a timeout value change. */
        global.timeout_ms = timeout_ms;

        global.multi_perform_timeout = sol_timeout_add(global.timeout_ms,
            multi_perform_cb, NULL);

        return global.multi_perform_timeout ? 0 : -1;
    } else {
        multi_perform_cb(NULL);
    }

    return 0;
}

int
sol_http_client_init(void)
{
    return 0;
}

void
sol_http_client_shutdown(void)
{
    struct sol_ptr_vector v;
    struct sol_http_client_connection *c;
    uint16_t i;

    /* steal vector so destroy_connection and
     * sol_http_client_shutdown_lazy() sees none left. We'll delete
     * the actual vector later in a single pass with
     * sol_ptr_vector_clear().
     */
    v = global.connections;
    sol_ptr_vector_init(&global.connections);

    SOL_PTR_VECTOR_FOREACH_IDX (&v, c, i) {
        destroy_connection(c);
    }
    sol_ptr_vector_clear(&v);

    if (did_curl_init) {
        curl_global_cleanup();
        did_curl_init = false;
    }
}

static int
sol_http_client_init_lazy(void)
{
    if (global.ref) {
        global.ref++;
        return 0;
    }

    if (!did_curl_init) {
        CURLcode r;

        /* cURL says "exactly once", we can't know what other modules
         * are doing, but at least in our case we do it once.
         */
        r = curl_global_init(CURL_GLOBAL_ALL);
        if (r == CURLE_OK)
            did_curl_init = true;
        else {
            SOL_WRN("curl_global_init(CURL_GLOBAL_ALL) failed: %s",
                curl_easy_strerror(r));
            return -EINVAL;
        }
    }

    global.multi = curl_multi_init();
    SOL_NULL_CHECK(global.multi, -EINVAL);

    curl_multi_setopt(global.multi, CURLMOPT_TIMERFUNCTION, timer_cb);

    global.multi_perform_timeout = NULL;

    global.ref++;

    return 0;
}

static bool
connection_watch_cb(void *data, int fd, uint32_t flags)
{
    bool value;
    int running;
    struct sol_http_client_connection *connection = data;
    int action = 0;

    if (flags & SOL_FD_FLAGS_IN)
        action |= CURL_CSELECT_IN;
    if (flags & SOL_FD_FLAGS_OUT)
        action |= CURL_CSELECT_OUT;
    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP))
        action |= CURL_CSELECT_ERR;

    value = !(action & CURL_CSELECT_ERR);
    if (!value) {
        uint16_t idx;
        struct connection_watch *cwatch;

        SOL_VECTOR_FOREACH_IDX (&connection->watches, cwatch, idx) {
            if (cwatch->fd == fd) {
                sol_fd_del(cwatch->watch);
                sol_vector_del(&connection->watches, idx);
                break;
            }
        }
    }

    curl_multi_socket_action(global.multi, fd, action, &running);
    pump_multi_info_queue();

    return value;
}

static void
print_connection_info_wrn(struct sol_http_client_connection *connection)
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
    struct connection_watch *cwatch;

    /* FIXME: Should the easy handle be removed from multi on failure? */
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    struct sol_http_client_connection *connection = clientp;

    if (purpose != CURLSOCKTYPE_IPCXN) {
        errno = -EINVAL;
        return -1;
    }

    cwatch = sol_vector_append(&connection->watches);
    SOL_NULL_CHECK(cwatch, -ENOMEM);

    cwatch->fd = socket(addr->family, addr->socktype | SOCK_CLOEXEC, addr->protocol);
    if (cwatch->fd < 0) {
        SOL_WRN("Could not create socket (family %d, type %d, protocol %d)",
            addr->family, addr->socktype, addr->protocol);
        print_connection_info_wrn(connection);
        goto err;
    }

    cwatch->watch = sol_fd_add(cwatch->fd, fd_flags,
        connection_watch_cb, connection);
    SOL_NULL_CHECK_GOTO(cwatch->watch, err_watch);

    return cwatch->fd;

err_watch:
    close(cwatch->fd);
err:
    sol_vector_del_last(&connection->watches);
    return -1;
}

static int
xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow)
{
    struct sol_http_client_connection *connection = clientp;

    if (dltotal > 0 && SOL_UNLIKELY(dltotal < dlnow)) {
        SOL_WRN("Received more than expected, aborting transfer (%"
            CURL_FORMAT_CURL_OFF_T "< %" CURL_FORMAT_CURL_OFF_T ")",
            dltotal, dlnow);
        print_connection_info_wrn(connection);
        connection->error = true;
        return 1;
    }

    return 0;
}

static size_t
header_cb(char *data, size_t size, size_t nmemb, void *connp)
{
    struct sol_http_client_connection *connection = connp;
    struct sol_http_param_value param;
    size_t data_size, key_size, discarted, i, cookie_name_size;
    char *sep, *decoded_key, *decoded_value;
    int r;

    r = sol_util_size_mul(size, nmemb, &data_size);
    SOL_INT_CHECK(r, < 0, 0);

    sep = memchr(data, ':', data_size);
    if (!sep)
        return data_size;

    key_size = sep - data;

    // The ':'
    discarted = 1;
    sep++;

    //Trim spaces
    while (isspace((uint8_t)*sep)) {
        sep++;
        discarted++;
    }

    for (i = data_size - 1; isspace((uint8_t)data[i]); i--)
        discarted++;

    if (!strncasecmp(data, "Set-Cookie:", key_size)) {
        param.type = SOL_HTTP_PARAM_COOKIE;
        cookie_name_size = 0;
        while (sep[cookie_name_size++] != '=') ;
        decoded_key = curl_easy_unescape(connection->curl, sep,
            cookie_name_size - 1, NULL);
        SOL_NULL_CHECK_GOTO(decoded_key, err_exit);
        sep += cookie_name_size + 1;
        discarted += cookie_name_size + 1;
    } else {
        param.type = SOL_HTTP_PARAM_HEADER;
        decoded_key = curl_easy_unescape(connection->curl, data, key_size,
            NULL);
        SOL_NULL_CHECK_GOTO(decoded_key, err_exit);
    }

    decoded_value = curl_easy_unescape(connection->curl, sep,
        data_size - key_size - discarted, NULL);
    SOL_NULL_CHECK_GOTO(decoded_value, err_value);

    param.value.key_value.key = sol_str_slice_from_str(decoded_key);
    param.value.key_value.value = sol_str_slice_from_str(decoded_value);

    if (sol_http_params_add_copy(&connection->response_params, param) < 0) {
        SOL_ERR("Could not add the http param - key: %.*s value: %.*s",
            SOL_STR_SLICE_PRINT(param.value.key_value.key),
            SOL_STR_SLICE_PRINT(param.value.key_value.value));
        goto err_add;
    }

    curl_free(decoded_key);
    curl_free(decoded_value);
    return data_size;

err_add:
    curl_free(decoded_value);
err_value:
    curl_free(decoded_key);
err_exit:
    return 0;
}

static struct sol_http_client_connection *
perform_multi(CURL *curl, struct curl_slist *headers,
    struct curl_httppost *formpost,
    const struct sol_http_request_interface *interface,
    const void *data)
{
    struct sol_http_client_connection *connection;
    void *buf;
    size_t data_size;
    int running;

    SOL_INT_CHECK(global.ref, <= 0, NULL);
    SOL_NULL_CHECK(curl, NULL);

    connection = calloc(1, sizeof(*connection));
    SOL_NULL_CHECK(connection, NULL);

    connection->headers = headers;
    connection->formpost = formpost;
    connection->curl = curl;
    connection->interface = *interface;
    connection->data = data;
    connection->error = false;
    sol_vector_init(&connection->watches, sizeof(struct connection_watch));

    data_size = interface->data_buffer_size;
    if (data_size) {
        buf = malloc(data_size);
        SOL_NULL_CHECK_GOTO(buf, err_buf);

        sol_buffer_init_flags(&connection->buffer, buf, data_size,
            SOL_BUFFER_FLAGS_NO_NUL_BYTE | SOL_BUFFER_FLAGS_FIXED_CAPACITY);
    } else
        sol_buffer_init_flags(&connection->buffer, NULL, 0,
            SOL_BUFFER_FLAGS_NO_NUL_BYTE | SOL_BUFFER_FLAGS_DEFAULT);
    sol_http_params_init(&connection->response_params);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, connection);

    if (interface->on_send) {
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
        curl_easy_setopt(curl, CURLOPT_READDATA, connection);
    }

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, connection);

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

    if (sol_ptr_vector_append(&global.connections, connection) < 0)
        goto remove_handle;

    if (global.multi_perform_timeout)
        return connection;

    /* Apparently this is required to kick off cURL's internal main loop. */
    curl_multi_socket_action(global.multi, CURL_SOCKET_TIMEOUT, 0, &running);

    /* This timeout will be recreated if cURL changes the timeout value. */
    global.multi_perform_timeout = sol_timeout_add(global.timeout_ms,
        multi_perform_cb, NULL);
    if (!global.multi_perform_timeout)
        goto remove_connection;

    return connection;

remove_connection:
    sol_ptr_vector_remove(&global.connections, connection);

remove_handle:
    curl_multi_remove_handle(global.multi, connection->curl);

free_buffer:
    sol_buffer_fini(&connection->buffer);
err_buf:
    free(connection);
    return NULL;
}

static bool
set_headers_from_params(CURL *curl, const struct sol_http_params *params,
    struct curl_slist **headers)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_http_param_value *iter;
    struct curl_slist *list = NULL;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (&params->params, iter, idx) {
        struct sol_str_slice key = iter->value.key_value.key;
        struct sol_str_slice value = iter->value.key_value.value;
        struct curl_slist *tmp_list;
        int r;

        if (iter->type != SOL_HTTP_PARAM_HEADER)
            continue;

        if (sol_str_slice_str_case_eq(key, "Content-Length")) {
            curl_off_t len;
            len = sol_util_strtol_n(iter->value.key_value.value.data, NULL,
                iter->value.key_value.value.len, 0);

            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, len);
        }

        buf.used = 0;
        r = sol_buffer_append_slice(&buf, key);
        SOL_INT_CHECK_GOTO(r, < 0, fail);
        r = sol_buffer_append_char(&buf, ':');
        SOL_INT_CHECK_GOTO(r, < 0, fail);
        r = sol_buffer_append_slice(&buf, value);
        SOL_INT_CHECK_GOTO(r, < 0, fail);

        tmp_list = curl_slist_append(list, sol_buffer_at(&buf, 0));
        if (!tmp_list)
            goto fail;
        list = tmp_list;
    }

    if (list) {
        if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list) != CURLE_OK)
            goto fail;
    }

    *headers = list;
    sol_buffer_fini(&buf);
    return true;

fail:
    sol_buffer_fini(&buf);
    curl_slist_free_all(list);
    return false;
}

static bool
set_auth_basic(CURL *curl, const struct sol_http_param_value *param)
{
    char *user, *password;
    bool r = false;

    user = password = NULL;

    if (param->value.auth.user.len) {
        user = sol_str_slice_to_str(param->value.auth.user);
        SOL_NULL_CHECK(user, false);
    }

    if (param->value.auth.password.len) {
        password = sol_str_slice_to_str(param->value.auth.password);
        SOL_NULL_CHECK_GOTO(password, exit);
    }

    if (curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC) != CURLE_OK)
        goto exit;
    if (user && curl_easy_setopt(curl, CURLOPT_USERNAME, user) != CURLE_OK)
        goto exit;
    if (password && curl_easy_setopt(curl, CURLOPT_PASSWORD,
        password) != CURLE_OK)
        goto exit;

    r = true;
exit:
    free(password);
    free(user);
    return r;
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
set_postfields(CURL *curl, const struct sol_str_slice slice)
{

    if (curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
        (curl_off_t)slice.len) != CURLE_OK)
        return false;

    return curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS,
        slice.len ? slice.data : "") == CURLE_OK;
}

static bool
set_cookies_from_params(CURL *curl, const struct sol_http_params *params)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    bool r;
    int err;

    err = sol_http_encode_params(&buf, SOL_HTTP_PARAM_COOKIE, params);
    SOL_INT_CHECK(err, < 0, false);

    r = curl_easy_setopt(curl, CURLOPT_COOKIE, buf.data) == CURLE_OK;
    sol_buffer_fini(&buf);
    return r;
}

static bool
set_uri_from_params(CURL *curl, const char *base,
    const struct sol_http_params *params)
{
    struct sol_buffer full_uri = SOL_BUFFER_INIT_EMPTY;
    int err;
    bool r;

    err = sol_http_create_uri_from_str(&full_uri, base, params);
    SOL_INT_CHECK(err, < 0, false);
    r = curl_easy_setopt(curl, CURLOPT_URL, full_uri.data) == CURLE_OK;
    sol_buffer_fini(&full_uri);
    return r;
}

static bool
set_post_fields_from_params(CURL *curl, const struct sol_http_params *params)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int err;
    bool r;

    err = sol_http_encode_params(&buf, SOL_HTTP_PARAM_POST_FIELD, params);
    SOL_INT_CHECK(err, < 0, false);
    r = set_postfields(curl, sol_buffer_get_slice(&buf));
    sol_buffer_fini(&buf);
    return r;
}

static int
set_post_data_from_params(CURL *curl, struct curl_httppost **formpost,
    const struct sol_http_params *params)
{
    int len = 0;
    uint16_t idx;
    struct sol_http_param_value *iter;
    struct curl_httppost *lastptr = NULL;
    CURLFORMcode ret;
    bool has_post_field = false;

    SOL_HTTP_PARAMS_FOREACH_IDX (params, iter, idx) {
        if (iter->type == SOL_HTTP_PARAM_POST_FIELD)
            has_post_field = true;

        if (iter->type != SOL_HTTP_PARAM_POST_DATA)
            continue;

        if (has_post_field) {
            SOL_WRN("Request can not have both, POSTFIELD and POSTDATA at same time.");
            return -1;
        }

        if (iter->value.data.filename.len) {
            char filename[PATH_MAX + 1];
            if (iter->value.data.filename.len >= PATH_MAX)
                return -1;
            memcpy(filename, iter->value.data.filename.data,
                iter->value.data.filename.len);
            filename[iter->value.data.filename.len] = '\0';
            ret = curl_formadd(formpost, &lastptr,
                CURLFORM_COPYNAME, iter->value.data.key.data,
                CURLFORM_NAMELENGTH, iter->value.data.key.len,
                CURLFORM_FILE, filename,
                CURLFORM_END);
        } else {
            ret = curl_formadd(formpost, &lastptr,
                CURLFORM_COPYNAME, iter->value.data.key.data,
                CURLFORM_NAMELENGTH, iter->value.data.key.len,
                CURLFORM_COPYCONTENTS, iter->value.data.value.data,
                CURLFORM_CONTENTSLENGTH, iter->value.data.value.len,
                CURLFORM_END);
        }

        SOL_EXP_CHECK(ret != CURL_FORMADD_OK, -1);
        len++;
    }

    if (*formpost)
        return curl_easy_setopt(curl, CURLOPT_HTTPPOST, *formpost) == CURLE_OK ? len : -1;

    return len;
}

static bool
check_param_api_version(const struct sol_http_params *params)
{
#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(params->api_version != SOL_HTTP_PARAM_API_VERSION)) {
        SOL_ERR("Parameter has an invalid API version. Expected %" PRIu16 ", got %" PRIu16,
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }
#endif

    return true;
}

static struct sol_http_client_connection *
client_request_internal(enum sol_http_method method,
    const char *url, const struct sol_http_params *params,
    const struct sol_http_request_interface *interface,
    const void *data)
{
    static const struct sol_http_params empty_params = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_PARAM_API_VERSION, )
        .params = SOL_VECTOR_INIT(struct sol_http_param_value)
    };
    static const struct curl_http_method_opt sol_to_curl_method[] = {
        [SOL_HTTP_METHOD_GET] = { .method = CURLOPT_HTTPGET,
                                  .args.enabled = 1L },
        [SOL_HTTP_METHOD_POST] = { .method = CURLOPT_HTTPPOST,
                                   .args.enabled = 1L },
        [SOL_HTTP_METHOD_HEAD] = { .method = CURLOPT_NOBODY,
                                   .args.enabled = 1L },
        [SOL_HTTP_METHOD_DELETE] = { .method = CURLOPT_CUSTOMREQUEST,
                                     .args.request_name = "DELETE" },
        [SOL_HTTP_METHOD_PUT] = { .method = CURLOPT_CUSTOMREQUEST,
                                  .args.request_name = "PUT" },
        [SOL_HTTP_METHOD_CONNECT] = { .method = CURLOPT_CUSTOMREQUEST,
                                      .args.request_name = "CONNECT" },
        [SOL_HTTP_METHOD_OPTIONS] = { .method = CURLOPT_CUSTOMREQUEST,
                                      .args.request_name = "OPTIONS" },
        [SOL_HTTP_METHOD_TRACE] = { .method = CURLOPT_CUSTOMREQUEST,
                                    .args.request_name = "TRACE" },
        [SOL_HTTP_METHOD_PATCH] = { .method = CURLOPT_CUSTOMREQUEST,
                                    .args.request_name = "PATCH" }
    };
    struct sol_http_param_value *value;
    struct curl_slist *headers = NULL;
    struct curl_httppost *formpost = NULL;
    struct sol_http_client_connection *pending;
    CURL *curl;
    uint16_t idx;
    struct curl_http_method_opt method_opt;
    CURLcode code;

    SOL_NULL_CHECK(url, NULL);

    if (method >= SOL_HTTP_METHOD_INVALID) {
        SOL_WRN("The HTTP method is set to invalid");
        return NULL;
    }

    if (!strstartswith(url, "http://")
        && !strstartswith(url, "https://")) {
        SOL_WRN("Invalid protocol for URI: %s", url);
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

    if (sol_http_client_init_lazy() < 0) {
        SOL_WRN("could not initialize http-client integration with cURL");
        return NULL;
    }

    curl = curl_easy_init();
    if (!curl) {
        SOL_WRN("Could not create cURL handle");
        goto failed_easy_init;
    }

    method_opt = sol_to_curl_method[method];

    if (method <= SOL_HTTP_METHOD_HEAD)
        code = curl_easy_setopt(curl, method_opt.method,
            method_opt.args.enabled);
    else
        code = curl_easy_setopt(curl, method_opt.method,
            method_opt.args.request_name);

    if (code != CURLE_OK) {
        SOL_WRN("Could not set HTTP method");
        goto invalid_option;
    }

    if (!set_uri_from_params(curl, url, params)) {
        SOL_WRN("Could not set URI from params");
        goto invalid_option;
    }

    if (!set_cookies_from_params(curl, params)) {
        SOL_WRN("Could not set cookies from params");
        goto invalid_option;
    }

    if (method == SOL_HTTP_METHOD_POST) {
        if (!set_post_fields_from_params(curl, params)) {
            SOL_WRN("Could not set POST fields from params");
            goto invalid_option;
        }
        if (set_post_data_from_params(curl, &formpost, params) < 0) {
            SOL_WRN("Could not set POST data from params");
            goto invalid_option;
        }
    }

    if (!set_headers_from_params(curl, params, &headers)) {
        SOL_WRN("Could not set custom headers from params");
        goto invalid_option;
    }

    SOL_VECTOR_FOREACH_IDX (&params->params, value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_POST_FIELD:
            /* already handled by set_post_fields_from_params() */
            continue;
        case SOL_HTTP_PARAM_POST_DATA:
            /* already handled by set_post_data_from_params() */
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
            if (!set_auth_basic(curl, value))
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
        case SOL_HTTP_PARAM_FRAGMENT:
            /* already handle by  set_uri_from_params() */
            continue;
        }
    }

    pending = perform_multi(curl, headers, formpost, interface, data);
    if (pending)
        return pending;

invalid_option:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_formfree(formpost);
failed_easy_init:
    sol_http_client_shutdown_lazy();
    return NULL;
}


SOL_API struct sol_http_client_connection *
sol_http_client_request(enum sol_http_method method,
    const char *url, const struct sol_http_params *params,
    void (*cb)(void *data, struct sol_http_client_connection *connection,
    struct sol_http_response *response),
    const void *data)
{
    struct sol_http_client_connection *pending;
    const struct sol_http_request_interface *interface =
        &(struct sol_http_request_interface) {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_REQUEST_INTERFACE_API_VERSION, )
        .on_response = cb
    };

    pending = client_request_internal(method, url, params, interface, data);
    SOL_NULL_CHECK(pending, NULL);

    return pending;
}

SOL_API void
sol_http_client_connection_cancel(struct sol_http_client_connection *pending)
{
    SOL_NULL_CHECK(pending);

    if (pending->in_use && !pending->del_timeout) {
        pending->del_timeout = sol_timeout_add(0, schedule_del, pending);
        SOL_NULL_CHECK(pending->del_timeout);
        return;
    } else if (pending->in_use && pending->del_timeout)
        return;

    SOL_INT_CHECK(sol_ptr_vector_remove(&global.connections, pending), < 0);
    destroy_connection(pending);
}

SOL_API struct sol_http_client_connection *
sol_http_client_request_with_interface(enum sol_http_method method,
    const char *url, const struct sol_http_params *params,
    const struct sol_http_request_interface *interface,
    const void *data)
{
    struct sol_http_client_connection *pending;

#ifndef SOL_NO_API_VERSION
    if (interface->api_version != SOL_HTTP_REQUEST_INTERFACE_API_VERSION) {
        SOL_WRN("interface->api_version=%" PRIu16 ", "
            "expected version is %" PRIu16 ".",
            interface->api_version, SOL_HTTP_REQUEST_INTERFACE_API_VERSION);
        return NULL;
    }
#endif

    pending = client_request_internal(method, url, params, interface, data);
    SOL_NULL_CHECK(pending, NULL);

    return pending;
}
