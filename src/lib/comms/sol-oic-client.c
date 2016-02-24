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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbor.h"
#include "sol-coap.h"
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-random.h"
#include "sol-util-internal.h"

#include "sol-oic-client.h"
#include "sol-oic-cbor.h"
#include "sol-oic-common.h"
#include "sol-oic-server.h"

#define POLL_OBSERVE_TIMEOUT_MS 10000

#define OIC_COAP_SERVER_UDP_PORT  5683
#define OIC_COAP_SERVER_DTLS_PORT 5684

#ifndef SOL_NO_API_VERSION
#define OIC_RESOURCE_CHECK_API(ptr, ...) \
    do { \
        if (SOL_UNLIKELY(ptr->api_version != \
            SOL_OIC_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle oic client resource that has unsupported " \
                "version '%u', expected version is '%u'", \
                ptr->api_version, SOL_OIC_RESOURCE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)
#else
#define OIC_RESOURCE_CHECK_API(ptr, ...)
#endif

struct sol_oic_client {
    struct sol_coap_server *server;
    struct sol_coap_server *dtls_server;
};

struct find_resource_ctx {
    struct sol_oic_client *client;
    bool (*cb)(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data);
    const void *data;
    int64_t token;
};

struct server_info_ctx {
    struct sol_oic_client *client;
    void (*cb)(struct sol_oic_client *cli, const struct sol_oic_platform_information *info, void *data);
    const void *data;
    int64_t token;
};

struct resource_request_ctx {
    struct sol_oic_client *client;
    struct sol_oic_resource *res;
    void (*cb)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
        const struct sol_oic_map_reader *repr_vec, void *data);
    const void *data;
    int64_t token;
};

SOL_LOG_INTERNAL_DECLARE(_sol_oic_client_log_domain, "oic-client");

static struct sol_coap_server *
_best_server_for_resource(const struct sol_oic_client *client,
    const struct sol_oic_resource *res, struct sol_network_link_addr *addr)
{
    *addr = res->addr;

    if (client->dtls_server && res->secure) {
        addr->port = OIC_COAP_SERVER_DTLS_PORT;
        SOL_DBG("Resource has secure flag and we have DTLS support (using port %d)",
            addr->port);
        return client->dtls_server;
    }

    SOL_DBG("Resource %s secure flag and we %s DTLS support (using port %d)",
        res->secure ? "has" : "does not have",
        client->dtls_server ? "have" : "do not have",
        addr->port);
    return client->server;
}

static bool
_set_token_and_mid(struct sol_coap_packet *pkt, int64_t *token)
{
    static struct sol_random *random = NULL;
    int32_t mid;

    if (SOL_UNLIKELY(!random)) {
        random = sol_random_new(SOL_RANDOM_DEFAULT, 0);
        SOL_NULL_CHECK(random, false);
    }

    if (!sol_random_get_int64(random, token)) {
        SOL_WRN("Could not generate CoAP token");
        return false;
    }
    if (!sol_random_get_int32(random, &mid)) {
        SOL_WRN("Could not generate CoAP message id");
        return false;
    }

    if (!sol_coap_header_set_token(pkt, (uint8_t *)token, (uint8_t)sizeof(*token))) {
        SOL_WRN("Could not set CoAP packet token");
        return false;
    }

    sol_coap_header_set_id(pkt, (int16_t)mid);

    return true;
}

static bool
_pkt_has_same_token(const struct sol_coap_packet *pkt, int64_t token)
{
    uint8_t *token_data, token_len;

    token_data = sol_coap_header_get_token(pkt, &token_len);
    if (SOL_UNLIKELY(!token_data))
        return false;

    if (SOL_UNLIKELY(token_len != sizeof(token)))
        return false;

    return SOL_LIKELY(memcmp(token_data, &token, sizeof(token)) == 0);
}

SOL_API struct sol_oic_resource *
sol_oic_resource_ref(struct sol_oic_resource *r)
{
    SOL_NULL_CHECK(r, NULL);
    OIC_RESOURCE_CHECK_API(r, NULL);

    r->refcnt++;
    return r;
}

SOL_API void
sol_oic_resource_unref(struct sol_oic_resource *r)
{
    SOL_NULL_CHECK(r);
    OIC_RESOURCE_CHECK_API(r);

    r->refcnt--;
    if (!r->refcnt) {
        free((char *)r->href.data);
        free((char *)r->device_id.data);

        sol_vector_clear(&r->types);
        free(r->types_data);

        sol_vector_clear(&r->interfaces);
        free(r->interfaces_data);

        free(r);
    }
}

static bool
_parse_platform_info_payload(struct sol_oic_platform_information *info,
    uint8_t *payload, uint16_t payload_len)
{
    CborParser parser;
    CborError err;
    CborValue root;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);

    if (!cbor_value_is_map(&root))
        return false;

    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_PLATFORM_ID,
        &info->platform_id))
        return false;
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MANUF_NAME,
        &info->manufacturer_name)) {
        free((char *)info->platform_id.data);
        return false;
    }
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MANUF_URL,
        &info->manufacturer_url))
        info->manufacturer_url = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MODEL_NUM,
        &info->model_number))
        info->model_number = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MANUF_DATE,
        &info->manufacture_date))
        info->manufacture_date = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_PLATFORM_VER,
        &info->platform_version))
        info->platform_version = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_OS_VER,
        &info->os_version))
        info->os_version = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_HW_VER,
        &info->hardware_version))
        info->hardware_version = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_FIRMWARE_VER,
        &info->firmware_version))
        info->firmware_version = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_SUPPORT_URL,
        &info->support_url))
        info->support_url = SOL_STR_SLICE_STR(NULL, 0);
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_SYSTEM_TIME,
        &info->system_time))
        info->system_time = SOL_STR_SLICE_STR(NULL, 0);

    return true;
}

static bool
_parse_server_info_payload(struct sol_oic_server_information *info,
    uint8_t *payload, uint16_t payload_len)
{
    CborParser parser;
    CborError err;
    CborValue root;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);

    if (!cbor_value_is_map(&root))
        return false;

    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_SPEC_VERSION,
        &info->spec_version))
        return false;
    if (!sol_cbor_map_get_bytestr_value(&root, SOL_OIC_KEY_DEVICE_ID,
        &info->device_id))
        goto error;
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_DATA_MODEL_VERSION,
        &info->data_model_version))
        goto error;

    //TODO: This field should be mandatory, but letting it optional to make it
    //compatible with iotivity 1.0.1
    if (!sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_DEVICE_NAME,
        &info->device_name))
        info->device_name = SOL_STR_SLICE_STR(NULL, 0);

    return true;

error:
    free((char *)info->spec_version.data);
    free((char *)info->device_id.data);
    return false;
}

static bool
_platform_info_reply_cb(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr,
    void *data)
{
    struct server_info_ctx *ctx = data;
    uint8_t *payload;
    uint16_t payload_len;
    struct sol_oic_platform_information info = { 0 };

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        goto free_ctx;
    }

    if (!req || !addr)
        goto error;

    if (!_pkt_has_same_token(req, ctx->token))
        goto error;

    if (!sol_oic_pkt_has_cbor_content(req))
        goto error;

    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        SOL_WRN("Could not get pkt payload");
        goto error;
    }

    if (_parse_platform_info_payload(&info, payload, payload_len)) {
        SOL_SET_API_VERSION(info.api_version = SOL_OIC_PLATFORM_INFORMATION_API_VERSION; )
        ctx->cb(ctx->client, &info, (void *)ctx->data);
    } else {
        SOL_WRN("Could not parse payload");
        goto error;
    }

    free((char *)info.platform_id.data);
    free((char *)info.manufacturer_name.data);
    free((char *)info.manufacturer_url.data);
    free((char *)info.model_number.data);
    free((char *)info.manufacture_date.data);
    free((char *)info.platform_version.data);
    free((char *)info.os_version.data);
    free((char *)info.hardware_version.data);
    free((char *)info.firmware_version.data);
    free((char *)info.support_url.data);
    free((char *)info.system_time.data);
    goto free_ctx;

error:
    ctx->cb(ctx->client, NULL, (char *)ctx->data);
free_ctx:
    free(ctx);
    return false;
}

static bool
_server_info_reply_cb(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr,
    void *data)
{
    struct server_info_ctx *ctx = data;
    uint8_t *payload;
    uint16_t payload_len;
    struct sol_oic_server_information info = { 0 };

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        goto free_ctx;
    }

    if (!req || !addr) {
        goto error;
    }

    if (!_pkt_has_same_token(req, ctx->token))
        goto error;

    if (!sol_oic_pkt_has_cbor_content(req))
        goto error;

    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        SOL_WRN("Could not get pkt payload");
        goto error;
    }

    if (_parse_server_info_payload(&info, payload, payload_len)) {
        void (*cb)(struct sol_oic_client *cli,
            const struct sol_oic_server_information *info, void *data);

        SOL_SET_API_VERSION(info.api_version = SOL_OIC_SERVER_INFORMATION_API_VERSION; )
        cb = (void (*)(struct sol_oic_client *cli,
            const struct sol_oic_server_information *info, void *data))ctx->cb;
        cb(ctx->client, &info, (void *)ctx->data);
    } else {
        SOL_WRN("Could not parse payload");
        goto error;
    }

    free((char *)info.device_name.data);
    free((char *)info.spec_version.data);
    free((char *)info.device_id.data);
    free((char *)info.data_model_version.data);
    goto free_ctx;

error:
    ctx->cb(ctx->client, NULL, (void *)ctx->data);
free_ctx:
    free(ctx);
    return false;
}

static bool
client_get_info(struct sol_oic_client *client,
    struct sol_coap_server *server,
    struct sol_network_link_addr *addr,
    const char *device_uri,
    bool (*reply_cb)(struct sol_coap_server *server, struct sol_coap_packet *req, const struct sol_network_link_addr *addr, void *data),
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_platform_information *info, void *data),
    const void *data)
{
    struct server_info_ctx *ctx;
    struct sol_coap_packet *req;
    int r;

    ctx = sol_util_memdup(&(struct server_info_ctx) {
            .client = client,
            .cb = info_received_cb,
            .data = data,
        }, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, false);

    req = sol_coap_packet_request_new(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_CON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        goto out_no_pkt;
    }

    if (!_set_token_and_mid(req, &ctx->token))
        goto out;

    if (sol_coap_packet_add_uri_path_option(req, device_uri) < 0) {
        SOL_WRN("Invalid URI: %s", device_uri);
        goto out;
    }

    r = sol_coap_send_packet_with_reply(server, req, addr, reply_cb, ctx);
    if (!r)
        return true;

    goto out_no_pkt;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return false;
}

SOL_API bool
sol_oic_client_get_platform_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_platform_information *info, void *data),
    const void *data)
{
    struct sol_network_link_addr addr;
    struct sol_coap_server *server;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    OIC_RESOURCE_CHECK_API(resource, false);

    server = _best_server_for_resource(client, resource,  &addr);
    return client_get_info(client, server, &addr, SOL_OIC_PLATFORM_PATH,
        _platform_info_reply_cb, info_received_cb, data);
}

SOL_API bool
sol_oic_client_get_platform_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_platform_information *info, void *data),
    const void *data)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    SOL_NULL_CHECK(addr, false);

    return client_get_info(client, client->server, addr, SOL_OIC_PLATFORM_PATH,
        _platform_info_reply_cb, info_received_cb, data);
}

SOL_API bool
sol_oic_client_get_server_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_server_information *info, void *data),
    const void *data)
{
    struct sol_network_link_addr addr;
    struct sol_coap_server *server;

    void (*cb)(struct sol_oic_client *cli,
        const struct sol_oic_platform_information *info, void *data);

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    OIC_RESOURCE_CHECK_API(resource, false);

    cb = (void (*)(struct sol_oic_client *cli,
        const struct sol_oic_platform_information *info, void *data))
        info_received_cb;
    server = _best_server_for_resource(client, resource,  &addr);
    return client_get_info(client, server, &addr, SOL_OIC_DEVICE_PATH,
        _server_info_reply_cb, cb, data);
}

SOL_API bool
sol_oic_client_get_server_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_server_information *info, void *data),
    const void *data)
{
    void (*cb)(struct sol_oic_client *cli,
        const struct sol_oic_platform_information *info, void *data);

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    SOL_NULL_CHECK(addr, false);

    cb = (void (*)(struct sol_oic_client *cli,
        const struct sol_oic_platform_information *info, void *data))
        info_received_cb;
    return client_get_info(client, client->server, addr, SOL_OIC_DEVICE_PATH,
        _server_info_reply_cb, cb, data);
}

static bool
_has_observable_option(struct sol_coap_packet *pkt)
{
    const uint8_t *ptr;
    uint16_t len;

    ptr = sol_coap_find_first_option(pkt, SOL_COAP_OPTION_OBSERVE, &len);

    return ptr && len == 1 && *ptr;
}

static struct sol_oic_resource *
_new_resource(void)
{
    struct sol_oic_resource *res = malloc(sizeof(*res));

    SOL_NULL_CHECK(res, NULL);

    res->href = SOL_STR_SLICE_STR(NULL, 0);
    res->device_id = SOL_STR_SLICE_STR(NULL, 0);
    sol_vector_init(&res->types, sizeof(struct sol_str_slice));
    res->types_data = NULL;
    sol_vector_init(&res->interfaces, sizeof(struct sol_str_slice));
    res->interfaces_data = NULL;

    res->observe.timeout = NULL;
    res->observe.clear_data = 0;

    res->observable = false;
    res->secure = false;
    res->is_observing = false;

    res->refcnt = 1;

    SOL_SET_API_VERSION(res->api_version = SOL_OIC_RESOURCE_API_VERSION; )

    return res;
}

static bool
_iterate_over_resource_reply_payload(struct sol_coap_packet *req,
    const struct sol_network_link_addr *addr,
    const struct find_resource_ctx *ctx, bool *cb_return)
{
    CborParser parser;
    CborError err;
    CborValue root, devices_array, resources_array, value, map;
    uint8_t *payload;
    uint16_t payload_len;
    struct sol_str_slice device_id;
    struct sol_oic_resource *res = NULL;
    CborValue bitmap_value, secure_value;
    uint64_t bitmap;
    bool secure;


    *cb_return  = true;

    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        SOL_WRN("Could not get payload form discovery packet response");
        return false;
    }

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);
    if (!cbor_value_is_array(&root))
        return false;

    err = cbor_value_enter_container(&root, &devices_array);
    SOL_INT_CHECK(err, != CborNoError, false);

    for (; cbor_value_is_map(&devices_array) && err == CborNoError;
        err = cbor_value_advance(&devices_array)) {
        SOL_INT_CHECK(err, != CborNoError, false);
        if (!sol_cbor_map_get_bytestr_value(&devices_array, SOL_OIC_KEY_DEVICE_ID,
            &device_id))
            return false;

        err  = cbor_value_map_find_value(&devices_array,
            SOL_OIC_KEY_RESOURCE_LINKS, &value);
        if (err != CborNoError || !cbor_value_is_array(&value))
            goto error;

        err = cbor_value_enter_container(&value, &resources_array);
        SOL_INT_CHECK_GOTO(err, != CborNoError, error);
        for (; cbor_value_is_map(&resources_array) && err == CborNoError;
            err = cbor_value_advance(&resources_array)) {
            res = _new_resource();
            SOL_NULL_CHECK_GOTO(res, error);

            if (!sol_cbor_map_get_str_value(&resources_array, SOL_OIC_KEY_HREF,
                &res->href))
                goto error;

            if (!sol_cbor_map_get_bsv(&resources_array,
                SOL_OIC_KEY_RESOURCE_TYPES, &res->types_data, &res->types))
                goto error;
            if (!sol_cbor_map_get_bsv(&resources_array,
                SOL_OIC_KEY_INTERFACES, &res->interfaces_data,
                &res->interfaces))
                goto error;

            err = cbor_value_map_find_value(&resources_array,
                SOL_OIC_KEY_POLICY, &map);
            if (err != CborNoError || !cbor_value_is_map(&map))
                goto error;

            err = cbor_value_map_find_value(&map, SOL_OIC_KEY_BITMAP,
                &bitmap_value);
            if (err != CborNoError ||
                !cbor_value_is_unsigned_integer(&bitmap_value))
                goto error;
            err = cbor_value_get_uint64(&bitmap_value, &bitmap);
            SOL_INT_CHECK_GOTO(err, != CborNoError, error);

            err = cbor_value_map_find_value(&map, SOL_OIC_KEY_POLICY_SECURE,
                &secure_value);
            SOL_INT_CHECK(err, != CborNoError, false);
            if (!cbor_value_is_valid(&secure_value)) {
                secure = false;
            } else {
                if (!cbor_value_is_boolean(&secure_value))
                    goto error;
                err = cbor_value_get_boolean(&secure_value, &secure);
                SOL_INT_CHECK_GOTO(err, != CborNoError, error);
            }

            res->observable = (bitmap & SOL_OIC_FLAG_OBSERVABLE);
            res->secure = secure;
            res->observable = res->observable || _has_observable_option(req);
            res->addr = *addr;
            res->device_id.data = sol_util_memdup(device_id.data,
                device_id.len);
            if (!res->device_id.data)
                goto error;
            res->device_id.len = device_id.len;
            if (!ctx->cb(ctx->client, res, (void *)ctx->data)) {
                sol_oic_resource_unref(res);
                free((char *)device_id.data);
                *cb_return  = false;
                return true;
            }

            sol_oic_resource_unref(res);
        }
        free((char *)device_id.data);
    }

    return true;

error:
    sol_oic_resource_unref(res);
    free((char *)device_id.data);
    return false;
}

static bool
_find_resource_reply_cb(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr,
    void *data)
{
    struct find_resource_ctx *ctx = data;
    bool cb_return;

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        free(ctx);
        return false;
    }

    if (!req || !addr) {
        if (!ctx->cb(ctx->client, NULL, (void *)ctx->data)) {
            free(ctx);
            return false;
        }
        return true;
    }

    if (!_pkt_has_same_token(req, ctx->token)) {
        SOL_WRN("Discovery packet token differs from expected");
        return false;
    }

    if (!sol_oic_pkt_has_cbor_content(req)) {
        SOL_WRN("Discovery packet not in CBOR format");
        return true;
    }

    if (!_iterate_over_resource_reply_payload(req, addr, ctx, &cb_return)) {
        SOL_WRN("Could not iterate over find resource reply packet");
        return true;
    }

    if (!cb_return)
        free(ctx);
    return cb_return;
}

SOL_API bool
sol_oic_client_find_resource(struct sol_oic_client *client,
    struct sol_network_link_addr *addr, const char *resource_type,
    bool (*resource_found_cb)(struct sol_oic_client *cli,
    struct sol_oic_resource *res,
    void *data),
    const void *data)
{
    static const char oic_well_known[] = "/oic/res";
    struct sol_coap_packet *req;
    struct find_resource_ctx *ctx;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);

    ctx = sol_util_memdup(&(struct find_resource_ctx) {
            .client = client,
            .cb = resource_found_cb,
            .data = data,
        }, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, false);

    /* Multicast discovery should be non-confirmable */
    req = sol_coap_packet_request_new(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_NONCON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        goto out_no_pkt;
    }

    if (!_set_token_and_mid(req, &ctx->token))
        goto out;

    if (sol_coap_packet_add_uri_path_option(req, oic_well_known) < 0) {
        SOL_WRN("Invalid URI: %s", oic_well_known);
        goto out;
    }

    if (resource_type) {
        char query[64];

        r = snprintf(query, sizeof(query), "rt=%s", resource_type);
        if (r < 0 || r >= (int)sizeof(query))
            goto out;

        sol_coap_add_option(req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    /* Discovery packets can't be sent through a DTLS server. */
    r = sol_coap_send_packet_with_reply(client->server, req, addr, _find_resource_reply_cb, ctx);
    if (r < 0)
        goto out_no_pkt;

    return true;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return false;
}

static bool
_resource_request_cb(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr,
    void *data)
{
    struct resource_request_ctx *ctx = data;
    CborParser parser;
    CborValue root;
    CborError err;
    uint8_t *payload;
    uint16_t payload_len;
    struct sol_oic_map_reader *map_reader = NULL;

    if (!ctx->cb)
        return false;
    if (!req || !addr) {
        ctx->cb(SOL_COAP_CODE_EMPTY, ctx->client, NULL, NULL, (void *)ctx->data);
        free(data);
        return false;
    }
    if (!_pkt_has_same_token(req, ctx->token))
        return true;

    if (!sol_oic_pkt_has_cbor_content(req))
        goto empty_payload;
    if (!sol_coap_packet_has_payload(req))
        goto empty_payload;
    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0)
        goto empty_payload;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    if (err != CborNoError || !cbor_value_is_map(&root)) {
        SOL_ERR("Error while parsing CBOR repr packet: %s",
            cbor_error_string(err));
    } else
        map_reader = (struct sol_oic_map_reader *)&root;

empty_payload:
    ctx->cb(sol_coap_header_get_code(req), ctx->client, addr, map_reader,
        (void *)ctx->data);

    return true;
}

static bool
_one_shot_resource_request_cb(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr,
    void *data)
{

    if (req && addr)
        _resource_request_cb(server, req, addr, data);
    else {
        struct resource_request_ctx *ctx = data;

        ctx->cb(SOL_COAP_CODE_EMPTY, ctx->client, NULL, NULL, (void *)ctx->data);
    }

    free(data);
    return false;
}

static bool
_resource_request_unobserve(struct sol_oic_client *client, struct sol_oic_resource *res)
{
    struct sol_coap_server *server;
    struct sol_network_link_addr addr;
    int r;

    server = _best_server_for_resource(client, res, &addr);
    r = sol_coap_unobserve_server(server, &addr,
        (uint8_t *)&res->observe.token, (uint8_t)sizeof(res->observe.token));

    return r == 0;
}

static bool
_resource_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method, sol_coap_msgtype_t msg_type,
    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *repr_map),
    void *fill_repr_map_data,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    void *data, bool observe)
{
    CborError err;
    char *href;

    bool (*cb)(struct sol_coap_server *server, struct sol_coap_packet *req, const struct sol_network_link_addr *addr, void *data);
    struct sol_coap_packet *req;
    struct sol_coap_server *server;
    struct sol_network_link_addr addr;
    struct sol_oic_map_writer map_encoder;
    struct resource_request_ctx *ctx = sol_util_memdup(&(struct resource_request_ctx) {
            .client = client,
            .cb = callback,
            .data = data,
            .res = res,
        }, sizeof(*ctx));

    SOL_NULL_CHECK(ctx, false);

    req = sol_coap_packet_request_new(method, msg_type);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        goto out_no_req;
    }

    if (!_set_token_and_mid(req, &ctx->token))
        goto out;

    if (observe) {
        uint8_t reg = 0;

        res->observe.token = ctx->token;
        sol_coap_add_option(req, SOL_COAP_OPTION_OBSERVE, &reg, sizeof(reg));
        cb = _resource_request_cb;
    } else {
        cb = _one_shot_resource_request_cb;
    }

    href = strndupa(res->href.data, res->href.len);
    if (sol_coap_packet_add_uri_path_option(req, href) < 0) {
        SOL_WRN("Invalid URI: %s", href);
        goto out;
    }

    if (fill_repr_map) {
        sol_oic_packet_cbor_create(req, &map_encoder);
        if (!fill_repr_map(fill_repr_map_data, &map_encoder))
            goto out;
        err = sol_oic_packet_cbor_close(req, &map_encoder);
        SOL_INT_CHECK_GOTO(err, != CborNoError, cbor_error);
    }
    server = _best_server_for_resource(client, res, &addr);


    if (!sol_coap_send_packet_with_reply(server, req, &addr, cb, ctx) == 0) {
        SOL_DBG("Failed to send CoAP packet through %s server (port %d)",
            server == client->dtls_server ? "secure" : "non-secure", addr.port);
        goto out;
    }

    SOL_DBG("Sending CoAP packet through %s server (port %d)",
        server == client->dtls_server ? "secure" : "non-secure", addr.port);
    return true;

cbor_error:
    SOL_ERR("Could not encode CBOR representation: %s", cbor_error_string(err));
out:
    sol_coap_packet_unref(req);
out_no_req:
    free(ctx);
    return false;
}

SOL_API bool
sol_oic_client_resource_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method,
    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *repr_map),
    void *fill_repr_map_data,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    const void *callback_data)
{
    SOL_NULL_CHECK(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    return _resource_request(client, res, method,
        SOL_COAP_TYPE_CON, fill_repr_map, fill_repr_map_data, callback,
        (void *)callback_data, false);
}

SOL_API bool
sol_oic_client_resource_non_confirmable_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method,
    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *repr_map),
    void *fill_repr_map_data,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    const void *callback_data)
{
    SOL_NULL_CHECK(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    return _resource_request(client, res, method,
        SOL_COAP_TYPE_NONCON, fill_repr_map, fill_repr_map_data, callback,
        (void *)callback_data, false);
}

static bool
_poll_resource(void *data)
{
    struct resource_request_ctx *ctx = data;
    bool r;

    if (ctx->res->observe.clear_data) {
        ctx->res->observe.clear_data--;
        free(ctx);
        return false;
    }

    r = _resource_request(ctx->client, ctx->res, SOL_COAP_METHOD_GET,
        SOL_COAP_TYPE_CON, NULL, NULL, ctx->cb, (void *)ctx->data, false);
    if (!r)
        SOL_WRN("Could not send polling packet to observable resource");

    return true;
}

static bool
_observe_with_polling(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    void *data)
{
    struct resource_request_ctx *ctx = sol_util_memdup(&(struct resource_request_ctx) {
            .client = client,
            .cb = callback,
            .data = data,
            .res = res
        }, sizeof(*ctx));

    SOL_NULL_CHECK(ctx, false);

    SOL_INF("Resource does not support observation, polling every %dms", POLL_OBSERVE_TIMEOUT_MS);
    res->observe.timeout = sol_timeout_add(POLL_OBSERVE_TIMEOUT_MS, _poll_resource, ctx);
    if (!res->observe.timeout) {
        free(ctx);
        SOL_WRN("Could not add timeout to observe resource via polling");
        return false;
    }

    sol_oic_resource_ref(res);
    return true;
}

static bool
_stop_observing_with_polling(struct sol_oic_resource *res)
{
    SOL_INF("Deactivating resource polling timer");

    /* Set timeout to NULL and increment clear_data so that context is properly freed */
    res->observe.timeout = NULL;
    res->observe.clear_data++;

    sol_oic_resource_unref(res);

    return true;
}

static bool
client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    void *data, bool observe, bool non_confirmable)
{
    SOL_NULL_CHECK(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    if (observe) {
        if (res->is_observing)
            return false;

        if (!res->observable)
            res->is_observing = _observe_with_polling(client, res, callback,
                data);
        else
            res->is_observing = _resource_request(client, res,
                SOL_COAP_METHOD_GET,
                non_confirmable ? SOL_COAP_TYPE_NONCON : SOL_COAP_TYPE_CON,
                NULL, NULL, callback, data, true);
        return res->is_observing;
    }

    if (!res->is_observing) {
        SOL_WRN("Attempting to stop observing resource without ever being "
            "observed");
        return false;
    }

    if (res->observe.timeout)
        res->is_observing = !_stop_observing_with_polling(res);
    else if (res->observable)
        res->is_observing = !_resource_request_unobserve(client, res);

    return !res->is_observing;
}

SOL_API bool
sol_oic_client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    const void *data, bool observe)
{
    return client_resource_set_observable(client, res, callback, (void *)data,
        observe, false);
}

SOL_API bool
sol_oic_client_resource_set_observable_non_confirmable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(sol_coap_responsecode_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec, void *data),
    const void *data, bool observe)
{
    return client_resource_set_observable(client, res, callback, (void *)data,
        observe, true);
}

SOL_API struct sol_oic_client *
sol_oic_client_new(void)
{
    struct sol_oic_client *client = malloc(sizeof(*client));

    SOL_NULL_CHECK(client, NULL);

    client->server = sol_coap_server_new(0);
    SOL_NULL_CHECK_GOTO(client->server, error_create_server);

    client->dtls_server = sol_coap_secure_server_new(0);
    if (!client->dtls_server) {
        SOL_INT_CHECK_GOTO(errno, != ENOSYS, error_create_dtls_server);
        SOL_INF("DTLS support not built-in, only making non-secure requests");
    }

    return client;

error_create_dtls_server:
    sol_coap_server_unref(client->server);

error_create_server:
    sol_util_secure_clear_memory(client, sizeof(*client));
    free(client);

    return NULL;
}

SOL_API void
sol_oic_client_del(struct sol_oic_client *client)
{
    SOL_NULL_CHECK(client);

    sol_coap_server_unref(client->server);

    if (client->dtls_server)
        sol_coap_server_unref(client->dtls_server);

    sol_util_secure_clear_memory(client, sizeof(*client));
    free(client);
}
