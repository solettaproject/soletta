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

#include <stdio.h>

#include "sol-log.h"
#include "sol-mainloop.h"

#include "sol-oic-client.h"

static void
got_get_response(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *srv_addr, const struct sol_oic_map_reader *map_reader)
{
    struct sol_oic_repr_field field;
    enum sol_oic_map_loop_reason end_reason;
    struct sol_oic_map_reader iterator;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!srv_addr) {
        SOL_WRN("Response timeout");
        return;
    }

    if (!map_reader) {
        SOL_WRN("Empty Response");
        return;
    }

    if (!sol_network_link_addr_to_str(srv_addr, &addr)) {
        SOL_WRN("Could not convert network address to string");
        return;
    }

    printf("Dumping payload received from addr %.*s {\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));
    SOL_OIC_MAP_LOOP(map_reader, &field, &iterator, end_reason) {
        printf("\tkey: '%s', value: ", field.key);

        switch (field.type) {
        case SOL_OIC_REPR_TYPE_UINT:
            printf("uint(%" PRIu64 ")\n", field.v_uint);
            break;
        case SOL_OIC_REPR_TYPE_INT:
            printf("int(%" PRIi64 ")\n", field.v_int);
            break;
        case SOL_OIC_REPR_TYPE_SIMPLE:
            printf("simple(%d)\n", field.v_simple);
            break;
        case SOL_OIC_REPR_TYPE_TEXT_STRING:
            printf("str(%.*s)\n", (int)field.v_slice.len, field.v_slice.data);
            break;
        case SOL_OIC_REPR_TYPE_BYTE_STRING:
            printf("bytestr() [not dumping]\n");
            break;
        case SOL_OIC_REPR_TYPE_HALF_FLOAT:
            printf("hfloat(%p)\n", field.v_voidptr);
            break;
        case SOL_OIC_REPR_TYPE_FLOAT:
            printf("float(%f)\n", field.v_float);
            break;
        case SOL_OIC_REPR_TYPE_DOUBLE:
            printf("float(%g)\n", field.v_double);
            break;
        case SOL_OIC_REPR_TYPE_BOOL:
            printf("boolean(%s)\n", field.v_boolean ? "true" : "false");
            break;
        case SOL_OIC_REPR_TYPE_UNSUPPORTED:
            printf("\tkey: '%s', value: unsupported cbor code(%" PRIi64 ")\n",
                field.key, field.v_int);
            break;
        default:
            printf("unknown(%d)\n", field.type);
        }
    }
    printf("}\n\n");
}

static bool
found_resource(void *data,
    struct sol_oic_client *cli,
    struct sol_oic_resource *res)
{
    static const char digits[] = "0123456789abcdef";
    struct sol_str_slice *slice;
    struct sol_oic_request *request;
    uint16_t idx;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!res)
        return false;

    if (!sol_network_link_addr_to_str(&res->addr, &addr)) {
        SOL_WRN("Could not convert network address to string");
        return false;
    }

    printf("Found resource: coap://%.*s%.*s\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)),
        SOL_STR_SLICE_PRINT(res->path));

    printf("Flags:\n"
        " - observable: %s\n"
        " - secure: %s\n",
        res->observable ? "yes" : "no",
        res->secure ? "yes" : "no");

    printf("Device ID: ");
    for (idx = 0; idx < 16; idx++) {
        unsigned int digit = res->device_id.data[idx];
        putchar(digits[(digit >> 4) & 0x0f]);
        putchar(digits[digit & 0x0f]);
    }
    putchar('\n');

    printf("Resource types:\n");
    SOL_VECTOR_FOREACH_IDX (&res->types, slice, idx)
        printf("\t\t%.*s\n", SOL_STR_SLICE_PRINT(*slice));

    printf("Resource interfaces:\n");
    SOL_VECTOR_FOREACH_IDX (&res->interfaces, slice, idx)
        printf("\t\t%.*s\n", SOL_STR_SLICE_PRINT(*slice));

    printf("Issuing GET %.*s on resource...\n", SOL_STR_SLICE_PRINT(res->path));
    request = sol_oic_client_request_new(SOL_COAP_METHOD_GET, res);
    if (!request)
        return false;
    sol_oic_client_request(cli, request, got_get_response, data);

    printf("\n");

    return false;
}

int
main(int argc, char *argv[])
{
    struct sol_network_link_addr srv_addr =
    { .family = SOL_NETWORK_FAMILY_INET6,
      .port = 5683 };
    struct sol_oic_client *client;
    const char *resource_type;

    sol_init();

    if (argc < 2) {
        printf("Usage: %s <address> [resource_type]\n", argv[0]);
        return 0;
    }

    if (!strchr(argv[1], ':'))
        srv_addr.family = SOL_NETWORK_FAMILY_INET;

    if (!sol_network_link_addr_from_str(&srv_addr, argv[1])) {
        printf("Could not convert IP address to sockaddr_in\n");
        return 1;
    }

    client = sol_oic_client_new();

    if (argc < 3) {
        printf("No rt filter specified, assuming everything\n");
        resource_type = NULL;
    } else {
        printf("Finding resources with resource type %s\n", argv[2]);
        resource_type = argv[2];
    }

    sol_oic_client_find_resources(client, &srv_addr,
        resource_type, NULL, found_resource, NULL);

    sol_run();

    sol_oic_client_del(client);

    return 0;
}
