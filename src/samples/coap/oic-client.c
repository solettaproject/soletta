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

#include <stdio.h>
#include <arpa/inet.h>

#include "sol-log.h"
#include "sol-mainloop.h"

#include "sol-oic-client.h"

static void
got_get_response(struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data)
{
    char addr[SOL_INET_ADDR_STRLEN];

    if (!sol_network_addr_to_str(cliaddr, addr, sizeof(addr))) {
        SOL_WRN("Could not convert network address to string");
        return;
    }

    printf("Received payload for GET from %s (%.*s): %.*s\n",
        addr, SOL_STR_SLICE_PRINT(*href), SOL_STR_SLICE_PRINT(*payload));
}

static void
found_resource(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data)
{
    struct sol_str_slice *slice;
    uint16_t idx;
    char addr[SOL_INET_ADDR_STRLEN];

    if (!sol_network_addr_to_str(&res->addr, addr, sizeof(addr))) {
        SOL_WRN("Could not convert network address to string");
        return;
    }

    printf("Found resource: coap://%s%.*s <observable: %s>\n", addr,
        SOL_STR_SLICE_PRINT(res->href), res->observable ? "yes" : "no");

    printf("Resource types:\n");
    SOL_VECTOR_FOREACH_IDX (&res->types, slice, idx)
        printf("\t\t%.*s\n", SOL_STR_SLICE_PRINT(*slice));

    printf("Resource interfaces:\n");
    SOL_VECTOR_FOREACH_IDX (&res->interfaces, slice, idx)
        printf("\t\t%.*s\n", SOL_STR_SLICE_PRINT(*slice));

    printf("Issuing GET %.*s on resource...\n", SOL_STR_SLICE_PRINT(res->href));
    sol_oic_client_resource_request(cli, res, SOL_COAP_METHOD_GET, NULL, 0,
        got_get_response, data);

    printf("\n");
}

int
main(int argc, char *argv[])
{
    struct sol_oic_client client;
    struct sol_network_link_addr cliaddr = { .family = AF_INET, .port = 5683 };
    const char *resource_type;

    sol_init();

    if (inet_pton(cliaddr.family, "224.0.1.187", &cliaddr.addr) < 0) {
        printf("could not convert multicast ip address to sockaddr_in\n");
        return 1;
    }

    client.server = sol_coap_server_new(0);

    if (argc < 2) {
        printf("No rt filter specified, assuming everything\n");
        resource_type = NULL;
    } else {
        printf("Finding resources with resource type %s\n", argv[1]);
        resource_type = argv[1];
    }

    sol_oic_client_find_resource(&client, &cliaddr, resource_type, found_resource, NULL);

    sol_run();

    return 0;
}
