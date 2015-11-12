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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sol-http-server.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"
#include "sol-arena.h"

#define SOL_HTTP_PARAM_IF_SINCE_MODIFIED "If-Since-Modified"
#define SOL_HTTP_PARAM_LAST_MODIFIED "Last-Modified"
#define READABLE_BY_EVERYONE (S_IRUSR | S_IRGRP | S_IROTH)

struct http_handler {
    time_t last_modified;
    struct sol_str_slice path;
    int (*request_cb)(void *data, struct sol_http_request *request);
    const void *user_data;
};

struct sol_http_request {
    struct MHD_Connection *connection;
    struct MHD_PostProcessor *pp;
    const char *url;
    struct sol_http_param params;
    enum sol_http_method method;
    time_t if_since_modified;
};

struct static_dir {
    size_t basename_len;
    char *basename;
    char *root;
};

struct default_page {
    char *page;
    enum sol_http_status_code error;
};

struct sol_http_server {
    struct MHD_Daemon *daemon;
    struct sol_vector dirs;
    struct sol_vector handlers;
    struct sol_vector fds;
    struct sol_vector defaults;
    struct sol_ptr_vector requests;
};

struct http_connection {
    struct sol_fd *watch;
    int fd;
};

static char *
sanitize_path(const char *str)
{
    char *handler, *aux;

    /*
     * Ensure that will have at least space for the first /
     * and nul char at end.
     */
    handler = calloc(1, strlen(str) + 2);
    SOL_NULL_CHECK(handler, NULL);

    aux = handler;
    *aux = '/';
    while (str && (*str != '\0')) {
        if ((*str == '/') && (*aux == '/')) {
            str++;
            continue;
        }

        aux++;
        *aux = *str;
        str++;
    }
    *(++aux) = '\0';

    return handler;
}

SOL_API const char *
sol_http_request_get_url(const struct sol_http_request *request)
{
    SOL_NULL_CHECK(request, NULL);

    return request->url;
}

SOL_API const struct sol_http_param *
sol_http_request_get_params(const struct sol_http_request *request)
{
    SOL_NULL_CHECK(request, NULL);

    return &request->params;
}

SOL_API enum sol_http_method
sol_http_request_get_method(const struct sol_http_request *request)
{
    SOL_NULL_CHECK(request, SOL_HTTP_METHOD_INVALID);

    return request->method;
}

static struct MHD_Response *
build_mhd_response(const struct sol_http_response *response)
{
    struct sol_buffer buf;
    uint16_t idx;
    struct MHD_Response *r;
    struct sol_http_param_value *value;

    r = MHD_create_response_from_buffer(response->content.used, response->content.data,
        MHD_RESPMEM_MUST_COPY);
    if (!r)
        return NULL;

    sol_buffer_init(&buf);
    SOL_HTTP_PARAM_FOREACH_IDX (&response->param, value, idx) {
        int ret;
        buf.used = 0;
        switch (value->type) {
        case SOL_HTTP_PARAM_HEADER:
            ret = sol_buffer_append_slice(&buf, value->value.key_value.key);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_char(&buf, 0);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_slice(&buf, value->value.key_value.value);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_char(&buf, 0);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            if (MHD_add_response_header(r, sol_buffer_at(&buf, 0), sol_buffer_at(&buf,
                value->value.key_value.key.len + 1)) == MHD_NO) {
                SOL_WRN("Could not add the header: %.*s", SOL_STR_SLICE_PRINT(value->value.key_value.key));
                goto err;
            }
            break;
        case SOL_HTTP_PARAM_COOKIE:
            ret = sol_buffer_append_slice(&buf, value->value.key_value.key);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_char(&buf, '=');
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_slice(&buf, value->value.key_value.value);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_char(&buf, 0);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            if (MHD_add_response_header(r, MHD_HTTP_HEADER_SET_COOKIE, sol_buffer_at(&buf, 0)) == MHD_NO) {
                SOL_WRN("Could not add the cookie: %.*s", SOL_STR_SLICE_PRINT(value->value.key_value.key));
                goto err;
            }
            break;
        case SOL_HTTP_PARAM_QUERY_PARAM:
        case SOL_HTTP_PARAM_ALLOW_REDIR:
        case SOL_HTTP_PARAM_TIMEOUT:
        case SOL_HTTP_PARAM_AUTH_BASIC:
        default:
            break;
        }
    }

    sol_buffer_fini(&buf);
    return r;

err:
    sol_buffer_fini(&buf);
    MHD_destroy_response(r);
    return NULL;
}

static int
post_iterator(void *data, enum MHD_ValueKind kind, const char *key,
    const char *filename, const char *type,
    const char *encoding, const char *value, uint64_t off, size_t size)
{
    struct sol_http_request *request = data;

    if (!size)
        return MHD_NO;

    if (!sol_http_param_add_copy(&request->params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD(key, value))) {
        SOL_WRN("Could not add %s key", key);
        return MHD_NO;
    }

    return MHD_YES;
}

static time_t
process_if_modified_since(const char *value)
{
    char *s;
    struct tm t;

    s = strptime(value, "%a, %d %b %Y %H:%M:%S GMT", &t);
    if (!s || *s != '\0')
        return 0;

    return timegm(&t);
}

static int
headers_iterator(void *data, enum MHD_ValueKind kind, const char *key, const char *value)
{
    struct sol_http_request *request = data;

    switch (kind) {
    case MHD_HEADER_KIND:
        if (!strcasecmp(key, SOL_HTTP_PARAM_IF_SINCE_MODIFIED)) {
            request->if_since_modified = process_if_modified_since(key);
            SOL_INT_CHECK_GOTO(request->if_since_modified, == 0, param_err);
        }
        if (!sol_http_param_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_HEADER(key, value)))
            goto param_err;
        break;
    case MHD_COOKIE_KIND:
        if (!sol_http_param_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_COOKIE(key, value)))
            goto param_err;
        break;
    case MHD_GET_ARGUMENT_KIND:
        if (!sol_http_param_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_QUERY(key, value)))
            goto param_err;
        break;
    case MHD_POSTDATA_KIND:
        if (!sol_http_param_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_POST_FIELD(key, value)))
            goto param_err;
        break;
    default:
        goto param_err;
    }

    return MHD_YES;

param_err:
    return MHD_NO;
}

static int
get_static_file(const struct static_dir *dir, const char *url)
{
    int ret;
    char path[PATH_MAX], *real_path;

    /* url given by microhttpd starts from /. e. g.
     * https://www.solettaproject.com => url == /
     * https://www.solettaproject.com/thankyou => url == /thankyou
     */
    while (*url && *(url + 1) == '/')
        url++;

    if (!streqn(url, dir->basename, dir->basename_len))
        return -EINVAL;

    url += dir->basename_len;
    if (*(dir->basename + dir->basename_len - 1) != '/' &&
        *url != '/')
        return -EINVAL;

    while (*url == '/')
        url++;

    ret = snprintf(path, sizeof(path), "%s/%s", dir->root,
        *url ? url : "index.html");
    if (ret < 0 || ret >= (int)sizeof(path))
        return -ENOMEM;

    real_path = realpath(path, NULL);
    if (!real_path)
        return -errno;

    if (!strstartswith(real_path, dir->root)) {
        free(real_path);
        return -EINVAL;
    }
    free(real_path);

    /*  According with microhttpd fd will be closed when response is
     *  destroyed and fd should be in 'blocking' mode
     */
    return open(path, O_RDONLY | O_CLOEXEC);
}

static struct MHD_Response *
get_default_response(const struct sol_http_server *server, enum sol_http_status_code error)
{
    int r;
    uint16_t i;
    char buf[32];
    struct stat st;
    struct default_page *def;
    struct MHD_Response *response = NULL;

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i) {
        int fd;

        if (def->error != error)
            continue;

        fd = open(def->page, O_RDONLY | O_CLOEXEC);
        SOL_INT_CHECK(fd, < 0, NULL);

        r = fstat(fd, &st);
        if (r < 0) {
            close(fd);
            SOL_WRN("Failed to status the file: %s (%s)", def->page,
                sol_util_strerrora(errno));
            return NULL;
        }

        response = MHD_create_response_from_fd(st.st_size, fd);
        if (!response) {
            close(fd);
            SOL_WRN("Could not create the response with: %s", def->page);
            return NULL;
        }
        return response;
    }

    r = snprintf(buf, sizeof(buf), "status - %d", error);
    if (r < 0 || r > (int)sizeof(buf)) {
        SOL_WRN("Could not set the status code on response body");
        response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    } else {
        response = MHD_create_response_from_buffer(r, buf, MHD_RESPMEM_MUST_COPY);
    }

    return response;
}

static int
http_server_handler(void *data, struct MHD_Connection *connection, const char *url, const char *method,
    const char *version, const char *upload_data, size_t *upload_data_size, void **ptr)
{
    int ret, fd;
    uint16_t i;
    char *path = NULL;
    struct static_dir *dir;
    struct MHD_Response *mhd_response = NULL;
    struct sol_http_server *server = data;
    struct http_handler *handler;
    struct sol_http_request *req = *ptr;
    enum sol_http_status_code status = SOL_HTTP_STATUS_NOT_FOUND;

    if (!req) {
        req = calloc(1, sizeof(struct sol_http_request));
        SOL_NULL_CHECK(req, MHD_NO);

        ret = sol_ptr_vector_append(&server->requests, req);
        if (ret < 0) {
            SOL_WRN("Could not append request for: %s", url);
            free(req);
            return MHD_NO;
        }

        sol_http_param_init(&req->params);
        req->url = url;
        req->connection = connection;
        *ptr = req;
        return MHD_YES;
    }

    if (method && streq(method, "POST")) {
        req->method = SOL_HTTP_METHOD_POST;
        if (!req->pp)
            req->pp = MHD_create_post_processor(connection, 1024, post_iterator, req);
        SOL_NULL_CHECK_GOTO(req->pp, end);

        if (MHD_post_process(req->pp, upload_data, *upload_data_size) == MHD_NO) {
            status = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
            goto end;
        }

        if (*upload_data_size) {
            *upload_data_size = 0;
            return MHD_YES;
        }
    } else if (method && streq(method, "GET")) {
        req->method = SOL_HTTP_METHOD_GET;
    } else {
        SOL_WRN("Method %s not implemented", method ? method : "NULL");
        status = SOL_HTTP_STATUS_NOT_IMPLEMENTED;
        goto end;
    }

    path = sanitize_path(url);
    if (!path) {
        status = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
        goto end;
    }

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (!sol_str_slice_str_eq(handler->path, path))
            continue;

        free(path);
        MHD_get_connection_values(connection, MHD_HEADER_KIND, headers_iterator, req);
        MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, headers_iterator, req);
        MHD_get_connection_values(connection, MHD_COOKIE_KIND, headers_iterator, req);
        MHD_get_connection_values(connection, MHD_POSTDATA_KIND, headers_iterator, req);
        if (handler->last_modified && (req->if_since_modified > handler->last_modified)) {
            status = SOL_HTTP_STATUS_NOT_MODIFIED;
            goto end;
        } else {
            MHD_suspend_connection(connection);
            ret = handler->request_cb((void *)handler->user_data, req);
            SOL_INT_CHECK(ret, < 0, MHD_NO);
        }

        return MHD_YES;
    }

    free(path);
    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        struct stat st;
        fd = get_static_file(dir, url);
        if (fd < 0 && errno == EACCES) {
            status = SOL_HTTP_STATUS_FORBIDDEN;
        } else if (fd > 0) {
            ret = fstat(fd, &st);
            if (ret < 0) {
                close(fd);
            } else {
                if ((st.st_mode & READABLE_BY_EVERYONE) != READABLE_BY_EVERYONE) {
                    status = SOL_HTTP_STATUS_FORBIDDEN;
                    close(fd);
                    break;
                }

                mhd_response = MHD_create_response_from_fd(st.st_size, fd);
                if (mhd_response) {
                    status = SOL_HTTP_STATUS_OK;
                    goto end;
                } else {
                    close(fd);
                }
            }
            break;
        }
    }

    mhd_response = get_default_response(server, status);
    SOL_NULL_CHECK(mhd_response, MHD_NO);
end:
    ret = MHD_queue_response(connection, status, mhd_response);
    MHD_destroy_response(mhd_response);
    return ret;
}

static bool
connection_watch_cb(void *data, int fd, uint32_t flags)
{
    uint16_t i;
    fd_set rs, ws, es;
    struct sol_http_server *server = data;
    struct http_connection *connection;

    SOL_NULL_CHECK(server, false);

    FD_ZERO(&rs);
    FD_ZERO(&ws);
    FD_ZERO(&es);

    if (flags & SOL_FD_FLAGS_IN)
        FD_SET(fd, &rs);
    if (flags & SOL_FD_FLAGS_OUT)
        FD_SET(fd, &ws);
    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP))
        FD_SET(fd, &es);

    /* This is far from optimized, the mainloop is given one file descriptor
       per time while it's possible to pass all the file descriptors with events
       to MHD at once. */
    if (MHD_run_from_select(server->daemon, &rs, &ws, &es) == MHD_NO)
        SOL_WRN("Something wrong happened inside microhttpd");

    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP)) {
        SOL_VECTOR_FOREACH_IDX (&server->fds, connection, i) {
            if (connection->fd == fd) {
                sol_vector_del(&server->fds, i);
                break;
            }
        }

        return false;
    }

    return true;
}

static void
notify_connection_cb(void *data, struct MHD_Connection *connection, void **socket_data,
    enum MHD_ConnectionNotificationCode code)
{
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    const union MHD_ConnectionInfo *info;
    struct sol_http_server *server = data;
    struct http_connection *conn;

    if (code != MHD_CONNECTION_NOTIFY_STARTED)
        return;

    info = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    SOL_NULL_CHECK(info);

    conn = sol_vector_append(&server->fds);
    SOL_NULL_CHECK(conn);

    conn->fd = (int)info->connect_fd;
    conn->watch = sol_fd_add(conn->fd, fd_flags, connection_watch_cb, server);

    if (!conn->watch) {
        char error_str[128];
        SOL_WRN("Could not watch file descriptor: %s", sol_util_strerror(errno, error_str, sizeof(error_str)));
        sol_vector_del_last(&server->fds);
    }
}

static void
free_request(struct sol_http_request *request)
{
    if (request->pp)
        MHD_destroy_post_processor(request->pp);
    sol_http_param_free(&request->params);
    free(request);
}

static void
notify_connection_finished_cb(void *data, struct MHD_Connection *connection,
    void **con_data, enum MHD_RequestTerminationCode code)
{
    struct sol_http_server *server = data;
    struct sol_http_request *request = *con_data;

    SOL_NULL_CHECK(request);

    sol_ptr_vector_remove(&server->requests, request);
    free_request(request);
}

SOL_API struct sol_http_server *
sol_http_server_new(uint16_t port)
{
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    const union MHD_DaemonInfo *info;
    struct http_connection *conn;
    struct sol_http_server *server;

    server = calloc(1, sizeof(*server));
    SOL_NULL_CHECK(server, NULL);

    sol_vector_init(&server->handlers, sizeof(struct http_handler));
    sol_vector_init(&server->fds, sizeof(struct http_connection));
    sol_vector_init(&server->dirs, sizeof(struct static_dir));
    sol_vector_init(&server->defaults, sizeof(struct default_page));
    sol_ptr_vector_init(&server->requests);

    server->daemon = MHD_start_daemon(MHD_USE_SUSPEND_RESUME,
        port, NULL, NULL,
        http_server_handler, server,
        MHD_OPTION_NOTIFY_CONNECTION, notify_connection_cb, server,
        MHD_OPTION_NOTIFY_COMPLETED, notify_connection_finished_cb, server,
        MHD_OPTION_END);
    SOL_NULL_CHECK_GOTO(server->daemon, err_daemon);

    info = MHD_get_daemon_info(server->daemon, MHD_DAEMON_INFO_LISTEN_FD);
    SOL_NULL_CHECK_GOTO(info, err);

    conn = sol_vector_append(&server->fds);
    SOL_NULL_CHECK_GOTO(conn, err);

    conn->fd = (int)info->listen_fd;
    conn->watch = sol_fd_add(conn->fd, fd_flags, connection_watch_cb, server);
    SOL_NULL_CHECK_GOTO(conn->watch, err);

    return server;

err:
    MHD_stop_daemon(server->daemon);
err_daemon:
    sol_vector_clear(&server->handlers);
    sol_vector_clear(&server->fds);
    sol_vector_clear(&server->dirs);
    sol_ptr_vector_clear(&server->requests);
    free(server);
    return NULL;
}

SOL_API void
sol_http_server_del(struct sol_http_server *server)
{
    uint16_t i;
    struct static_dir *dir;
    struct default_page *def;
    struct http_handler *handler;
    struct http_connection *connection;
    struct sol_http_request *request;

    SOL_NULL_CHECK(server);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->requests, request, i) {
        MHD_resume_connection(request->connection);
    }
    sol_ptr_vector_clear(&server->requests);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i)
        free((char *)handler->path.data);
    sol_vector_clear(&server->handlers);

    SOL_VECTOR_FOREACH_IDX (&server->fds, connection, i)
        sol_fd_del(connection->watch);
    sol_vector_clear(&server->fds);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        free(dir->root);
        free(dir->basename);
    }
    sol_vector_clear(&server->dirs);

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i)
        free(def->page);
    sol_vector_clear(&server->defaults);

    MHD_stop_daemon(server->daemon);

    free(server);
}

SOL_API int
sol_http_server_register_handler(struct sol_http_server *server, const char *path,
    int (*request_cb)(void *data, struct sol_http_request *request),
    const void *data)
{
    uint16_t i;
    char *p;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(request_cb, -EINVAL);

    p = sanitize_path(path);
    SOL_NULL_CHECK(p, -ENOMEM);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (sol_str_slice_str_eq(handler->path, p)) {
            free(p);
            return -EINVAL;
        }
    }

    handler = sol_vector_append(&server->handlers);
    SOL_NULL_CHECK_GOTO(handler, err);

    SOL_NULL_CHECK_GOTO(p, error);
    handler->path = sol_str_slice_from_str(p);

    handler->request_cb = request_cb;
    handler->user_data = data;
    handler->last_modified = 0;

    return 0;

error:
    sol_vector_del_last(&server->handlers);
err:
    free(p);
    return -ENOMEM;
}

SOL_API int
sol_http_server_unregister_handler(struct sol_http_server *server, const char *path)
{
    uint16_t i;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (sol_str_slice_str_eq(handler->path, path)) {
            free((char *)handler->path.data);
            sol_vector_del(&server->handlers, i);
            return 0;
        }
    }

    return -ENOENT;
}

SOL_API int
sol_http_server_send_response(struct sol_http_request *request, struct sol_http_response *response)
{
    int ret;
    struct MHD_Response *mhd_response;

    SOL_NULL_CHECK(request, -EINVAL);
    SOL_NULL_CHECK(request->connection, -EINVAL);
    SOL_NULL_CHECK(response, -EINVAL);

    MHD_resume_connection(request->connection);

    mhd_response = build_mhd_response(response);
    SOL_NULL_CHECK(mhd_response, -1);

    ret = MHD_queue_response(request->connection, response->response_code, mhd_response);
    MHD_destroy_response(mhd_response);

    SOL_INT_CHECK(ret, != MHD_YES, -1);

    return ret;
}

SOL_API int
sol_http_server_set_last_modified(struct sol_http_server *server, const char *path, time_t modified)
{
    uint16_t idx;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, idx) {
        if (sol_str_slice_str_eq(handler->path, path)) {
            handler->last_modified = modified;
            return 0;
        }
    }

    return -ENODATA;
}

SOL_API int
sol_http_server_add_dir(struct sol_http_server *server, const char *basename, const char *rootdir)
{
    uint16_t i;
    char *p;
    struct static_dir *dir;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(rootdir, -EINVAL);
    SOL_NULL_CHECK(basename, -EINVAL);

    p = sanitize_path(basename);
    SOL_NULL_CHECK(p, -ENOMEM);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        if (streq(dir->root, rootdir) &&
            streq(dir->basename, p)) {
            free(p);
            return -EINVAL;
        }
    }

    dir = sol_vector_append(&server->dirs);
    SOL_NULL_CHECK_GOTO(dir, err);

    dir->basename_len = strlen(p);
    dir->basename = p;
    dir->root = realpath(rootdir, NULL);
    SOL_NULL_CHECK_GOTO(dir->root, err_path);

    return 0;

err_path:
    if (sol_vector_del_last(&server->dirs) < 0)
        SOL_WRN("Could not remove %s/%s correctly",
            basename, rootdir);
err:
    free(p);
    return -ENOMEM;
}

SOL_API int
sol_http_server_remove_dir(struct sol_http_server *server, const char *basename, const char *rootdir)
{
    int r = -ENOMEM;
    uint16_t i;
    char *root = NULL, *p = NULL;
    struct static_dir *dir;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(rootdir, -EINVAL);
    SOL_NULL_CHECK(basename, -EINVAL);

    p = sanitize_path(basename);
    SOL_NULL_CHECK(p, -ENOMEM);

    root = realpath(rootdir, NULL);
    SOL_NULL_CHECK_GOTO(root, end);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        if (streq(dir->root, root) &&
            streq(dir->basename, p)) {
            free(dir->root);
            free(dir->basename);
            sol_vector_del(&server->dirs, i);
            r = 0;
            goto end;
        }
    }

    r = -ENODATA;
end:
    free(root);
    free(p);
    return r;
}

SOL_API int
sol_http_request_get_interface_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address)
{
    int r = 0;
    const union MHD_ConnectionInfo *info;

    union {
        struct sockaddr_in6 in6;
        struct sockaddr_in in4;
    } addr;
    socklen_t addrlen = sizeof(addr);

    SOL_NULL_CHECK(request, -EINVAL);
    SOL_NULL_CHECK(address, -EINVAL);

    info = MHD_get_connection_info(request->connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    SOL_NULL_CHECK(info, -EINVAL);

    r = getsockname(info->connect_fd, (struct sockaddr *)&addr, &addrlen);
    if (r < 0 || addrlen > sizeof(addr)) {
        SOL_WRN("Could not get the address for request: %s", request->url);
        return -EINVAL;
    }

    address->family = ((struct sockaddr *)&addr)->sa_family;
    switch (address->family) {
    case AF_INET:
        address->port = ntohs(addr.in4.sin_port);
        memcpy(&(address->addr.in), &addr.in4.sin_addr, sizeof(addr.in4.sin_addr));
        break;
    case AF_INET6:
        address->port = ntohs(addr.in6.sin6_port);
        memcpy(&(address->addr.in6), &addr.in6.sin6_addr, sizeof(addr.in6.sin6_addr));
        break;
    default:
        SOL_WRN("Unsupported family for request: %s", request->url);
        r = -EINVAL;
        break;
    }

    return r;
}

SOL_API int
sol_http_server_set_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error, const char *page)
{
    int r;
    uint16_t i;
    char *p;
    struct default_page *def;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(page, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i) {
        if (def->error == error) {
            SOL_WRN("There a page already registered for this error (%d)", error);
            return -EINVAL;
        }
    }

    p = realpath(page, NULL);
    SOL_NULL_CHECK(p, -errno);

    r = access(p, R_OK);
    if (r < 0) {
        r = -errno;
        SOL_WRN("Error on check file's permission: %s", sol_util_strerrora(errno));
        goto err;
    }

    r = -ENOMEM;
    def = sol_vector_append(&server->defaults);
    SOL_NULL_CHECK_GOTO(def, err);

    def->page = p;
    def->error = error;

    return 0;

err:
    free(p);
    return r;
}

SOL_API int
sol_http_server_remove_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error)
{
    uint16_t i;
    struct default_page *def;

    SOL_NULL_CHECK(server, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i) {
        if (def->error == error) {
            free(def->page);
            sol_vector_del(&server->defaults, i);
            return 0;
        }
    }

    return -ENODATA;
}
