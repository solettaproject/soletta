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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-unix-socket-impl");

#include "unix-socket.h"
#include "sol-mainloop.h"
#include "sol-missing.h"
#include "sol-util.h"
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
on_client_data(void *data, int fd, unsigned int cond)
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
on_server_data(void *data, int fd, unsigned int cond)
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
on_server_connect(void *data, int fd, unsigned int cond)
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

    c->sock = accept4(server->base.sock, (struct sockaddr *)&client, &len, SOCK_CLOEXEC);
    if (c->sock < 0) {
        SOL_WRN("Error on accept %s", sol_util_strerrora(errno));
        goto err;
    }

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

    SOL_NULL_CHECK(client, NULL);

    client->data = data;
    client->data_read_cb = data_read_cb;

    client->sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (client->sock < 0) {
        SOL_WRN("Failed to create the socket %s", sol_util_strerrora(errno));
        goto err;
    }

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

    SOL_VECTOR_FOREACH_IDX (&server->clients, c, i) {
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

    SOL_VECTOR_FOREACH_IDX (&server->clients, c, i) {
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

    SOL_NULL_CHECK(socket_path, NULL);

    server = calloc(1, sizeof(*server));
    SOL_NULL_CHECK(server, NULL);

    server->base.data = data;
    server->base.data_read_cb = data_read_cb;
    sol_vector_init(&server->clients, sizeof(struct client_data));

    server->base.sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (server->base.sock < 0) {
        SOL_WRN("Failed to create the socket %s", sol_util_strerrora(errno));
        goto err;
    }

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
