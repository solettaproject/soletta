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

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-unix-socket-impl");

#include "unix-socket.h"
#include "sol-mainloop.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

struct client_data {
    struct sol_fd *watch;
    int sock;
};

struct unix_socket {
    struct sol_fd *watch;
    void (*data_read_cb)(void *data, int fd);
    void (*del)(struct unix_socket *un_socket);
    int (*write)(struct unix_socket *un_socket, const void *data, size_t count);
    const void *data;
    int sock;
};

struct unix_socket_server {
    struct unix_socket base;
    struct sol_vector clients;
    struct sockaddr_un local;
};

static int
socket_write(int fd, const void *data, size_t count)
{
    unsigned int attempts = SOL_UTIL_MAX_READ_ATTEMPTS;
    size_t amount = 0;
    ssize_t w;

    while (attempts && (amount < count)) {
        w = write(fd, (char *)data + amount, count - amount);
        if (w < 0) {
            attempts--;
            if ((errno == EINTR) || (errno == EAGAIN))
                continue;
            else
                return -1;
        }

        amount += w;
    }

    return amount;
}

static bool
on_client_data(void *data, int fd, uint32_t cond)
{
    struct unix_socket *un_socket = data;

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        SOL_WRN("Error with the monitor, probably the socket has closed");
        goto err;
    }

    if (un_socket->data_read_cb) {
        un_socket->data_read_cb((void *)un_socket->data, fd);
    }

    return true;

err:
    un_socket->watch = NULL;
    return false;
}

static bool
on_server_data(void *data, int fd, uint32_t cond)
{
    struct unix_socket_server *server = data;
    struct client_data *c;
    uint16_t i;

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        SOL_VECTOR_FOREACH_IDX (&server->clients, c, i) {
            if (c->sock == fd) {
                close(c->sock);
                sol_vector_del(&server->clients, i);
                return false;
            }
        }
    }

    if (server->base.data_read_cb)
        server->base.data_read_cb((void *)server->base.data, fd);

    return true;
}

static bool
on_server_connect(void *data, int fd, uint32_t cond)
{
    struct unix_socket_server *server = data;
    struct client_data *c = sol_vector_append(&server->clients);
    struct sockaddr_un client;
    socklen_t len;

    SOL_NULL_CHECK(c, false);

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        SOL_WRN("Error with the monitor");
        goto err;
    }

    len = sizeof(client);

#ifdef HAVE_ACCEPT4
    c->sock = accept4(server->base.sock, (struct sockaddr *)&client, &len, SOCK_CLOEXEC);
#else
    c->sock = accept(server->base.sock, (struct sockaddr *)&client, &len);
#endif
    if (c->sock < 0) {
        SOL_WRN("Error on accept %s", sol_util_strerrora(errno));
        goto err;
    }

#ifndef HAVE_ACCEPT4
    /* we need to set the FD_CLOEXEC flag */
    {
        int ret = fcntl(c->sock, F_GETFD);
        if (ret >= 0)
            ret = fcntl(c->sock, F_SETFD, ret | FD_CLOEXEC);
        if (ret < 0) {
            SOL_WRN("Error on setting FD_CLOEXEC flag on socket: %s", sol_util_strerrora(errno));
            goto err_socket;
        }
    }
#endif

    c->watch = sol_fd_add(c->sock, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP,
        on_server_data, server);
    if (!c->watch) {
        SOL_WRN("Failed in create the watch descriptor");
        goto err_socket;
    }

    return true;

err_socket:
    close(c->sock);
err:
    sol_vector_del(&server->clients, server->clients.len);
    return false;
}

static int
client_write(struct unix_socket *client, const void *data, size_t count)
{
    if (socket_write(client->sock, data, count) < 0) {
        SOL_WRN("Failed to write on (%d): %s", client->sock, sol_util_strerrora(errno));
        return -1;
    }

    return 0;
}

static void
client_del(struct unix_socket *un_socket)
{
    if (un_socket->watch)
        sol_fd_del(un_socket->watch);
    close(un_socket->sock);
    free(un_socket);
}

struct unix_socket *
unix_socket_client_new(const void *data, const char *socket_path, void (*data_read_cb)(void *data, int fd))
{
    struct sockaddr_un local;
    struct unix_socket *client = calloc(1, sizeof(*client));
    int n = SOCK_STREAM;

#ifdef SOCK_CLOEXEC
    n |= SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif

    SOL_NULL_CHECK(client, NULL);

    client->data = data;
    client->data_read_cb = data_read_cb;

    client->sock = socket(AF_UNIX, n, 0);
    if (client->sock < 0) {
        SOL_WRN("Failed to create the socket %s", sol_util_strerrora(errno));
        goto err;
    }

#ifndef SOCK_CLOEXEC
    /* We need to set the socket to FD_CLOEXEC and non-blocking mode */
    if (!sol_fd_add_flags(client->sock, FD_CLOEXEC | O_NONBLOCK)) {
        SOL_WRN("Failed to set the socket to FD_CLOEXEC or O_NONBLOCK, %s",
            sol_util_strerrora(errno));
        goto sock_err;
    }
#endif

    local.sun_family = AF_UNIX;
    if (strncpy(local.sun_path, socket_path, sizeof(local.sun_path) - 1) == NULL) {
        SOL_WRN("Falied to copy the string, %s", sol_util_strerrora(errno));
        goto sock_err;
    }

    if (connect(client->sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        SOL_WRN("Could not connect: %s", sol_util_strerrora(errno));
        goto sock_err;
    }

    client->watch = sol_fd_add(client->sock, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP,
        on_client_data, client);

    client->write = client_write;
    client->del = client_del;
    return client;

sock_err:
    close(client->sock);
err:
    free(client);
    return NULL;
}

static int
server_write(struct unix_socket *un_socket, const void *data, size_t count)
{
    struct unix_socket_server *server = (struct unix_socket_server *)un_socket;
    struct client_data *c;
    uint16_t i;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&server->clients, c, i) {
        if (socket_write(c->sock, data, count) < (ssize_t)count) {
            SOL_WRN("Failed to write on (%d): %s", c->sock, sol_util_strerrora(errno));
            sol_fd_del(c->watch);
            close(c->sock);
            sol_vector_del(&server->clients, i);
        }
    }

    return 0;
}

static void
server_del(struct unix_socket *un_socket)
{
    struct unix_socket_server *server = (struct unix_socket_server *)un_socket;
    struct client_data *c;
    uint16_t i;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&server->clients, c, i) {
        sol_fd_del(c->watch);
        close(c->sock);
        sol_vector_del(&server->clients, i);
    }

    sol_vector_clear(&server->clients);
    unlink(server->local.sun_path);
    close(server->base.sock);
    free(server);
}

struct unix_socket *
unix_socket_server_new(const void *data, const char *socket_path, void (*data_read_cb)(void *data, int fd))
{
    struct unix_socket_server *server;
    int n = SOCK_STREAM;

#ifdef SOCK_CLOEXEC
    n |= SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif

    SOL_NULL_CHECK(socket_path, NULL);

    server = calloc(1, sizeof(*server));
    SOL_NULL_CHECK(server, NULL);

    server->base.data = data;
    server->base.data_read_cb = data_read_cb;
    sol_vector_init(&server->clients, sizeof(struct client_data));

    server->base.sock = socket(AF_UNIX, n, 0);
    if (server->base.sock < 0) {
        SOL_WRN("Failed to create the socket %s", sol_util_strerrora(errno));
        goto err;
    }

#ifndef SOCK_CLOEXEC
    /* We need to set the socket to FD_CLOEXEC and non-blocking mode */
    n = fcntl(server->base.sock, F_GETFD);
    if (n >= 0)
        n = fcntl(server->base.sock, F_SETFD, n | FD_CLOEXEC);
    if (n >= 0)
        n = sol_util_fd_set_flag(server->base.sock, O_NONBLOCK);
    if (n < 0) {
        SOL_WRN("Failed to set the socket to FD_CLOEXEC or O_NONBLOCK, %s", sol_util_strerrora(errno));
        goto sock_err;
    }
#endif

    server->local.sun_family = AF_UNIX;
    if (strncpy(server->local.sun_path, socket_path, sizeof(server->local.sun_path) - 1) == NULL) {
        SOL_WRN("Falied to copy the string, %s", sol_util_strerrora(errno));
        goto sock_err;
    }

    if (bind(server->base.sock, (struct sockaddr *)&server->local, sizeof(server->local)) < 0) {
        SOL_WRN("Failed to bind the socket %s", sol_util_strerrora(errno));
        goto sock_err;
    }

    if (listen(server->base.sock, SOMAXCONN) < 0) {
        SOL_WRN("Failed to listen the socket %s", sol_util_strerrora(errno));
        goto sock_err;
    }

    server->base.watch = sol_fd_add(server->base.sock, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP,
        on_server_connect, server);
    server->base.write = server_write;
    server->base.del = server_del;
    return (struct unix_socket *)server;

sock_err:
    close(server->base.sock);
err:
    free(server);
    return NULL;
}

int
unix_socket_write(struct unix_socket *un_socket, const void *data, size_t count)
{
    SOL_NULL_CHECK(un_socket, -EINVAL);
    SOL_NULL_CHECK(data, -EINVAL);
    SOL_INT_CHECK(un_socket->sock, < 0, -EINVAL);

    return un_socket->write(un_socket, data, count);
}

void
unix_socket_del(struct unix_socket *un_socket)
{
    SOL_NULL_CHECK(un_socket);

    un_socket->del(un_socket);
}
