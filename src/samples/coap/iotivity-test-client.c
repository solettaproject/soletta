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

/*
 * Soletta x Iotivity client sample.
 *
 * OIC Client using soletta library similar to occlient sample available at
 * iotivity repository. It can be use to test compatibility between Soletta
 * and Iotivity.
 *
 * iotivity-test-client is suppose to work with ocserver sample, available at
 * iotivity repository. Also working with src/samples/flow/oic/light-server.fbp
 * soletta sample.
 */
#include <stdio.h>
#include <arpa/inet.h>

#include "sol-log.h"
#include "sol-buffer.h"
#include "sol-coap.h"
#include "sol-mainloop.h"
#include "sol-oic-client.h"

#define DEVICE_ID_LEN (16)
#define streq(a, b) (strcmp((a), (b)) == 0)
#define POST_REQUEST_POWER (13)
#define PUT_REQUEST_POWER (7)

enum test_number_codes {
    TEST_DISCOVERY = 1,
    TEST_NON_CONFIRMABLE_GET = 2,
    TEST_NON_CONFIRMABLE_PUT = 4,
    TEST_NON_CONFIRMABLE_POST = 5,
    TEST_NON_CONFIRMABLE_DELETE = 6,
    TEST_NON_CONFIRMABLE_OBSERVE = 7,
    TEST_NON_CONFIRMABLE_INVALID_GET = 8,
    TEST_CONFIRMABLE_GET = 9,
    TEST_CONFIRMABLE_POST = 10,
    TEST_CONFIRMABLE_DELETE = 11,
    TEST_CONFIRMABLE_OBSERVE = 12,
    TEST_DISCOVER_PLATFORM = 19,
    TEST_DISCOVER_DEVICES = 20,
};

struct Context {
    int test_number;
    struct sol_oic_resource *res;
};

static bool found_resource(void *data, struct sol_oic_client *cli, struct sol_oic_resource *res);

static void
device_id_decode(const char *device_id_encoded, char *device_id)
{
    static const char digits[] = "0123456789abcdef";
    uint16_t idx;

    for (idx = 0; idx < DEVICE_ID_LEN; idx++) {
        unsigned int digit = device_id_encoded[idx];
        device_id[idx * 2] = digits[(digit >> 4) & 0x0f];
        device_id[idx * 2 + 1] = digits[digit & 0x0f];
    }
}

static bool
found_resource_print(void *data,
    struct sol_oic_client *cli,
    struct sol_oic_resource *res)
{
    struct sol_str_slice *slice;
    uint16_t idx;
    char device_id[DEVICE_ID_LEN * 2];
    static bool resource_found = false;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!res) {
        if (!resource_found) {
            printf("No resource found\n");
            sol_quit_with_code(EXIT_FAILURE);
        } else
            sol_quit();
        return false;
    }

    if (res->device_id.len < DEVICE_ID_LEN) {
        printf("Invalid device id\n");
        return false;
    }

    if (!sol_network_link_addr_to_str(&res->addr, &addr)) {
        printf("Could not convert network address to string\n");
        return false;
    }

    resource_found = true;
    printf("Found resource: coap://%.*s%.*s\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)),
        SOL_STR_SLICE_PRINT(res->path));

    printf("Flags:\n");
    printf(" - observable: %s\n", res->observable ? "yes" : "no");
    printf(" - secure: %s\n", res->secure ? "yes" : "no");

    device_id_decode(res->device_id.data, device_id);
    printf("Device ID: %.*s\n", DEVICE_ID_LEN * 2, device_id);

    printf("Resource types:\n");
    SOL_VECTOR_FOREACH_IDX (&res->types, slice, idx)
        printf("\t\t%.*s\n", SOL_STR_SLICE_PRINT(*slice));

    printf("Resource interfaces:\n");
    SOL_VECTOR_FOREACH_IDX (&res->interfaces, slice, idx)
        printf("\t\t%.*s\n", SOL_STR_SLICE_PRINT(*slice));
    printf("\n");

    return true;
}

static void
fill_info(const struct sol_oic_map_reader *map_reader, bool *state, int32_t *power)
{
    struct sol_oic_repr_field field;
    enum sol_oic_map_loop_reason end_reason;
    struct sol_oic_map_reader iterator;

    SOL_OIC_MAP_LOOP(map_reader, &field, &iterator, end_reason) {
        if (state && streq(field.key, "state") &&
            field.type == SOL_OIC_REPR_TYPE_BOOL) {
            *state = field.v_boolean;
            continue;
        }
        if (power && streq(field.key, "power")) {
            if (field.type == SOL_OIC_REPR_TYPE_UINT)
                *power = field.v_uint;
            else if (field.type == SOL_OIC_REPR_TYPE_INT)
                *power = field.v_int;
            else if (field.type == SOL_OIC_REPR_TYPE_SIMPLE)
                *power = field.v_simple;
            continue;
        }
    }
}

static void
check_delete_request(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr, const struct sol_oic_map_reader *map_reader)
{
    if (response_code == SOL_COAP_RESPONSE_CODE_NOT_FOUND)
        sol_quit();
    else {
        printf("DELETE request failed\n");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
check_put_request(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr, const struct sol_oic_map_reader *map_reader)
{
    bool state = false;
    int32_t power = -1;

    if (!cliaddr || !map_reader ||
        response_code >= SOL_COAP_RESPONSE_CODE_BAD_REQUEST) {
        printf("Invalid GET response after a PUT\n");
    }

    fill_info(map_reader, &state, &power);
    if (power == PUT_REQUEST_POWER && state == true) {
        printf("PUT request successful\n");
        sol_quit();
    } else {
        printf("PUT request failed\n");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
check_post_request(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr, const struct sol_oic_map_reader *map_reader)
{
    int32_t power = -1;

    if (!cliaddr || !map_reader ||
        response_code >= SOL_COAP_RESPONSE_CODE_BAD_REQUEST) {
        printf("Invalid GET response after a PUT\n");
    }

    fill_info(map_reader, NULL, &power);
    if (power == POST_REQUEST_POWER) {
        printf("POST request successful\n");
        sol_quit();
    } else {
        printf("POST request failed\n");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static int
post_fill_repr_map(struct sol_oic_map_writer *repr_map)
{
    return sol_oic_map_append(repr_map, &SOL_OIC_REPR_INT("power",
        POST_REQUEST_POWER));
}

static bool
check_response_code(enum sol_coap_response_code response_code, int test_number)
{
    switch (test_number) {
    case TEST_NON_CONFIRMABLE_GET:
    case TEST_CONFIRMABLE_GET:
    case TEST_NON_CONFIRMABLE_OBSERVE:
    case TEST_CONFIRMABLE_OBSERVE:
        return response_code == SOL_COAP_RESPONSE_CODE_OK ||
               response_code == SOL_COAP_RESPONSE_CODE_CONTENT;
    case TEST_NON_CONFIRMABLE_PUT:
    case TEST_NON_CONFIRMABLE_POST:
    case TEST_CONFIRMABLE_POST:
        return response_code == SOL_COAP_RESPONSE_CODE_CHANGED;
    case TEST_NON_CONFIRMABLE_DELETE:
    case TEST_CONFIRMABLE_DELETE:
        return response_code == SOL_COAP_RESPONSE_CODE_CONTENT ||
               response_code == SOL_COAP_RESPONSE_CODE_DELETED;
    case TEST_NON_CONFIRMABLE_INVALID_GET:
        return response_code == SOL_COAP_RESPONSE_CODE_NOT_FOUND;
    }
    return false;
}

static void
resource_notify(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_oic_map_reader *map_reader)
{
    struct Context *ctx = data;
    static uint8_t notify_count = 0;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!cliaddr) {
        printf("Timeout reached\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!sol_network_link_addr_to_str(cliaddr, &addr)) {
        printf("Could not convert network address to string\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!check_response_code(response_code, ctx->test_number)) {
        printf("Invalid response\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    printf("Received successful notification packet from: %.*s\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));
    if (notify_count++ >= 5)
        sol_quit();
}

static void
dump_byte_string(struct sol_buffer *buf, const struct sol_str_slice bytes)
{
    const char *p, *end;

    if (!bytes.len)
        return;

    end = bytes.data + bytes.len;
    for (p = bytes.data; p < end; p++) {
        if (isprint(*p))
            sol_buffer_append_printf(buf, "%#x(%c) ", *p, *p);
        else
            sol_buffer_append_printf(buf, "%#x ", *p);
    }
    buf->used--;
}

static void
print_response(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_oic_map_reader *map_reader)
{
    struct sol_oic_repr_field field;
    enum sol_oic_map_loop_reason end_reason;
    struct sol_oic_map_reader iterator;
    struct Context *ctx = data;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_oic_request *request;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    if (!cliaddr) {
        printf("Timeout reached\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!sol_network_link_addr_to_str(cliaddr, &addr)) {
        printf("Could not convert network address to string\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!check_response_code(response_code, ctx->test_number)) {
        printf("Invalid response\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (ctx->test_number == TEST_NON_CONFIRMABLE_DELETE ||
        ctx->test_number == TEST_CONFIRMABLE_DELETE) {
        request = sol_oic_client_request_new(SOL_COAP_METHOD_GET, ctx->res);
        sol_oic_client_request(cli, request, check_delete_request,
            NULL);
        return;
    }

    if (map_reader) {
        printf("Dumping payload received from addr %.*s {\n", SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));
        SOL_OIC_MAP_LOOP(map_reader, &field, &iterator, end_reason) {

            switch (field.type) {
            case SOL_OIC_REPR_TYPE_UINT:
                printf("\tkey: '%s', value: uint(%" PRIu64 ")\n", field.key,
                    field.v_uint);
                break;
            case SOL_OIC_REPR_TYPE_INT:
                printf("\tkey: '%s', value: int(%" PRIi64 ")\n", field.key,
                    field.v_int);
                break;
            case SOL_OIC_REPR_TYPE_SIMPLE:
                printf("\tkey: '%s', value: simple(%d)\n", field.key,
                    field.v_simple);
                break;
            case SOL_OIC_REPR_TYPE_TEXT_STRING:
                printf("\tkey: '%s', value: str(%.*s)\n", field.key,
                    (int)field.v_slice.len, field.v_slice.data);
                break;
            case SOL_OIC_REPR_TYPE_BYTE_STRING:
                dump_byte_string(&buf, field.v_slice);
                printf("\tkey: '%s', value: bytestr{%.*s}\n", field.key,
                    SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)));

                sol_buffer_fini(&buf);
                break;
            case SOL_OIC_REPR_TYPE_HALF_FLOAT:
                printf("\tkey: '%s', value: hfloat(%p)\n", field.key,
                    field.v_voidptr);
                break;
            case SOL_OIC_REPR_TYPE_FLOAT:
                printf("\tkey: '%s', value: float(%f)\n", field.key,
                    field.v_float);
                break;
            case SOL_OIC_REPR_TYPE_DOUBLE:
                printf("\tkey: '%s', value: float(%g)\n", field.key,
                    field.v_double);
                break;
            case SOL_OIC_REPR_TYPE_BOOL:
                printf("\tkey: '%s', value: boolean(%s)\n", field.key,
                    field.v_boolean ? "true" : "false");
                break;
            case SOL_OIC_REPR_TYPE_UNSUPPORTED:
                printf("\tkey: '%s', value: unsupported cbor code(%" PRIi64 ")\n",
                    field.key, field.v_int);
                break;
            default:
                printf("\tkey: '%s', value: unknown(%d)\n", field.key, field.type);
            }
        }
        printf("}\n\n");
    }

    if (ctx->test_number == TEST_NON_CONFIRMABLE_PUT) {
        request = sol_oic_client_request_new(SOL_COAP_METHOD_GET, ctx->res);
        if (!request)
            goto error;
        sol_oic_client_request(cli, request, check_put_request, NULL);
    } else if (ctx->test_number == TEST_NON_CONFIRMABLE_POST ||
        ctx->test_number == TEST_CONFIRMABLE_POST) {
        request = sol_oic_client_request_new(SOL_COAP_METHOD_GET, ctx->res);
        if (!request)
            goto error;
        sol_oic_client_request(cli, request, check_post_request, NULL);
    } else if (map_reader)
        sol_quit();
    else {
error:
        printf("Invalid response: empty payload.\n");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
server_info_cb(void *data, struct sol_oic_client *cli, const struct sol_oic_device_info *info)
{
    char device_id[DEVICE_ID_LEN * 2];

    if (info == NULL) {
        printf("No device found.\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    device_id_decode(info->device_id.data, device_id);
    printf("Found Device:\n");
    printf(" - Device ID: %.*s\n", DEVICE_ID_LEN * 2, device_id);
    printf(" - Device name: %.*s\n", SOL_STR_SLICE_PRINT(info->device_name));
    printf(" - Spec version: %.*s\n", SOL_STR_SLICE_PRINT(info->spec_version));
    printf(" - Data model version: %.*s\n",
        SOL_STR_SLICE_PRINT(info->data_model_version));
    sol_quit();
}

static void
platform_info_cb(void *data, struct sol_oic_client *cli, const struct sol_oic_platform_info *info)
{
    if (info == NULL) {
        printf("No platform found.\n");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    printf("Found Platform:\n");
    printf(" - Platform ID: %.*s\n", SOL_STR_SLICE_PRINT(info->platform_id));
    printf(" - Manufacturer name: %.*s\n",
        SOL_STR_SLICE_PRINT(info->manufacturer_name));
    printf(" - Manufacturer URL: %.*s\n",
        SOL_STR_SLICE_PRINT(info->manufacturer_url));
    printf(" - Model Number: %.*s\n", SOL_STR_SLICE_PRINT(info->model_number));
    printf(" - Manufacturer date: %.*s\n",
        SOL_STR_SLICE_PRINT(info->manufacture_date));
    printf(" - Plafform version: %.*s\n",
        SOL_STR_SLICE_PRINT(info->platform_version));
    printf(" - Hardware version: %.*s\n",
        SOL_STR_SLICE_PRINT(info->hardware_version));
    printf(" - Firmware version: %.*s\n",
        SOL_STR_SLICE_PRINT(info->firmware_version));
    printf(" - Support URL: %.*s\n", SOL_STR_SLICE_PRINT(info->support_url));
    sol_quit();
}

static int
put_fill_repr_map(struct sol_oic_map_writer *repr_map)
{
    int r;

    r = sol_oic_map_append(repr_map,
        &SOL_OIC_REPR_BOOL("state", true));
    SOL_INT_CHECK(r, < 0, r);

    return sol_oic_map_append(repr_map,
        &SOL_OIC_REPR_INT("power", PUT_REQUEST_POWER));

}

static bool
found_resource(void *data,
    struct sol_oic_client *cli,
    struct sol_oic_resource *res)
{
    struct Context *ctx = data;
    bool non_confirmable = false, observe = false;
    const char *method_str = "GET";
    enum sol_coap_method method = SOL_COAP_METHOD_GET;

    int (*fill_repr_map)(struct sol_oic_map_writer *repr_map) = NULL;
    struct sol_str_slice path;
    struct sol_oic_request *request;
    struct sol_str_slice *path_ptr = (struct sol_str_slice *)&res->path;

    if (!res)
        return false;

    if (!found_resource_print(data, cli, res))
        return false;

    ctx->res = sol_oic_resource_ref(res);
    if (!ctx->res) {
        sol_quit_with_code(EXIT_FAILURE);
        return false;
    }

    switch (ctx->test_number) {
    case TEST_NON_CONFIRMABLE_GET:
        non_confirmable = true;
        break;
    case TEST_NON_CONFIRMABLE_PUT:
        method_str = "PUT";
        method = SOL_COAP_METHOD_PUT;
        non_confirmable = true;
        fill_repr_map = put_fill_repr_map;
        break;
    case TEST_NON_CONFIRMABLE_POST:
        method_str = "POST";
        method = SOL_COAP_METHOD_POST;
        non_confirmable = true;
        fill_repr_map = post_fill_repr_map;
        break;
    case TEST_NON_CONFIRMABLE_DELETE:
        method_str = "DELETE";
        method = SOL_COAP_METHOD_DELETE;
        non_confirmable = true;
        break;
    case TEST_NON_CONFIRMABLE_OBSERVE:
        method_str = "OBSERVE";
        non_confirmable = true;
        observe = true;
        break;
    case TEST_NON_CONFIRMABLE_INVALID_GET:
        non_confirmable = true;
        method_str = "invalid GET";
        path = res->path;
        //XXX: hack
        *path_ptr = sol_str_slice_from_str("/SomeUnknownResource");
        break;
    case TEST_CONFIRMABLE_GET:
        break;
    case TEST_CONFIRMABLE_POST:
        method_str = "POST";
        method = SOL_COAP_METHOD_POST;
        fill_repr_map = post_fill_repr_map;
        break;
    case TEST_CONFIRMABLE_DELETE:
        method_str = "DELETE";
        method = SOL_COAP_METHOD_DELETE;
        break;
    case TEST_CONFIRMABLE_OBSERVE:
        method_str = "OBSERVE";
        observe = true;
        break;

    default:
        printf("Invalid test\n");
        goto error;
    }

    printf("Issuing %sconfirmable %s on resource %.*s\n",
        non_confirmable ? "non-" : "", method_str,
        SOL_STR_SLICE_PRINT(res->path));

    if (observe) {
        if (non_confirmable)
            sol_oic_client_resource_set_observable
                (cli, res, resource_notify,
                data, true);
        else
            sol_oic_client_resource_set_observable_non_confirmable
                (cli, res, resource_notify,
                data, true);
    } else {
        if (non_confirmable) {
            request = sol_oic_client_request_new(method, res);
        } else {
            request = sol_oic_client_non_confirmable_request_new(method, res);
        }
        if (fill_repr_map)
            fill_repr_map(sol_oic_client_request_get_writer(request));
        sol_oic_client_request(cli, request, print_response, data);
    }

    if (ctx->test_number == TEST_NON_CONFIRMABLE_INVALID_GET)
        *path_ptr = path;

    return false;

error:
    sol_oic_resource_unref(res);
    return false;
}

static void
usage(void)
{
    printf("iotivity-test-client uses same test numbers used in occlient "
        "sample from iotivity.\n");
    printf("Usage : iotivity-test-client <1..20>\n");
    printf("1  :  Just discover resources.\n");
    printf("2  :  Non-confirmable GET Request\n");
    printf("3  :  Unsupported\n");
    printf("4  :  Non-confirmable PUT Requests\n");
    printf("5  :  Non-confirmable POST Requests\n");
    printf("6  :  Non-confirmable DELETE Requests\n");
    printf("7  :  Non-confirmable OBSERVE Requests\n");
    printf("8  :  Non-confirmable GET Request for an unavailable resource\n");
    printf("9  :  Confirmable GET Request\n");
    printf("10 :  Confirmable POST Request\n");
    printf("11 :  Confirmable DELETE Requests\n");
    printf("12 :  Confirmable OBSERVE Requests\n");
    printf("13 :  Unsupported\n");
    printf("14 :  Unsupported\n");
    printf("15 :  Unsupported\n");
    printf("16 :  Unsupported\n");
    printf("17 :  Unsupported\n");
    printf("18 :  Unsupported\n");
    printf("19 :  Discover Platforms\n");
    printf("20 :  Discover Devices\n");
}

int
main(int argc, char *argv[])
{
    struct Context ctx;
    int ret;

    struct sol_oic_client *client;
    struct sol_network_link_addr cliaddr = { .family = SOL_NETWORK_FAMILY_INET, .port = 5683 };
    const char *resource_type = NULL;
    const char *interface_type = NULL;

    bool (*found_resource_cb)(void *data, struct sol_oic_client *cli, struct sol_oic_resource *res) = NULL;

    ctx.res = NULL;
    sol_init();

    if (argc < 2) {
        usage();
        return -1;
    }

    ctx.test_number = atoi(argv[1]);
    if (ctx.test_number < 1 || ctx.test_number > 20) {
        usage();
        return 1;
    }

    if (argc >= 3 && argv[2][0])
        resource_type = argv[2];

    if (argc >= 4 && argv[3][0])
        interface_type = argv[3];

    if (!sol_network_link_addr_from_str(&cliaddr, "224.0.1.187")) {
        printf("could not convert multicast ip address to sockaddr_in\n");
        return 1;
    }

    switch (ctx.test_number) {
    case TEST_DISCOVERY:
        found_resource_cb = found_resource_print;
        break;
    case TEST_NON_CONFIRMABLE_GET:
    case TEST_NON_CONFIRMABLE_PUT:
    case TEST_NON_CONFIRMABLE_POST:
    case TEST_NON_CONFIRMABLE_DELETE:
    case TEST_NON_CONFIRMABLE_OBSERVE:
    case TEST_NON_CONFIRMABLE_INVALID_GET:
    case TEST_CONFIRMABLE_GET:
    case TEST_CONFIRMABLE_POST:
    case TEST_CONFIRMABLE_DELETE:
    case TEST_CONFIRMABLE_OBSERVE:
        found_resource_cb = found_resource;
    case TEST_DISCOVER_PLATFORM:
    case TEST_DISCOVER_DEVICES:
        break;

    default:
        printf("Unsupported test.\n");
        return 0;
    }

    client = sol_oic_client_new();
    if (ctx.test_number == TEST_DISCOVER_PLATFORM)
        sol_oic_client_get_platform_info_by_addr(client, &cliaddr,
            platform_info_cb, NULL);
    else if (ctx.test_number == TEST_DISCOVER_DEVICES)
        sol_oic_client_get_server_info_by_addr(client, &cliaddr,
            server_info_cb, NULL);
    else
        sol_oic_client_find_resources(client, &cliaddr, resource_type,
            interface_type, found_resource_cb, &ctx);

    ret = sol_run();

    sol_oic_client_del(client);
    if (ctx.res)
        sol_oic_resource_unref(ctx.res);
    return ret;
}
