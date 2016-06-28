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
 * @brief Basic echo client
 *
 * This is echo client, the value given to the program is sent to the echo server
 * and the response is printed out.
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

static struct sol_socket *sock;
static struct sol_network_link_addr address = {
    .port = 7,
    .family = SOL_NETWORK_FAMILY_INET6,
};

static bool
on_can_read(void *data, struct sol_socket *s)
{
    ssize_t r;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_network_link_addr addr;

    r = sol_socket_recvmsg(s, &buffer, &addr);
    if (r < 0) {
        fprintf(stderr, "ERROR: Failed in receiving the message\n");
        goto err;
    }

    printf("Received: %.*s\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buffer)));
    sol_buffer_fini(&buffer);
    sol_quit_with_code(EXIT_SUCCESS);

    return true;

err:
    sol_buffer_fini(&buffer);
    sol_quit_with_code(EXIT_FAILURE);
    return false;
}

static bool
on_can_write(void *data, struct sol_socket *s)
{
    int r = -1;
    struct sol_buffer buffer = SOL_BUFFER_INIT_CONST(data, strlen(data));

    r = sol_socket_sendmsg(sock, &buffer, &address);
    sol_buffer_fini(&buffer);
    if (r < 0) {
        fprintf(stderr, "ERROR: Could not send data\n");
        sol_quit_with_code(EXIT_FAILURE);
        return false;
    }

    return false;
}

static void
startup_client(void)
{
    char **argv = sol_argv();
    char *addr = NULL;
    int c, opt_idx, argc = sol_argc();
    static const struct option opts[] = {
        { "address", required_argument, NULL, 'a' },
        { "port", required_argument, NULL, 'p' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };
    struct sol_socket_ip_options options = {
        .base  = {
            SOL_SET_API_VERSION(.api_version = SOL_SOCKET_OPTIONS_API_VERSION, )
            SOL_SET_API_VERSION(.sub_api = SOL_SOCKET_OPTIONS_API_VERSION, )
            .on_can_write = on_can_write,
            .on_can_read = on_can_read,
        },
        .family = SOL_NETWORK_FAMILY_INET6,
        .secure = false,
        .reuse_addr = true,
    };

    while ((c = getopt_long(argc, argv, "a:p:h", opts, &opt_idx)) != -1) {
        switch (c) {
        case 'a':
            addr = optarg;
            break;
        case 'p':
            address.port = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s -a <ip address> -p <address port> value\n", argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    if (!addr) {
        fprintf(stderr, "ERROR: No address was given\n");
        goto err;
    }

    options.base.data = argv[optind];
    if (!options.base.data) {
        fprintf(stderr, "ERROR: No data was given, use -h for help\n");
        goto err;
    }

    sock = sol_socket_ip_new(&options.base);
    if (!sock) {
        fprintf(stderr, "ERROR: Could not create the socket\n");
        goto err;
    }

    if (!sol_network_link_addr_from_str(&address, addr)) {
        fprintf(stderr, "ERROR: Could not convert the address: %s\n", addr);
        goto err;
    }

    sol_socket_set_read_monitor(sock, true);
    sol_socket_set_write_monitor(sock, true);
    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown_client(void)
{
    if (sock)
        sol_socket_del(sock);
}

SOL_MAIN_DEFAULT(startup_client, shutdown_client);
