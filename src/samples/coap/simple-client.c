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
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-coap.h"

#define DEFAULT_UDP_PORT 5683

static void
disable_observing(struct sol_coap_packet *req, struct sol_coap_server *server,
    struct sol_str_slice path[], const struct sol_network_link_addr *cliaddr)
{
    struct sol_coap_packet *pkt = sol_coap_packet_new(req);
    uint8_t observe = 1;
    int i;

    if (!pkt)
        return;

    sol_coap_header_set_code(pkt, SOL_COAP_METHOD_GET);
    sol_coap_header_set_type(pkt, SOL_COAP_TYPE_CON);

    sol_coap_add_option(pkt, SOL_COAP_OPTION_OBSERVE, &observe, sizeof(observe));

    for (i = 0; path[i].data; i++)
        sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, path[i].data, path[i].len);

    sol_coap_send_packet(server, pkt, cliaddr);

    SOL_INF("Disabled observing");
}

static bool
reply_cb(struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_str_slice *path = data;
    static int count;
    char addr[SOL_INET_ADDR_STRLEN];
    uint8_t *payload;
    uint16_t len;

    if (!req || !cliaddr) //timeout
        return false;

    sol_network_addr_to_str(cliaddr, addr, sizeof(addr));

    SOL_INF("Got response from %s", addr);

    sol_coap_packet_get_payload(req, &payload, &len);
    SOL_INF("Payload: %.*s", len, payload);

    if (++count == 10)
        disable_observing(req, server, path, cliaddr);

    return false;
}

int
main(int argc, char *argv[])
{
    struct sol_coap_server *server;
    struct sol_str_slice *path;
    struct sol_network_link_addr cliaddr = { };
    struct sol_coap_packet *req;
    uint8_t observe = 0;
    int i;

    uint8_t token[4] = { 0x41, 0x42, 0x43, 0x44 };

    sol_init();

    if (argc < 3) {
        SOL_INF("Usage: %s <address> <path> [path]\n", argv[0]);
        return 0;
    }

    server = sol_coap_server_new(0);
    if (!server) {
        SOL_WRN("Could not create a coap server.");
        return -1;
    }

    req = sol_coap_packet_request_new(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_CON);
    if (!req) {
        SOL_WRN("Could not make a GET request to resource %s", argv[2]);
        return -1;
    }

    sol_coap_header_set_token(req, token, sizeof(token));

    path = calloc(argc - 1, sizeof(*path));
    if (!path) {
        sol_coap_packet_unref(req);
        return -1;
    }

    for (i = 2; i < argc; i++) {
        path[i - 2] = sol_str_slice_from_str(argv[i]);
    }

    sol_coap_add_option(req, SOL_COAP_OPTION_OBSERVE, &observe, sizeof(observe));

    for (i = 0; path[i].data; i++)
        sol_coap_add_option(req, SOL_COAP_OPTION_URI_PATH, path[i].data, path[i].len);

    cliaddr.family = AF_INET;
    if (!sol_network_addr_from_str(&cliaddr, argv[1])) {
        SOL_WRN("%s is an invalid IPv4 address", argv[1]);
        free(path);
        sol_coap_packet_unref(req);
        return -1;
    }

    cliaddr.port = DEFAULT_UDP_PORT;

    /* Takes the ownership of 'req'. */
    sol_coap_send_packet_with_reply(server, req, &cliaddr, reply_cb, path);

    sol_run();

    sol_coap_server_unref(server);
    free(path);

    return 0;
}
