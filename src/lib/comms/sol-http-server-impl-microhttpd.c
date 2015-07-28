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
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>

#include "sol-http-server.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

struct http_handler {
    char *path;
    int (*response_cb)(void *data, struct sol_http_response *response, const enum sol_http_method method,
        const struct sol_http_param *params);
    const void *user_data;
};

struct http_request {
    struct MHD_PostProcessor *pp;
    struct sol_http_param params;
    struct sol_http_param response_params;
    struct sol_buffer response_content;
};

struct sol_http_server {
    struct MHD_Daemon *daemon;
    struct sol_vector handlers;
    struct sol_vector fds;
    int refcnt;
};

struct http_connection {
    struct sol_fd *watch;
    int fd;
};

static struct MHD_Response *
build_mhd_response(const struct sol_http_response *response)
{
    uint16_t idx;
    struct MHD_Response *r;
    struct sol_http_param_value *value;

    r = MHD_create_response_from_buffer(response->content.used, response->content.data,
        MHD_RESPMEM_PERSISTENT);
    if (!r)
        return NULL;

    SOL_HTTP_PARAM_FOREACH_IDX(&response->param, value, idx) {
        int ret;
        char buffer[512] = { 0 };

        switch (value->type) {
        case SOL_HTTP_PARAM_HEADER:
            if (MHD_add_response_header(r, value->value.key_value.key, value->value.key_value.value) == MHD_NO) {
                SOL_WRN("Could not add the header: %s", value->value.key_value.key);
                goto err;
            }
            break;
        case SOL_HTTP_PARAM_COOKIE:
            ret = snprintf(buffer, sizeof(buffer) - 1, "%s=%s", value->value.key_value.key, value->value.key_value.value);
            if (ret < 0 || ret >= (int)sizeof(buffer))
                goto err;
            if (MHD_add_response_header(r, MHD_HTTP_HEADER_SET_COOKIE, buffer) == MHD_NO) {
                SOL_WRN("Could not add the cookie: %s", value->value.key_value.key);
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

    return r;

err:
    MHD_destroy_response(r);
    return NULL;
}

static int
post_iterator(void *data, enum MHD_ValueKind kind, const char *key,
    const char *filename, const char *type,
    const char *encoding, const char *value, uint64_t off, size_t size)
{

    struct http_request *request = data;
    char *v, *k;

    if (!size)
        return MHD_NO;

    v = strdup(value);
    SOL_NULL_CHECK(v, MHD_NO);

    k = strdup(key);
    SOL_NULL_CHECK_GOTO(k, err);

    if (!sol_http_param_add(&request->params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD(k, v))) {
        SOL_WRN("Could not add %s key", key);
        goto param_err;
    }

    return MHD_YES;

param_err:
    free(k);
err:
    free(v);
    return MHD_NO;
}

static int
headers_iterator(void *data, enum MHD_ValueKind kind, const char *key, const char *value)
{
    struct http_request *request = data;
    char *v, *k;

    v = strdup(value);
    SOL_NULL_CHECK(v, MHD_NO);

    k = strdup(key);
    SOL_NULL_CHECK_GOTO(k, err);

    switch (kind) {
    case MHD_HEADER_KIND:
        if (!sol_http_param_add(&request->params,
            SOL_HTTP_REQUEST_PARAM_HEADER(k, v)))
            goto param_err;
        break;
    case MHD_COOKIE_KIND:
        if (!sol_http_param_add(&request->params,
            SOL_HTTP_REQUEST_PARAM_COOKIE(k, v)))
            goto err;
        break;
    default:
        goto param_err;
    }

    return MHD_YES;

param_err:
    free(k);
err:
    free(v);
    return MHD_NO;
}

static int
http_server_handler(void *data, struct MHD_Connection *connection, const char *url, const char *method,
    const char *version, const char *upload_data, size_t *upload_data_size, void **ptr)
{
    int ret;
    uint16_t i;
    struct sol_http_server *server = data;
    struct http_handler *handler;
    struct MHD_Response *mhd_response;
    enum sol_http_method sol_method;
    struct http_request *req = *ptr;
    struct sol_http_response response = {
        .api_version = SOL_HTTP_RESPONSE_API_VERSION,
        .url = url,
        .content = SOL_BUFFER_INIT_EMPTY,
        .param = SOL_HTTP_REQUEST_PARAM_INIT
    };

    if (!req) {
        req = calloc(1, sizeof(*req));
        SOL_NULL_CHECK(req, MHD_NO);

        sol_http_param_init(&req->params);
        sol_http_param_init(&req->response_params);

        *ptr = req;
        return MHD_YES;
    }

    if (streq(method, "POST")) {
        sol_method = SOL_HTTP_METHOD_POST;
        if (!req->pp)
            req->pp  = MHD_create_post_processor(connection, 1024, post_iterator, req);
        SOL_NULL_CHECK_GOTO(req->pp, end);

        MHD_post_process(req->pp, upload_data, *upload_data_size);
        if (*upload_data_size) {
            *upload_data_size = 0;
            return MHD_YES;
        }
    } else {
        sol_method = SOL_HTTP_METHOD_HEAD;
    }

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (!streq(url, handler->path))
            continue;

        MHD_get_connection_values(connection, MHD_HEADER_KIND, headers_iterator, req);
        SOL_INT_CHECK_GOTO(handler->response_cb((void *)handler->user_data, &response, sol_method, &req->params), < 0, end);
        mhd_response = build_mhd_response(&response);

        req->response_content = response.content;
        req->response_params = response.param;
        SOL_NULL_CHECK_GOTO(mhd_response, end);

        ret = MHD_queue_response(connection, response.response_code, mhd_response);
        MHD_destroy_response(mhd_response);
        return ret;
    }

end:
    return MHD_NO;
}

static bool
connection_watch_cb(void *data, int fd, unsigned int flags)
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

    MHD_run_from_select(server->daemon, &rs, &ws, &es);

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
    SOL_NULL_CHECK(connection);

    conn->fd = (int)info->connect_fd;
    conn->watch = sol_fd_add(conn->fd, fd_flags, connection_watch_cb, server);

    if (!conn->watch) {
        SOL_WRN("Could not create the fd watch");
        sol_vector_del(&server->fds, server->fds.len - 1);
    }
}

static void
notify_connection_finished_cb(void *data, struct MHD_Connection *connection,
    void **con_data, enum MHD_RequestTerminationCode code)
{
    struct http_request *request = *con_data;
    struct sol_http_param_value *value;
    uint16_t idx;

    if (!request)
        return;

    if (request->pp)
        MHD_destroy_post_processor(request->pp);

    SOL_HTTP_PARAM_FOREACH_IDX(&request->params, value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_POST_FIELD:
        case SOL_HTTP_PARAM_HEADER:
        case SOL_HTTP_PARAM_COOKIE:
            free((char *)value->value.key_value.value);
            free((char *)value->value.key_value.key);
            break;
        default:
            break;
        }
    }

    sol_buffer_fini(&request->response_content);
    sol_http_param_free(&request->params);
    sol_http_param_free(&request->response_params);
    free(request);
}

SOL_API struct sol_http_server *
sol_http_server_new(uint16_t port)
{
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    const union MHD_DaemonInfo *info;
    int listen_fd;
    struct sol_http_server *server;

    server = calloc(1, sizeof(*server));
    SOL_NULL_CHECK(server, NULL);

    sol_vector_init(&server->handlers, sizeof(struct http_handler));
    sol_vector_init(&server->fds, sizeof(struct http_connection));

    server->daemon = MHD_start_daemon(MHD_NO_FLAG,
        port, NULL, NULL,
        http_server_handler, server,
        MHD_OPTION_NOTIFY_CONNECTION, notify_connection_cb, server,
        MHD_OPTION_NOTIFY_COMPLETED, notify_connection_finished_cb, server,
        MHD_OPTION_END);
    SOL_NULL_CHECK_GOTO(server->daemon, err_daemon);

    info = MHD_get_daemon_info(server->daemon, MHD_DAEMON_INFO_LISTEN_FD);
    SOL_NULL_CHECK_GOTO(info, err);

    listen_fd = (int)info->listen_fd;
    if (!sol_fd_add(listen_fd, fd_flags, connection_watch_cb, server))
        goto err;

    server->refcnt++;

    return server;

err:
    MHD_stop_daemon(server->daemon);
    server->daemon = NULL;
err_daemon:
    free(server);
    return NULL;
}

SOL_API struct sol_http_server *
sol_http_server_ref(struct sol_http_server *server)
{
    SOL_NULL_CHECK(server, NULL);
    errno = ENOMEM;
    SOL_INT_CHECK(server->refcnt, == UINT16_MAX, NULL);
    errno = 0;
    server->refcnt++;
    return server;
}

SOL_API void
sol_http_server_unref(struct sol_http_server *server)
{
    uint16_t i;
    struct http_handler *handler;
    struct http_connection *connection;

    SOL_NULL_CHECK(server);
    SOL_INT_CHECK(server->refcnt, <= 0);

    server->refcnt--;
    if (server->refcnt)
        return;

    MHD_stop_daemon(server->daemon);
    server->daemon = NULL;
    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i)
        free(handler->path);

    sol_vector_clear(&server->handlers);

    SOL_VECTOR_FOREACH_IDX (&server->fds, connection, i)
        sol_fd_del(connection->watch);

    sol_vector_clear(&server->fds);

    free(server);
}

SOL_API int
sol_http_server_register_handler(struct sol_http_server *server, const char *path,
    int (*response_cb)(void *data, struct sol_http_response *response,
    const enum sol_http_method method, const struct sol_http_param *params),
    const void *data)
{
    uint16_t i;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_INT_CHECK(server->refcnt, <= 0, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(response_cb, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (streq(handler->path, path))
            return -EINVAL;
    }

    handler = sol_vector_append(&server->handlers);
    SOL_NULL_CHECK(handler, -ENOMEM);

    handler->path = strdup(path);
    SOL_NULL_CHECK_GOTO(handler->path, error);

    handler->response_cb = response_cb;
    handler->user_data = data;

    return 0;

error:
    sol_vector_del(&server->handlers, server->handlers.len - 1);
    return -ENOMEM;
}

SOL_API void
sol_http_server_unregister_handler(struct sol_http_server *server, const char *path)
{
    uint16_t i;
    struct http_handler *handler;

    SOL_NULL_CHECK(server);
    SOL_NULL_CHECK(path);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (streq(handler->path, path)) {
            free(handler->path);
            sol_vector_del(&server->handlers, i);
            return;
        }
    }
}
