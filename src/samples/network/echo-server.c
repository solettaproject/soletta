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

/**
 * @file
 * @brief Basic echo server
 *
 * Basic echo server. All data received is sent back.
 * To see the usage help, -h or --help.
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "soletta.h"
#include "sol-buffer.h"
#include "sol-socket.h"
#include "sol-network.h"
#include "sol-vector.h"
#include "sol-util.h"

struct queue_item {
    struct sol_buffer buf;
    struct sol_network_link_addr addr;
};

static struct sol_socket *sock;
static struct sol_vector queue;

static bool
on_can_read(void *data, struct sol_socket *s)
{
    ssize_t r;
    struct queue_item *item;

    item = sol_vector_append(&queue);
    if (!item) {
        fprintf(stderr, "ERROR: Could not allocate memory for an element\n");
        goto err;
    }

    sol_buffer_init(&item->buf);
    r = sol_socket_recvmsg(s, &item->buf, &item->addr);
    if (r < 0) {
        fprintf(stderr, "ERROR: Failed in receiving the message\n");
        goto err;
    }

    sol_socket_set_write_monitor(sock, true);
    return true;

err:
    sol_quit_with_code(EXIT_FAILURE);
    return false;
}

static bool
on_can_write(void *data, struct sol_socket *s)
{
    int r = -1;
    struct queue_item *item = sol_vector_get(&queue, 0);

    if (!item) {
        fprintf(stderr, "ERROR: Could not take the vector's element\n");
        return false;
    }

    r = sol_socket_sendmsg(sock, &item->buf, &item->addr);
    if (r < 0) {
        fprintf(stderr, "ERROR: Could not send data\n");
        goto end;
    }

    r = 0;

end:
    sol_buffer_fini(&item->buf);
    sol_vector_del(&queue, 0);
    if (!queue.len)
        sol_socket_set_write_monitor(sock, false);
    return (r < 0) ? false : true;
}

static void
startup_server(void)
{
    char **argv = sol_argv();
    int c, opt_idx, argc = sol_argc();
    static const struct option opts[] = {
        { "port", required_argument, NULL, 'p' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };
    struct sol_socket_ip_options options = {
        .base = {
            SOL_SET_API_VERSION(.api_version = SOL_SOCKET_OPTIONS_API_VERSION, )
            SOL_SET_API_VERSION(.sub_api = SOL_SOCKET_OPTIONS_API_VERSION, )
            .on_can_write = on_can_write,
            .on_can_read = on_can_read,
        },
        .family = SOL_NETWORK_FAMILY_INET6,
        .secure = false,
        .reuse_addr = true,
    };
    struct sol_network_link_addr addr = {
        .port = 7,
        .family = SOL_NETWORK_FAMILY_INET6,
    };

    while ((c = getopt_long(argc, argv, "p:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'p':
            addr.port = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-p <port to bind>]\n"
                "\tIf any port is given a random port will be monitored\n", argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    sol_vector_init(&queue, sizeof(struct queue_item));

    sock = sol_socket_ip_new(&options.base);
    if (!sock) {
        fprintf(stderr, "ERROR: Could not create the socket\n");
        goto err;
    }

    c = sol_socket_bind(sock, &addr);
    if (c < 0) {
        fprintf(stderr, "ERROR: Could not bind the socket (%s)\n", sol_util_strerrora(errno));
        goto err;
    }

    sol_socket_set_read_monitor(sock, true);
    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown_server(void)
{
    uint16_t i;
    struct queue_item *item;

    if (sock)
        sol_socket_del(sock);

    SOL_VECTOR_FOREACH_IDX (&queue, item, i) {
        sol_buffer_fini(&item->buf);
    }
    sol_vector_clear(&queue);
}

SOL_MAIN_DEFAULT(startup_server, shutdown_server);
