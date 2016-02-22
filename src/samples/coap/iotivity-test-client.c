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

static bool found_resource(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data);

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
found_resource_print(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data)
{
    struct sol_str_slice *slice;
    uint16_t idx;
    char addr[SOL_INET_ADDR_STRLEN];
    char device_id[DEVICE_ID_LEN * 2];
    static bool resource_found = false;

    if (!res) {
        if (!resource_found) {
            SOL_WRN("No resource found");
            sol_quit_with_code(EXIT_FAILURE);
        } else
            sol_quit();
        return false;
    }

    if (res->device_id.len < DEVICE_ID_LEN) {
        SOL_WRN("Invalid device id");
        return false;
    }

    if (!sol_network_addr_to_str(&res->addr, addr, sizeof(addr))) {
        SOL_WRN("Could not convert network address to string");
        return false;
    }

    resource_found = true;
    SOL_DBG("Found resource: coap://%s%.*s", addr,
        SOL_STR_SLICE_PRINT(res->href));

    SOL_DBG("Flags:");
    SOL_DBG(" - observable: %s", res->observable ? "yes" : "no");
    SOL_DBG(" - secure: %s", res->secure ? "yes" : "no");

    device_id_decode(res->device_id.data, device_id);
    SOL_DBG("Device ID: %.*s", DEVICE_ID_LEN * 2, device_id);

    SOL_DBG("Resource types:");
    SOL_VECTOR_FOREACH_IDX (&res->types, slice, idx)
        SOL_DBG("\t\t%.*s", SOL_STR_SLICE_PRINT(*slice));

    SOL_DBG("Resource interfaces:");
    SOL_VECTOR_FOREACH_IDX (&res->interfaces, slice, idx)
        SOL_DBG("\t\t%.*s", SOL_STR_SLICE_PRINT(*slice));
    SOL_DBG(" ");

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
            field.type == SOL_OIC_REPR_TYPE_BOOLEAN) {
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
check_delete_request(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr, const struct sol_oic_map_reader *map_reader, void *data)
{
    if (response_code == SOL_COAP_RSPCODE_NOT_FOUND)
        sol_quit();
    else {
        SOL_WRN("DELETE request failed");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
check_put_request(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr, const struct sol_oic_map_reader *map_reader, void *data)
{
    bool state = false;
    int32_t power = -1;

    if (!cliaddr || !map_reader ||
        response_code >= SOL_COAP_RSPCODE_BAD_REQUEST) {
        SOL_WRN("Invalid GET response after a PUT");
    }

    fill_info(map_reader, &state, &power);
    if (power == PUT_REQUEST_POWER && state == true) {
        SOL_DBG("PUT request successful");
        sol_quit();
    } else {
        SOL_DBG("PUT request failed");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
check_post_request(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr, const struct sol_oic_map_reader *map_reader, void *data)
{
    int32_t power = -1;

    if (!cliaddr || !map_reader ||
        response_code >= SOL_COAP_RSPCODE_BAD_REQUEST) {
        SOL_WRN("Invalid GET response after a PUT");
    }

    fill_info(map_reader, NULL, &power);
    if (power == POST_REQUEST_POWER) {
        SOL_DBG("POST request successful");
        sol_quit();
    } else {
        SOL_DBG("POST request failed");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static bool
post_fill_repr_map(void *data, struct sol_oic_map_writer *repr_map)
{
    int ret;

    ret = sol_oic_map_append(repr_map,
        &SOL_OIC_REPR_INT("power", POST_REQUEST_POWER));
    SOL_EXP_CHECK(!ret, false);

    return true;
}

static bool
check_response_code(sol_coap_responsecode_t response_code, int test_number)
{
    switch (test_number) {
    case TEST_NON_CONFIRMABLE_GET:
    case TEST_CONFIRMABLE_GET:
    case TEST_NON_CONFIRMABLE_OBSERVE:
    case TEST_CONFIRMABLE_OBSERVE:
        return response_code == SOL_COAP_RSPCODE_OK ||
               response_code == SOL_COAP_RSPCODE_CONTENT;
    case TEST_NON_CONFIRMABLE_PUT:
    case TEST_NON_CONFIRMABLE_POST:
    case TEST_CONFIRMABLE_POST:
        return response_code == SOL_COAP_RSPCODE_CHANGED;
    case TEST_NON_CONFIRMABLE_DELETE:
    case TEST_CONFIRMABLE_DELETE:
        return response_code == SOL_COAP_RSPCODE_CONTENT ||
               response_code == SOL_COAP_RSPCODE_DELETED;
    case TEST_NON_CONFIRMABLE_INVALID_GET:
        return response_code == SOL_COAP_RSPCODE_NOT_FOUND;
    }
    return false;
}

static void
resource_notify(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_oic_map_reader *map_reader, void *data)
{
    char addr[SOL_INET_ADDR_STRLEN];
    struct Context *ctx = data;
    static uint8_t notify_count = 0;

    if (!cliaddr) {
        SOL_WRN("Timeout reached");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!sol_network_addr_to_str(cliaddr, addr, sizeof(addr))) {
        SOL_WRN("Could not convert network address to string");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!check_response_code(response_code, ctx->test_number)) {
        SOL_WRN("Invalid response");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    SOL_WRN("Received successful notification packet");
    if (notify_count++ >= 5)
        sol_quit();
}

static void
dump_byte_string(struct sol_buffer *buf, const struct sol_str_slice bytes)
{
    const char *p, *end;

    end = bytes.data + bytes.len;
    for (p = bytes.data; p < end; p++) {
        if (isprint(*p))
            sol_buffer_append_printf(buf, "%#x(%c)", *p, *p);
        else
            sol_buffer_append_printf(buf, "%#x", *p);
    }
}

static void
print_response(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *cliaddr,
    const struct sol_oic_map_reader *map_reader, void *data)
{
    struct sol_oic_repr_field field;
    enum sol_oic_map_loop_reason end_reason;
    struct sol_oic_map_reader iterator;
    char addr[SOL_INET_ADDR_STRLEN];
    struct Context *ctx = data;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;

    if (!cliaddr) {
        SOL_WRN("Timeout reached");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!sol_network_addr_to_str(cliaddr, addr, sizeof(addr))) {
        SOL_WRN("Could not convert network address to string");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (!check_response_code(response_code, ctx->test_number)) {
        SOL_WRN("Invalid response");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    if (ctx->test_number == TEST_NON_CONFIRMABLE_DELETE ||
        ctx->test_number == TEST_CONFIRMABLE_DELETE) {
        sol_oic_client_resource_request(cli, ctx->res, SOL_COAP_METHOD_GET,
            NULL, NULL, check_delete_request, NULL);
        return;
    }

    if (map_reader) {
        SOL_DBG("Dumping payload received from addr %s {", addr);
        SOL_OIC_MAP_LOOP(map_reader, &field, &iterator, end_reason) {

            switch (field.type) {
            case SOL_OIC_REPR_TYPE_UINT:
                SOL_DBG("\tkey: '%s', value: uint(%" PRIu64 ")", field.key,
                    field.v_uint);
                break;
            case SOL_OIC_REPR_TYPE_INT:
                SOL_DBG("\tkey: '%s', value: int(%" PRIi64 ")", field.key,
                    field.v_int);
                break;
            case SOL_OIC_REPR_TYPE_SIMPLE:
                SOL_DBG("\tkey: '%s', value: simple(%d)", field.key,
                    field.v_simple);
                break;
            case SOL_OIC_REPR_TYPE_TEXT_STRING:
                SOL_DBG("\tkey: '%s', value: str(%.*s)", field.key,
                    (int)field.v_slice.len, field.v_slice.data);
                break;
            case SOL_OIC_REPR_TYPE_BYTE_STRING:
                dump_byte_string(&buf, field.v_slice);
                SOL_DBG("\tkey: '%s', value: bytestr{%s}", field.key,
                    (char *)buf.data);

                sol_buffer_fini(&buf);
                break;
            case SOL_OIC_REPR_TYPE_HALF_FLOAT:
                SOL_DBG("\tkey: '%s', value: hfloat(%p)", field.key,
                    field.v_voidptr);
                break;
            case SOL_OIC_REPR_TYPE_FLOAT:
                SOL_DBG("\tkey: '%s', value: float(%f)", field.key,
                    field.v_float);
                break;
            case SOL_OIC_REPR_TYPE_DOUBLE:
                SOL_DBG("\tkey: '%s', value: float(%g)", field.key,
                    field.v_double);
                break;
            case SOL_OIC_REPR_TYPE_BOOLEAN:
                SOL_DBG("\tkey: '%s', value: boolean(%s)", field.key,
                    field.v_boolean ? "true" : "false");
                break;
            default:
                SOL_DBG("\tkey: '%s', value: unknown(%d)", field.key, field.type);
            }
        }
        SOL_DBG("}\n");
    }

    if (ctx->test_number == TEST_NON_CONFIRMABLE_PUT) {
        sol_oic_client_resource_request(cli, ctx->res, SOL_COAP_METHOD_GET,
            NULL, NULL, check_put_request, NULL);
    } else if (ctx->test_number == TEST_NON_CONFIRMABLE_POST ||
        ctx->test_number == TEST_CONFIRMABLE_POST) {
        sol_oic_client_resource_request(cli, ctx->res, SOL_COAP_METHOD_GET,
            NULL, NULL, check_post_request, NULL);
    } else if (map_reader)
        sol_quit();
    else {
        SOL_WRN("Invalid response: empty payload.");
        sol_quit_with_code(EXIT_FAILURE);
    }
}

static void
server_info_cb(struct sol_oic_client *cli, const struct sol_oic_server_information *info, void *data)
{
    char device_id[DEVICE_ID_LEN * 2];

    if (info == NULL) {
        SOL_WRN("No device found.");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    device_id_decode(info->device_id.data, device_id);
    SOL_DBG("Found Device:");
    SOL_DBG(" - Device ID: %.*s", DEVICE_ID_LEN * 2, device_id);
    SOL_DBG(" - Device name: %.*s", SOL_STR_SLICE_PRINT(info->device_name));
    SOL_DBG(" - Spec version: %.*s", SOL_STR_SLICE_PRINT(info->spec_version));
    SOL_DBG(" - Data model version: %.*s",
        SOL_STR_SLICE_PRINT(info->data_model_version));
    sol_quit();
}

static void
platform_info_cb(struct sol_oic_client *cli, const struct sol_oic_platform_information *info, void *data)
{
    if (info == NULL) {
        SOL_WRN("No platform found.");
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    SOL_DBG("Found Platform:");
    SOL_DBG(" - Platform ID: %.*s", SOL_STR_SLICE_PRINT(info->platform_id));
    SOL_DBG(" - Manufacturer name: %.*s",
        SOL_STR_SLICE_PRINT(info->manufacturer_name));
    SOL_DBG(" - Manufacturer URL: %.*s",
        SOL_STR_SLICE_PRINT(info->manufacturer_url));
    SOL_DBG(" - Model Number: %.*s", SOL_STR_SLICE_PRINT(info->model_number));
    SOL_DBG(" - Manufacturer date: %.*s",
        SOL_STR_SLICE_PRINT(info->manufacture_date));
    SOL_DBG(" - Plafform version: %.*s",
        SOL_STR_SLICE_PRINT(info->platform_version));
    SOL_DBG(" - Hardware version: %.*s",
        SOL_STR_SLICE_PRINT(info->hardware_version));
    SOL_DBG(" - Firmware version: %.*s",
        SOL_STR_SLICE_PRINT(info->firmware_version));
    SOL_DBG(" - Support URL: %.*s", SOL_STR_SLICE_PRINT(info->support_url));
    sol_quit();
}

static bool
put_fill_repr_map(void *data, struct sol_oic_map_writer *repr_map)
{
    int ret;

    ret = sol_oic_map_append(repr_map,
        &SOL_OIC_REPR_BOOLEAN("state", true));
    SOL_EXP_CHECK(!ret, false);

    ret = sol_oic_map_append(repr_map,
        &SOL_OIC_REPR_INT("power", PUT_REQUEST_POWER));
    SOL_EXP_CHECK(!ret, false);

    return true;
}

static bool
found_resource(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data)
{
    struct Context *ctx = data;
    bool non_confirmable = false, observe = false;
    const char *method_str = "GET";
    sol_coap_method_t method = SOL_COAP_METHOD_GET;

    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *repr_map) = NULL;
    struct sol_str_slice href;

    if (!res)
        return false;

    if (!found_resource_print(cli, res, data))
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
        href = res->href;
        res->href = sol_str_slice_from_str("/SomeUnknownResource");
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
        SOL_WRN("Invalid test");
        goto error;
    }

    SOL_DBG("Issuing %sconfirmable %s on resource %.*s",
        non_confirmable ? "non-" : "", method_str,
        SOL_STR_SLICE_PRINT(res->href));

    if (observe) {
        if (non_confirmable)
            sol_oic_client_resource_set_observable(cli, res, resource_notify,
                data, true);
        else
            sol_oic_client_resource_set_observable_non_confirmable(cli, res,
                resource_notify, data, true);
    } else {
        if (non_confirmable)
            sol_oic_client_resource_non_confirmable_request(cli, res, method,
                fill_repr_map, NULL, print_response, data);
        else
            sol_oic_client_resource_request(cli, res, method, fill_repr_map,
                NULL, print_response, data);
    }

    if (ctx->test_number == TEST_NON_CONFIRMABLE_INVALID_GET)
        res->href = href;

    return false;

error:
    sol_oic_resource_unref(res);
    return false;
}

static void
usage(void)
{
    SOL_INF("iotivity-test-client uses same test numbers used in occlient "
        "sample from iotivity.");
    SOL_INF("Usage : iotivity-test-client <1..20>");
    SOL_INF("1  :  Just discover resources.");
    SOL_INF("2  :  Non-confirmable GET Request");
    SOL_INF("3  :  Unsupported");
    SOL_INF("4  :  Non-confirmable PUT Requests");
    SOL_INF("5  :  Non-confirmable POST Requests");
    SOL_INF("6  :  Non-confirmable DELETE Requests");
    SOL_INF("7  :  Non-confirmable OBSERVE Requests");
    SOL_INF("8  :  Non-confirmable GET Request for an unavailable resource");
    SOL_INF("9  :  Confirmable GET Request");
    SOL_INF("10 :  Confirmable POST Request");
    SOL_INF("11 :  Confirmable DELETE Requests");
    SOL_INF("12 :  Confirmable OBSERVE Requests");
    SOL_INF("13 :  Unsupported");
    SOL_INF("14 :  Unsupported");
    SOL_INF("15 :  Unsupported");
    SOL_INF("16 :  Unsupported");
    SOL_INF("17 :  Unsupported");
    SOL_INF("18 :  Unsupported");
    SOL_INF("19 :  Discover Platforms");
    SOL_INF("20 :  Discover Devices");
}

int
main(int argc, char *argv[])
{
    struct Context ctx;
    int ret;

    struct sol_oic_client client = {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_CLIENT_API_VERSION)
    };
    struct sol_network_link_addr cliaddr = { .family = SOL_NETWORK_FAMILY_INET, .port = 5683 };
    const char *resource_type = NULL;

    bool (*found_resource_cb)(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data) = NULL;

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

    if (!sol_network_addr_from_str(&cliaddr, "224.0.1.187")) {
        SOL_WRN("could not convert multicast ip address to sockaddr_in");
        return 1;
    }

    client.server = sol_coap_server_new(0);
    client.dtls_server = sol_coap_secure_server_new(0);

    SOL_INF("DTLS support %s\n", client.dtls_server ? "available" :
        "unavailable");

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
        resource_type = "core.light";
    case TEST_DISCOVER_PLATFORM:
    case TEST_DISCOVER_DEVICES:
        break;

    default:
        SOL_WRN("Unsupported test.");
        return 0;
    }

    if (ctx.test_number == TEST_DISCOVER_PLATFORM)
        sol_oic_client_get_platform_info_by_addr(&client, &cliaddr,
            platform_info_cb, NULL);
    else if (ctx.test_number == TEST_DISCOVER_DEVICES)
        sol_oic_client_get_server_info_by_addr(&client, &cliaddr,
            server_info_cb, NULL);
    else
        sol_oic_client_find_resource(&client, &cliaddr, resource_type,
            found_resource_cb, &ctx);

    ret = sol_run();
    if (ctx.res)
        sol_oic_resource_unref(ctx.res);
    return ret;
}
