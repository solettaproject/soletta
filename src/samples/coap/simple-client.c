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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-str-slice.h"
#include "sol-vector.h"

#include "sol-coap.h"

#define DEFAULT_UDP_PORT 5683

static void
disable_observing(struct sol_coap_packet *req, struct sol_coap_server *server,
    struct sol_str_slice path[], const struct sol_network_link_addr *cliaddr)
{
    struct sol_coap_packet *pkt = sol_coap_packet_new(req);
    uint8_t observe = 1;
    int i, r;

    if (!pkt)
        return;

    r = sol_coap_header_set_code(pkt, SOL_COAP_METHOD_GET);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_coap_header_set_type(pkt, SOL_COAP_MESSAGE_TYPE_CON);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_OBSERVE, &observe, sizeof(observe));
    SOL_INT_CHECK_GOTO(r, < 0, err);

    for (i = 0; path[i].data; i++)
        sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, path[i].data, path[i].len);

    sol_coap_send_packet(server, pkt, cliaddr);

    SOL_INF("Disabled observing");

    return;

err:
    sol_coap_packet_unref(pkt);
}

static bool
reply_cb(void *data, struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_str_slice *path = data;
    static int count;
    struct sol_buffer *buf;
    size_t offset;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!req || !cliaddr) //timeout
        return false;

    sol_network_link_addr_to_str(cliaddr, &addr);

    SOL_INF("Got response from %.*s\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));

    sol_coap_packet_get_payload(req, &buf, &offset);
    SOL_INF("Payload: %.*s\n", (int)(buf->used - offset),
        (char *)sol_buffer_at(buf, offset));

    if (++count == 10)
        disable_observing(req, server, path, cliaddr);

    return true;
}

int
main(int argc, char *argv[])
{
    struct sol_coap_server *server;
    struct sol_str_slice *path;
    struct sol_network_link_addr cliaddr = { };
    struct sol_coap_packet *req;
    uint8_t observe = 0;
    int i, r;
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = 0 };

    uint8_t token[4] = { 0x41, 0x42, 0x43, 0x44 };

    sol_init();

    if (argc < 3) {
        printf("Usage: %s <address> <path> [path]\n", argv[0]);
        return 0;
    }

    server = sol_coap_server_new(&servaddr, false);
    if (!server) {
        SOL_WRN("Could not create a coap server.");
        return -1;
    }

    req = sol_coap_packet_new_request(SOL_COAP_METHOD_GET, SOL_COAP_MESSAGE_TYPE_CON);
    if (!req) {
        SOL_WRN("Could not make a GET request to resource %s", argv[2]);
        return -1;
    }

    r = sol_coap_header_set_token(req, token, sizeof(token));
    if (r < 0) {
        SOL_WRN("Could not set coap header token.");
        goto err;
    }

    path = calloc(argc - 1, sizeof(*path));
    if (!path) {
        SOL_WRN("Could not allocate the path");
        goto err;
    }

    for (i = 2; i < argc; i++) {
        path[i - 2] = sol_str_slice_from_str(argv[i]);
    }

    sol_coap_add_option(req, SOL_COAP_OPTION_OBSERVE, &observe, sizeof(observe));

    for (i = 0; path[i].data; i++)
        sol_coap_add_option(req, SOL_COAP_OPTION_URI_PATH, path[i].data, path[i].len);

    cliaddr.family = SOL_NETWORK_FAMILY_INET6;
    if (!sol_network_link_addr_from_str(&cliaddr, argv[1])) {
        SOL_WRN("%s is an invalid IPv6 address", argv[1]);
        goto err_addr;
    }

    cliaddr.port = DEFAULT_UDP_PORT;

    /* Takes the ownership of 'req'. */
    sol_coap_send_packet_with_reply(server, req, &cliaddr, reply_cb, path);

    sol_run();

    sol_coap_server_unref(server);
    free(path);

    return 0;

err_addr:
    free(path);
err:
    sol_coap_server_unref(server);
    return -1;
}
