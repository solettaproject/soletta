/*
 * This file is part of the Soletta Project
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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tinycbor/cbor.h"
#include "sol-coap.h"
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-random.h"
#include "sol-util-internal.h"

#include "sol-oic-client.h"
#include "sol-oic-cbor.h"
#include "sol-oic.h"
#include "sol-oic-common.h"
#include "sol-oic-server.h"
#include "sol-oic-security.h"

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

#define CHECK_REQUEST(request, ...) \
    if (!request || request->is_server_request) { \
        SOL_WRN("Request packet is not a request create by oic client"); \
        return __VA_ARGS__; \
    }

struct sol_oic_client {
    struct sol_coap_server *server;
    struct sol_coap_server *dtls_server;
    struct sol_oic_security *security;
};

struct find_resource_ctx {
    struct sol_oic_client *client;
    bool (*cb)(void *data, struct sol_oic_client *cli, struct sol_oic_resource *res);
    const void *data;
    int64_t token;
};

struct server_info_ctx {
    struct sol_oic_client *client;
    void (*cb)(void *data, struct sol_oic_client *cli, const struct sol_oic_platform_info *info);
    const void *data;
    int64_t token;
};

struct sol_oic_client_request {
    struct sol_oic_request base;
    bool (*cb)(void *data, struct sol_coap_server *server, struct sol_coap_packet *req, const struct sol_network_link_addr *addr);
    struct sol_oic_resource *res;
    int64_t token;
    struct sol_oic_map_writer writer;
};

struct resource_request_ctx {
    struct sol_oic_client *client;
    struct sol_oic_resource *res;
    void (*cb)(void *data, sol_coap_response_code_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr, const struct sol_oic_map_reader *repr_vec);
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

static int
_set_token_and_mid(struct sol_coap_packet *pkt, int64_t *token)
{
    static struct sol_random *random = NULL;
    int32_t mid;
    int r;

    if (SOL_UNLIKELY(!random)) {
        random = sol_random_new(SOL_RANDOM_DEFAULT, 0);
        SOL_NULL_CHECK(random, -ENOMEM);
    }

    if (!sol_random_get_int64(random, token)) {
        SOL_WRN("Could not generate CoAP token");
        return -EIO;
    }
    if (!sol_random_get_int32(random, &mid)) {
        SOL_WRN("Could not generate CoAP message id");
        return -EIO;
    }

    r = sol_coap_header_set_token(pkt, (uint8_t *)token,
        (uint8_t)sizeof(*token));
    if (r < 0) {
        SOL_WRN("Could not set CoAP packet token");
        return r;
    }

    r = sol_coap_header_set_id(pkt, (int16_t)mid);
    if (r < 0) {
        SOL_WRN("Could not set CoAP header ID");
        return r;
    }

    return 0;
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

static void
clear_vector_list(struct sol_vector *vector, void *data)
{
    uint16_t i;
    struct sol_str_slice *str;

    if (data)
        free(data);
    else {
        SOL_VECTOR_FOREACH_IDX (vector, str, i)
            free((char *)str->data);
    }
    sol_vector_clear(vector);
}

SOL_API void
sol_oic_resource_unref(struct sol_oic_resource *r)
{
    SOL_NULL_CHECK(r);
    OIC_RESOURCE_CHECK_API(r);

    r->refcnt--;
    if (!r->refcnt) {
        free((char *)r->path.data);
        free((char *)r->device_id.data);

        clear_vector_list(&r->types, r->types_data);
        clear_vector_list(&r->interfaces, r->interfaces_data);

        free(r);
    }
}

static bool
_parse_platform_info_payload(struct sol_oic_platform_info *info,
    uint8_t *payload, uint16_t payload_len)
{
    CborParser parser;
    CborError err;
    CborValue root;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);

    if (!cbor_value_is_map(&root))
        return false;

    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_PLATFORM_ID,
        &info->platform_id) < 0)
        return false;
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MANUF_NAME,
        &info->manufacturer_name) < 0) {
        free((char *)info->platform_id.data);
        return false;
    }
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MANUF_URL,
        &info->manufacturer_url) < 0)
        info->manufacturer_url = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MODEL_NUM,
        &info->model_number) < 0)
        info->model_number = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_MANUF_DATE,
        &info->manufacture_date) < 0)
        info->manufacture_date = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_PLATFORM_VER,
        &info->platform_version) < 0)
        info->platform_version = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_OS_VER,
        &info->os_version) < 0)
        info->os_version = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_HW_VER,
        &info->hardware_version) < 0)
        info->hardware_version = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_FIRMWARE_VER,
        &info->firmware_version) < 0)
        info->firmware_version = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_SUPPORT_URL,
        &info->support_url) < 0)
        info->support_url = SOL_STR_SLICE_STR(NULL, 0);
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_SYSTEM_TIME,
        &info->system_time) < 0)
        info->system_time = SOL_STR_SLICE_STR(NULL, 0);

    return true;
}

static bool
extract_device_id(CborValue *map, struct sol_buffer *device_id)
{
    CborValue value;
    CborError err;
    struct sol_str_slice slice;
    int r;

    sol_buffer_init_flags(device_id, NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    if (cbor_value_map_find_value(map, SOL_OIC_KEY_DEVICE_ID, &value) != CborNoError)
        return false;

    if (cbor_value_is_byte_string(&value))
        return cbor_value_dup_byte_string(&value, (uint8_t **)&device_id->data,
            &device_id->used, NULL) == CborNoError;

    if (cbor_value_is_text_string(&value)) {
        err = cbor_value_dup_text_string(&value, (char **)&slice.data,
            &slice.len, NULL);
        SOL_INT_CHECK(err, != CborNoError, false);
        r = sol_util_uuid_bytes_from_string(slice, device_id);
        free((char *)slice.data);
        return r == 0;
    }

    return false;
}

static bool
_parse_server_info_payload(struct sol_oic_device_info *info,
    uint8_t *payload, uint16_t payload_len)
{
    CborParser parser;
    CborError err;
    CborValue root;
    struct sol_buffer device_id;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);

    if (!cbor_value_is_map(&root))
        return false;

    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_SPEC_VERSION,
        &info->spec_version) < 0)
        return false;

    if (!extract_device_id(&root, &device_id))
        goto error;
    info->device_id.data = sol_buffer_steal_or_copy(&device_id,
        &info->device_id.len);
    sol_buffer_fini(&device_id);
    SOL_NULL_CHECK_GOTO(info->device_id.data, error);

    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_DATA_MODEL_VERSION,
        &info->data_model_version) < 0)
        goto error;

    //TODO: This field should be mandatory, but letting it optional to make it
    //compatible with iotivity 1.0.1
    if (sol_cbor_map_get_str_value(&root, SOL_OIC_KEY_DEVICE_NAME,
        &info->device_name) < 0)
        info->device_name = SOL_STR_SLICE_STR(NULL, 0);

    return true;

error:
    free((char *)info->spec_version.data);
    free((char *)info->device_id.data);
    return false;
}

static bool
_platform_info_reply_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr)
{
    struct sol_oic_platform_info info = { 0 };
    struct server_info_ctx *ctx = data;
    struct sol_buffer *buf;
    size_t offset;

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

    if (sol_coap_packet_get_payload(req, &buf, &offset) < 0) {
        SOL_WRN("Could not get pkt payload");
        goto error;
    }

    if (_parse_platform_info_payload(&info, sol_buffer_at(buf, offset),
        buf->used - offset)) {
        SOL_SET_API_VERSION(info.api_version = SOL_OIC_PLATFORM_INFO_API_VERSION; )
        ctx->cb((void *)ctx->data, ctx->client, &info);
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
    ctx->cb((char *)ctx->data, ctx->client, NULL);
free_ctx:
    free(ctx);
    return false;
}

static bool
_server_info_reply_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr)
{
    struct server_info_ctx *ctx = data;
    struct sol_buffer *buf;
    size_t offset;
    struct sol_oic_device_info info = { 0 };

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

    if (sol_coap_packet_get_payload(req, &buf, &offset) < 0) {
        SOL_WRN("Could not get pkt payload");
        goto error;
    }

    if (_parse_server_info_payload(&info, sol_buffer_at(buf, offset),
        buf->used - offset)) {
        void (*cb)(void *data, struct sol_oic_client *cli,
            const struct sol_oic_device_info *info);

        SOL_SET_API_VERSION(info.api_version = SOL_OIC_DEVICE_INFO_API_VERSION; )
        cb = (void (*)(void *data, struct sol_oic_client *cli,
            const struct sol_oic_device_info *info))ctx->cb;
        cb((void *)ctx->data, ctx->client, &info);
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
    ctx->cb((void *)ctx->data, ctx->client, NULL);
free_ctx:
    free(ctx);
    return false;
}

static int
client_get_info(struct sol_oic_client *client,
    struct sol_coap_server *server,
    struct sol_network_link_addr *addr,
    const char *device_uri,
    bool (*reply_cb)(void *data, struct sol_coap_server *server, struct sol_coap_packet *req, const struct sol_network_link_addr *addr),
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
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
    SOL_NULL_CHECK(ctx, -ENOMEM);

    req = sol_coap_packet_new_request(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_CON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        r = -errno;
        goto out_no_pkt;
    }

    r = _set_token_and_mid(req, &ctx->token);
    SOL_INT_CHECK_GOTO(r, < 0, out);

    r = sol_coap_packet_add_uri_path_option(req, device_uri);
    if (r < 0) {
        SOL_WRN("Invalid URI: %s", device_uri);
        goto out;
    }

    r = sol_coap_send_packet_with_reply(server, req, addr, reply_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, out_no_pkt);

    return 0;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return r;
}

SOL_API int
sol_oic_client_get_platform_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
    const void *data)
{
    struct sol_network_link_addr addr;
    struct sol_coap_server *server;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, -EINVAL);
    OIC_RESOURCE_CHECK_API(resource, -EINVAL);

    server = _best_server_for_resource(client, resource,  &addr);
    return client_get_info(client, server, &addr, SOL_OIC_PLATFORM_PATH,
        _platform_info_reply_cb, info_received_cb, data);
}

SOL_API int
sol_oic_client_get_platform_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
    const void *data)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(addr, -EINVAL);

    return client_get_info(client, client->server, addr, SOL_OIC_PLATFORM_PATH,
        _platform_info_reply_cb, info_received_cb, data);
}

SOL_API int
sol_oic_client_get_server_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_device_info *info),
    const void *data)
{
    struct sol_network_link_addr addr;
    struct sol_coap_server *server;

    void (*cb)(void *data, struct sol_oic_client *cli,
        const struct sol_oic_platform_info *info);

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, -EINVAL);
    OIC_RESOURCE_CHECK_API(resource, -EINVAL);

    cb = (void (*)(void *data, struct sol_oic_client *cli,
        const struct sol_oic_platform_info *info))
        info_received_cb;
    server = _best_server_for_resource(client, resource,  &addr);
    return client_get_info(client, server, &addr, SOL_OIC_DEVICE_PATH,
        _server_info_reply_cb, cb, data);
}

SOL_API int
sol_oic_client_get_server_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_device_info *info),
    const void *data)
{
    void (*cb)(void *data, struct sol_oic_client *cli,
        const struct sol_oic_platform_info *info);

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(addr, -EINVAL);

    cb = (void (*)(void *data, struct sol_oic_client *cli,
        const struct sol_oic_platform_info *info))
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

    res->path = SOL_STR_SLICE_STR(NULL, 0);
    res->device_id = SOL_STR_SLICE_STR(NULL, 0);
    sol_vector_init(&res->types, sizeof(struct sol_str_slice));
    res->types_data = NULL;
    sol_vector_init(&res->interfaces, sizeof(struct sol_str_slice));
    res->interfaces_data = NULL;

    res->observe.timeout = NULL;
    res->observe.clear_data = 0;

    res->observable = false;
    res->secure = false;
    res->is_observed = false;

    res->refcnt = 1;

    SOL_SET_API_VERSION(res->api_version = SOL_OIC_RESOURCE_API_VERSION; )

    return res;
}

static int
extract_list_from_map(const CborValue *map, const char *key, char **data, struct sol_vector *vector)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    if (cbor_value_is_text_string(&value))
        return sol_cbor_bsv_to_vector(&value, data, vector) == 0;

    if (cbor_value_is_array(&value)) {
        *data = NULL;
        return sol_cbor_array_to_vector(&value, vector) == 0;
    }

    return false;
}

static bool
_iterate_over_resource_reply_payload(struct sol_coap_packet *req,
    const struct sol_network_link_addr *addr,
    const struct find_resource_ctx *ctx, bool *cb_return)
{
    CborParser parser;
    CborError err;
    CborValue root, devices_array, resources_array, value, map;
    struct sol_buffer *buf;
    size_t offset;
    struct sol_buffer device_id;
    struct sol_oic_resource *res = NULL;
    CborValue bitmap_value, secure_value;
    uint64_t bitmap;
    bool secure;


    *cb_return  = true;

    if (sol_coap_packet_get_payload(req, &buf, &offset) < 0) {
        SOL_WRN("Could not get payload form discovery packet response");
        return false;
    }

    err = cbor_parser_init(sol_buffer_at(buf, offset), buf->used - offset,
        0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);
    if (!cbor_value_is_array(&root))
        return false;

    err = cbor_value_enter_container(&root, &devices_array);
    SOL_INT_CHECK(err, != CborNoError, false);

    for (; cbor_value_is_map(&devices_array) && err == CborNoError;
        err = cbor_value_advance(&devices_array)) {
        SOL_INT_CHECK(err, != CborNoError, false);
        if (!extract_device_id(&devices_array, &device_id))
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

            if (sol_cbor_map_get_str_value(&resources_array, SOL_OIC_KEY_HREF,
                &res->path) < 0)
                goto error;

            if (!extract_list_from_map(&resources_array,
                SOL_OIC_KEY_RESOURCE_TYPES, &res->types_data, &res->types))
                goto error;
            if (!extract_list_from_map(&resources_array,
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
                device_id.used);
            if (!res->device_id.data)
                goto error;
            res->device_id.len = device_id.used;
            if (!ctx->cb((void *)ctx->data, ctx->client, res)) {
                sol_oic_resource_unref(res);
                sol_buffer_fini(&device_id);
                *cb_return  = false;
                return true;
            }

            sol_oic_resource_unref(res);
        }
        sol_buffer_fini(&device_id);
    }

    return true;

error:
    sol_oic_resource_unref(res);
    sol_buffer_fini(&device_id);
    return false;
}

static bool
_find_resource_reply_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr)
{
    struct find_resource_ctx *ctx = data;
    bool cb_return;

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        free(ctx);
        return false;
    }

    if (!req || !addr) {
        if (!ctx->cb((void *)ctx->data, ctx->client, NULL)) {
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

SOL_API int
sol_oic_client_find_resource(struct sol_oic_client *client,
    struct sol_network_link_addr *addr, const char *resource_type,
    const char *resource_interface,
    bool (*resource_found_cb)(void *data, struct sol_oic_client *cli,
    struct sol_oic_resource *res),
    const void *data)
{
    char query[64];
    static const char oic_well_known[] = "/oic/res";
    struct sol_coap_packet *req;
    struct find_resource_ctx *ctx;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, -EINVAL);

    ctx = sol_util_memdup(&(struct find_resource_ctx) {
        .client = client,
        .cb = resource_found_cb,
        .data = data,
    }, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, -ENOMEM);

    /* Multicast discovery should be non-confirmable */
    req = sol_coap_packet_new_request(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_NON_CON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        r = -errno;
        goto out_no_pkt;
    }

    r = _set_token_and_mid(req, &ctx->token);
    SOL_INT_CHECK_GOTO(r, < 0, out);

    r = sol_coap_packet_add_uri_path_option(req, oic_well_known);
    if (r < 0) {
        SOL_WRN("Invalid URI: %s", oic_well_known);
        goto out;
    }

    if (resource_type && *resource_type) {
        r = snprintf(query, sizeof(query), "rt=%s", resource_type);
        SOL_INT_CHECK_GOTO(r, < 0, out);
        if (r >= (int)sizeof(query)) {
            r = -ERANGE;
            goto out;
        }

        sol_coap_add_option(req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    if (resource_interface && *resource_interface) {
        r = snprintf(query, sizeof(query), "if=%s", resource_interface);
        SOL_INT_CHECK_GOTO(r, < 0, out);
        if (r >= (int)sizeof(query)) {
            r = -ERANGE;
            goto out;
        }

        sol_coap_add_option(req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    /* Discovery packets can't be sent through a DTLS server. */
    r = sol_coap_send_packet_with_reply(client->server, req, addr, _find_resource_reply_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, out_no_pkt);

    return 0;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return r;
}

static bool
_resource_request_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr)
{
    struct resource_request_ctx *ctx = data;
    CborParser parser;
    CborValue root;
    CborError err;
    struct sol_buffer *buf;
    size_t offset;
    struct sol_oic_map_reader *map_reader = NULL;
    uint8_t code;

    if (!ctx->cb)
        return false;
    if (!req || !addr) {
        ctx->cb((void *)ctx->data, SOL_COAP_CODE_EMPTY, ctx->client, NULL, NULL);
        free(data);
        return false;
    }
    if (!_pkt_has_same_token(req, ctx->token))
        return true;

    if (!sol_oic_pkt_has_cbor_content(req))
        goto empty_payload;
    if (!sol_coap_packet_has_payload(req))
        goto empty_payload;
    if (sol_coap_packet_get_payload(req, &buf, &offset) < 0)
        goto empty_payload;

    err = cbor_parser_init(sol_buffer_at(buf, offset), buf->used - offset,
        0, &parser, &root);
    if (err != CborNoError || !cbor_value_is_map(&root)) {
        SOL_ERR("Error while parsing CBOR repr packet: %s",
            cbor_error_string(err));
    } else
        map_reader = (struct sol_oic_map_reader *)&root;

empty_payload:
    sol_coap_header_get_code(req, &code);
    ctx->cb((void *)ctx->data, code, ctx->client, addr, map_reader);

    return true;
}

static bool
_one_shot_resource_request_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr)
{

    if (req && addr)
        _resource_request_cb(data, server, req, addr);
    else {
        struct resource_request_ctx *ctx = data;

        ctx->cb((void *)ctx->data, SOL_COAP_CODE_EMPTY, ctx->client, NULL, NULL);
    }

    free(data);
    return false;
}

static int
_resource_request_unobserve(struct sol_oic_client *client, struct sol_oic_resource *res)
{
    struct sol_coap_server *server;
    struct sol_network_link_addr addr;

    server = _best_server_for_resource(client, res, &addr);
    return sol_coap_unobserve_by_token(server, &addr,
        (uint8_t *)&res->observe.token, (uint8_t)sizeof(res->observe.token));
}

static int
_resource_request(struct sol_oic_client_request *request,
    struct sol_oic_client *client,
    void (*callback)(void *data, sol_coap_response_code_t response_code,
    struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    void *data)
{
    CborError err;
    struct sol_coap_server *server;
    struct sol_network_link_addr addr;
    struct sol_coap_packet *req;
    int r;
    struct resource_request_ctx *ctx = sol_util_memdup(&(struct resource_request_ctx) {
        .client = client,
        .cb = callback,
        .data = data,
        .res = request->res,
        .token = request->token,
    }, sizeof(*ctx));

    SOL_NULL_CHECK(ctx, -errno);

    err = sol_oic_packet_cbor_close(request->base.pkt, &request->writer);
    SOL_INT_CHECK_GOTO(err, != CborNoError, cbor_error);

    server = _best_server_for_resource(client, request->res, &addr);
    req = request->base.pkt;
    request->base.pkt = NULL;
    r = sol_coap_send_packet_with_reply(server, req, &addr, request->cb, ctx);
    if (r < 0) {
        SOL_DBG("Failed to send CoAP packet through %s server (port %d)",
            server == client->dtls_server ? "secure" : "non-secure", addr.port);
        goto error;
    }

    SOL_DBG("Sending CoAP packet through %s server (port %d)",
        server == client->dtls_server ? "secure" : "non-secure", addr.port);
    sol_oic_client_request_free((struct sol_oic_request *)request);
    return 0;

cbor_error:
    SOL_ERR("Could not encode CBOR representation: %s", cbor_error_string(err));
    r = -ECOMM;
error:
    free(ctx);
    sol_oic_client_request_free((struct sol_oic_request *)request);
    return r;
}

static struct sol_oic_request *
request_new(sol_coap_method_t method, sol_coap_msgtype_t type, struct sol_oic_resource *res, bool is_observe)
{
    struct sol_oic_client_request *request;
    char *path;

    if (type != SOL_COAP_TYPE_CON && type != SOL_COAP_TYPE_NON_CON) {
        SOL_WRN("Only SOL_COAP_TYPE_CON and SOL_COAP_TYPE_NON_CON requests are"
            " supported");
        return NULL;
    }

    request = calloc(1, sizeof(struct sol_oic_client_request));
    SOL_NULL_CHECK(request, NULL);

    request->res = res;
    request->base.pkt = sol_coap_packet_new_request(method, type);
    SOL_NULL_CHECK_GOTO(request->base.pkt, error_pkt);

    if (_set_token_and_mid(request->base.pkt, &request->token) < 0)
        goto error;

    if (is_observe) {
        uint8_t reg = 0;

        res->observe.token = request->token;
        sol_coap_add_option(request->base.pkt, SOL_COAP_OPTION_OBSERVE, &reg,
            sizeof(reg));
        request->cb = _resource_request_cb;
    } else
        request->cb = _one_shot_resource_request_cb;

    path = strndupa(res->path.data, res->path.len);
    if (sol_coap_packet_add_uri_path_option(request->base.pkt, path) < 0) {
        SOL_WRN("Invalid URI: %s", path);
        goto error;
    }

    sol_oic_packet_cbor_create(request->base.pkt, &request->writer);

    return (struct sol_oic_request *)request;

error:
    sol_coap_packet_unref(request->base.pkt);
error_pkt:
    free(request);
    return NULL;
}

SOL_API struct sol_oic_request *
sol_oic_client_request_new(sol_coap_method_t method, struct sol_oic_resource *res)
{
    OIC_RESOURCE_CHECK_API(res, NULL);

    return request_new(method, SOL_COAP_TYPE_CON, res, false);
}

SOL_API struct sol_oic_request *
sol_oic_client_non_confirmable_request_new(sol_coap_method_t method, struct sol_oic_resource *res)
{
    OIC_RESOURCE_CHECK_API(res, NULL);

    return request_new(method, SOL_COAP_TYPE_NON_CON, res, false);
}

SOL_API void
sol_oic_client_request_free(struct sol_oic_request *request)
{
    if (!request)
        return;

    oic_request_free((struct sol_oic_request *)request);
}

SOL_API struct sol_oic_map_writer *
sol_oic_client_request_get_writer(struct sol_oic_request *request)
{
    CHECK_REQUEST(request, NULL);

    return &((struct sol_oic_client_request *)request)->writer;
}

SOL_API int
sol_oic_client_request(struct sol_oic_client *client,
    struct sol_oic_request *request,
    void (*callback)(void *data, sol_coap_response_code_t response_code,
    struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    const void *callback_data)
{
    SOL_NULL_CHECK(client, -EINVAL);
    CHECK_REQUEST(request, -EINVAL);

    return _resource_request((struct sol_oic_client_request *)request, client,
        callback, (void *)callback_data);
}

static bool
_poll_resource(void *data)
{
    struct resource_request_ctx *ctx = data;
    struct sol_oic_request *request;
    int r;

    if (ctx->res->observe.clear_data) {
        ctx->res->observe.clear_data--;
        free(ctx);
        return false;
    }

    request = sol_oic_client_request_new(SOL_COAP_METHOD_GET, ctx->res);
    SOL_NULL_CHECK_GOTO(request, error);

    r = _resource_request((struct sol_oic_client_request *)request,
        ctx->client, ctx->cb, (void *)ctx->data);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return true;

error:
    SOL_WRN("Could not send polling packet to observable resource");
    return true;
}

static bool
_observe_with_polling(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, sol_coap_response_code_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
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

static int
client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, sol_coap_response_code_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    void *data, bool observe, bool non_confirmable)
{
    int r;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(res, -EINVAL);
    OIC_RESOURCE_CHECK_API(res, -EINVAL);

    if (observe) {
        if (res->is_observed)
            return -EINVAL;

        if (!res->observable) {
            res->is_observed = _observe_with_polling(client, res, callback,
                data);
            r = res->is_observed ? 0 : -ECOMM;
        } else {
            struct sol_oic_request *request;

            request = request_new(SOL_COAP_METHOD_GET,
                non_confirmable ? SOL_COAP_TYPE_NON_CON : SOL_COAP_TYPE_CON,
                res, true);
            SOL_NULL_CHECK(request, -ENOMEM);

            r = _resource_request((struct sol_oic_client_request *)request, client, callback, data);
            res->is_observed = (r == 0);
        }
        return r;
    }

    if (!res->is_observed) {
        SOL_WRN("Attempting to stop observing resource without ever being "
            "observed");
        return -EINVAL;
    }

    if (res->observe.timeout)
        res->is_observed = !_stop_observing_with_polling(res);
    else if (res->observable)
        res->is_observed = (_resource_request_unobserve(client, res) == 0);

    return res->is_observed ? -ECOMM : 0;
}

SOL_API int
sol_oic_client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, sol_coap_response_code_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    const void *data, bool observe)
{
    return client_resource_set_observable(client, res, callback, (void *)data,
        observe, false);
}

SOL_API int
sol_oic_client_resource_set_observable_non_confirmable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, sol_coap_response_code_t response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    const void *data, bool observe)
{
    return client_resource_set_observable(client, res, callback, (void *)data,
        observe, true);
}

SOL_API struct sol_oic_client *
sol_oic_client_new(void)
{
    struct sol_oic_client *client = malloc(sizeof(*client));
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = 0 };


    SOL_NULL_CHECK(client, NULL);

    client->server = sol_coap_server_new(&servaddr);
    SOL_NULL_CHECK_GOTO(client->server, error_create_server);

    client->dtls_server = sol_coap_secure_server_new(&servaddr);
    if (!client->dtls_server) {
        client->security = NULL;

        SOL_INT_CHECK_GOTO(errno, != ENOSYS, error_create_dtls_server);
        SOL_INF("DTLS support not built-in, only making non-secure requests");
    } else {
        client->security = sol_oic_client_security_add(client->server,
            client->dtls_server);
        if (!client->security)
            SOL_WRN("Could not enable security features for OIC client");
    }

    return client;

error_create_dtls_server:
    sol_coap_server_unref(client->server);

error_create_server:
    sol_util_clear_memory_secure(client, sizeof(*client));
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

    sol_util_clear_memory_secure(client, sizeof(*client));
    free(client);
}
