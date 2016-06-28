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
#include "sol-reentrant.h"
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
#define OIC_RESOURCE_CHECK_API(ptr, err, ...) \
    do { \
        if (SOL_UNLIKELY(ptr->api_version != \
            SOL_OIC_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle oic client resource that has unsupported " \
                "version '%" PRIu16 "', expected version is '%" PRIu16 "'", \
                ptr->api_version, SOL_OIC_RESOURCE_API_VERSION); \
            if (err) \
                errno = err; \
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

struct ctx {
    struct sol_coap_server *server;
    struct sol_oic_client *client;
    struct sol_coap_packet *req;
    const void *data;
    struct sol_network_link_addr addr;
    int64_t token;
    struct sol_reentrant reentrant;
};

struct sol_oic_resource_internal {
    struct sol_oic_resource base;
    char *types_data; //Buffer with types vector data.
    char *interfaces_data; //Buffer with interfaces vector data.
    struct {
        struct sol_timeout *timeout; //Polling timeout handler
        int clear_data; //Polling counter.
        int64_t token; //Observation token, if in observe mode
    } observe;
    int refcnt;
    /*
     * True if this client is observing the resource.
     *
     * It is expected that clients that are observing a resource
     * receive notifications when the resource state changes. */
    bool is_observed : 1;
};

struct find_resource_ctx {
    struct ctx base;
    bool (*cb)(void *data, struct sol_oic_client *cli, struct sol_oic_resource *res);
};

struct server_info_ctx {
    struct ctx base;
    void (*cb)(void *data, struct sol_oic_client *cli, const struct sol_oic_platform_info *info);
};

struct sol_oic_client_request {
    struct sol_oic_request base;
    bool (*cb)(void *data, struct sol_coap_server *server, struct sol_coap_packet *req, const struct sol_network_link_addr *addr);
    struct sol_oic_resource_internal *res;
    int64_t token;
    struct sol_oic_map_writer writer;
};

struct resource_request_ctx {
    struct ctx base;
    struct sol_oic_resource_internal *res;
    void (*cb)(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr, const struct sol_oic_map_reader *repr_vec);
    const void *data;
    int64_t token;
};

SOL_LOG_INTERNAL_DECLARE(_sol_oic_client_log_domain, "oic-client");

static struct sol_coap_server *
_best_server_for_resource(const struct sol_oic_client *client,
    struct sol_oic_resource *res,
    struct sol_network_link_addr *addr)
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

    r = sol_random_get_int64(random, token);
    if (r < 0) {
        SOL_WRN("Could not generate CoAP token");
        return r;
    }
    r = sol_random_get_int32(random, &mid);
    if (r < 0) {
        SOL_WRN("Could not generate CoAP message id");
        return r;
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
    struct sol_oic_resource_internal *res =
        (struct sol_oic_resource_internal *)r;

    SOL_NULL_CHECK(r, NULL);
    OIC_RESOURCE_CHECK_API(r, EINVAL, NULL);

    res->refcnt++;
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
    struct sol_oic_resource_internal *res =
        (struct sol_oic_resource_internal *)r;

    SOL_NULL_CHECK(r);
    OIC_RESOURCE_CHECK_API(r, 0);

    res->refcnt--;
    if (!res->refcnt) {
        free((char *)r->path.data);
        free((char *)r->device_id.data);

        clear_vector_list((struct sol_vector *)&r->types, res->types_data);
        clear_vector_list((struct sol_vector *)&r->interfaces,
            res->interfaces_data);

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

    if (!_pkt_has_same_token(req, ctx->base.token))
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
        SOL_REENTRANT_CALL(ctx->base.reentrant) {
            ctx->cb((void *)ctx->base.data, ctx->base.client, &info);
        }
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
    SOL_REENTRANT_CALL(ctx->base.reentrant) {
        ctx->cb((void *)ctx->base.data, ctx->base.client, NULL);
    }
free_ctx:
    SOL_REENTRANT_FREE(ctx->base.reentrant) {
        free(ctx);
    }
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

    if (!_pkt_has_same_token(req, ctx->base.token))
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
        SOL_REENTRANT_CALL(ctx->base.reentrant) {
            cb((void *)ctx->base.data, ctx->base.client, &info);
        }
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
    SOL_REENTRANT_CALL(ctx->base.reentrant) {
        ctx->cb((void *)ctx->base.data, ctx->base.client, NULL);
    }
free_ctx:
    SOL_REENTRANT_FREE(ctx->base.reentrant) {
        free(ctx);
    }
    return false;
}

static struct sol_oic_pending *
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
    int r = 0;

    ctx = sol_util_memdup(&(struct server_info_ctx) {
            .base.client = client,
            .base.server = server,
            .cb = info_received_cb,
            .base.data = data,
        }, sizeof(*ctx));
    SOL_NULL_CHECK_ERRNO(ctx, ENOMEM, NULL);

    memcpy(&ctx->base.addr, addr, sizeof(*addr));

    ctx->base.req = sol_coap_packet_new_request(SOL_COAP_METHOD_GET,
        SOL_COAP_MESSAGE_TYPE_CON);
    if (!ctx->base.req) {
        SOL_WRN("Could not create CoAP packet");
        r = -errno;
        goto out_no_pkt;
    }

    r = _set_token_and_mid(ctx->base.req, &ctx->base.token);
    SOL_INT_CHECK_GOTO(r, < 0, out);

    r = sol_coap_packet_add_uri_path_option(ctx->base.req, device_uri);
    if (r < 0) {
        SOL_WRN("Invalid URI: %s", device_uri);
        goto out;
    }

    r = sol_coap_send_packet_with_reply(ctx->base.server, ctx->base.req, addr,
        reply_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, out_no_pkt);

    return (struct sol_oic_pending *)ctx;

out:
    sol_coap_packet_unref(ctx->base.req);
out_no_pkt:
    SOL_REENTRANT_FREE(ctx->base.reentrant) {
        free(ctx);
    }
    errno = -r;
    return NULL;
}

SOL_API struct sol_oic_pending *
sol_oic_client_get_platform_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
    const void *data)
{
    struct sol_network_link_addr addr;
    struct sol_coap_server *server;

    SOL_LOG_INTERNAL_INIT_ONCE;
    SOL_NULL_CHECK_ERRNO(client, EINVAL, NULL);
    OIC_RESOURCE_CHECK_API(resource, EINVAL, NULL);

    server = _best_server_for_resource(client, resource,  &addr);
    return client_get_info(client, server, &addr, SOL_OIC_PLATFORM_PATH,
        _platform_info_reply_cb, info_received_cb, data);
}

SOL_API struct sol_oic_pending *
sol_oic_client_get_platform_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
    const void *data)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK_ERRNO(client, EINVAL, NULL);
    SOL_NULL_CHECK_ERRNO(addr, EINVAL, NULL);

    return client_get_info(client, client->server, addr, SOL_OIC_PLATFORM_PATH,
        _platform_info_reply_cb, info_received_cb, data);
}

SOL_API struct sol_oic_pending *
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

    SOL_NULL_CHECK_ERRNO(client, EINVAL, NULL);
    OIC_RESOURCE_CHECK_API(resource, EINVAL, NULL);

    cb = (void (*)(void *data, struct sol_oic_client *cli,
        const struct sol_oic_platform_info *info))
        info_received_cb;
    server = _best_server_for_resource(client, resource,  &addr);
    return client_get_info(client, server, &addr, SOL_OIC_DEVICE_PATH,
        _server_info_reply_cb, cb, data);
}

SOL_API struct sol_oic_pending *
sol_oic_client_get_server_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_device_info *info),
    const void *data)
{
    void (*cb)(void *data, struct sol_oic_client *cli,
        const struct sol_oic_platform_info *info);

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK_ERRNO(client, EINVAL, NULL);
    SOL_NULL_CHECK_ERRNO(addr, EINVAL, NULL);

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

static struct sol_oic_resource_internal *
_new_resource(void)
{
    struct sol_oic_resource_internal *res = malloc(sizeof(*res));
    struct sol_str_slice *path = (struct sol_str_slice *)&res->base.path;
    struct sol_str_slice *device_id =
        (struct sol_str_slice *)&res->base.device_id;
    bool *observable = (bool *)&res->base.observable;
    bool *secure = (bool *)&res->base.secure;

    SOL_NULL_CHECK(res, NULL);

    path->len = 0;
    path->data = NULL;
    device_id->len = 0;
    device_id->data = NULL;
    sol_vector_init((struct sol_vector *)&res->base.types,
        sizeof(struct sol_str_slice));
    res->types_data = NULL;
    sol_vector_init((struct sol_vector *)&res->base.interfaces,
        sizeof(struct sol_str_slice));
    res->interfaces_data = NULL;

    res->observe.timeout = NULL;
    res->observe.clear_data = 0;

    *observable = *secure = false;
    res->is_observed = false;

    res->refcnt = 1;

    SOL_SET_API_VERSION(res->base.api_version = SOL_OIC_RESOURCE_API_VERSION; )

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
    CborValue root, devices_array, resources_array, value, map;
    struct sol_oic_resource_internal *res = NULL;
    CborValue bitmap_value, secure_value;
    bool discovery_callback_result;
    struct sol_buffer device_id;
    struct sol_buffer *buf;
    CborParser parser;
    uint64_t bitmap;
    CborError err;
    size_t offset;

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
            struct sol_network_link_addr *addr_ptr;
            struct sol_str_slice *device_id_ptr;
            bool is_secure = false;
            bool *observable;
            bool *secure;

            res = _new_resource();
            SOL_NULL_CHECK_GOTO(res, error);
            observable = (bool *)&res->base.observable;
            secure = (bool *)&res->base.secure;
            addr_ptr = (struct sol_network_link_addr *)&res->base.addr;
            device_id_ptr = (struct sol_str_slice *)&res->base.device_id;

            if (sol_cbor_map_get_str_value(&resources_array, SOL_OIC_KEY_HREF,
                (struct sol_str_slice *)&res->base.path) < 0)
                goto error;

            if (!extract_list_from_map(&resources_array,
                SOL_OIC_KEY_RESOURCE_TYPES, &res->types_data,
                (struct sol_vector *)&res->base.types))
                goto error;
            if (!extract_list_from_map(&resources_array,
                SOL_OIC_KEY_INTERFACES, &res->interfaces_data,
                (struct sol_vector *)&res->base.interfaces))
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
                is_secure = false;
            } else {
                if (!cbor_value_is_boolean(&secure_value))
                    goto error;
                err = cbor_value_get_boolean(&secure_value, &is_secure);
                SOL_INT_CHECK_GOTO(err, != CborNoError, error);
            }

            *observable = (bitmap & SOL_OIC_FLAG_OBSERVABLE);
            *secure = is_secure;
            *observable |= _has_observable_option(req);
            *addr_ptr = *addr;
            device_id_ptr->data = sol_util_memdup(device_id.data,
                device_id.used);
            if (!device_id_ptr->data)
                goto error;
            device_id_ptr->len = device_id.used;
            SOL_REENTRANT_CALL((*(struct sol_reentrant *)(&(ctx->base.reentrant)))) {
                discovery_callback_result =
                    ctx->cb((void *)ctx->base.data, ctx->base.client,
                    (struct sol_oic_resource *)res);
            }

            if (!discovery_callback_result || ctx->base.reentrant.delete_me) {
                sol_oic_resource_unref((struct sol_oic_resource *)res);
                sol_buffer_fini(&device_id);
                *cb_return  = false;
                return true;
            }

            sol_oic_resource_unref((struct sol_oic_resource *)res);
        }
        sol_buffer_fini(&device_id);
    }

    return true;

error:
    sol_oic_resource_unref((struct sol_oic_resource *)res);
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
        SOL_REENTRANT_FREE(ctx->base.reentrant) {
            free(ctx);
        }
        return false;
    }

    if (!req || !addr) {
        bool discovery_callback_result;

        SOL_REENTRANT_CALL(ctx->base.reentrant) {
            discovery_callback_result =
                ctx->cb((void *)ctx->base.data, ctx->base.client, NULL);
        }
        if (!discovery_callback_result || ctx->base.reentrant.delete_me) {
            SOL_REENTRANT_FREE(ctx->base.reentrant) {
                free(ctx);
            }
            return false;
        }
        return true;
    }

    if (!_pkt_has_same_token(req, ctx->base.token)) {
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
        SOL_REENTRANT_FREE(ctx->base.reentrant) {
            free(ctx);
        }
        return cb_return;
}

SOL_API struct sol_oic_pending *
sol_oic_client_find_resources(struct sol_oic_client *client,
    struct sol_network_link_addr *addr, const char *resource_type,
    const char *resource_interface,
    bool (*resource_found_cb)(void *data, struct sol_oic_client *cli,
    struct sol_oic_resource *res),
    const void *data)
{
    char query[64];
    static const char oic_well_known[] = "/oic/res";
    struct find_resource_ctx *ctx;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK_ERRNO(client, EINVAL, NULL);

    ctx = sol_util_memdup(&(struct find_resource_ctx) {
            .base.client = client,
            .base.server = client->server,
            .cb = resource_found_cb,
            .base.data = data,
        }, sizeof(*ctx));
    SOL_NULL_CHECK_ERRNO(ctx, ENOMEM, NULL);

    memcpy(&ctx->base.addr, addr, sizeof(*addr));

    /* Multicast discovery should be non-confirmable */
    ctx->base.req = sol_coap_packet_new_request(SOL_COAP_METHOD_GET,
        SOL_COAP_MESSAGE_TYPE_NON_CON);
    if (!ctx->base.req) {
        SOL_WRN("Could not create CoAP packet");
        r = -errno;
        goto out_no_pkt;
    }

    r = _set_token_and_mid(ctx->base.req, &ctx->base.token);
    SOL_INT_CHECK_GOTO(r, < 0, out);

    r = sol_coap_packet_add_uri_path_option(ctx->base.req, oic_well_known);
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

        sol_coap_add_option(ctx->base.req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    if (resource_interface && *resource_interface) {
        r = snprintf(query, sizeof(query), "if=%s", resource_interface);
        SOL_INT_CHECK_GOTO(r, < 0, out);
        if (r >= (int)sizeof(query)) {
            r = -ERANGE;
            goto out;
        }

        sol_coap_add_option(ctx->base.req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    /* Discovery packets can't be sent through a DTLS server. */
    r = sol_coap_send_packet_with_reply(ctx->base.server, ctx->base.req, addr,
        _find_resource_reply_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, out_no_pkt);

    return (struct sol_oic_pending *)ctx;

out:
    sol_coap_packet_unref(ctx->base.req);
out_no_pkt:
    SOL_REENTRANT_FREE(ctx->base.reentrant) {
        free(ctx);
    }
    errno = -r;
    return NULL;
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
        SOL_REENTRANT_CALL(ctx->base.reentrant) {
            ctx->cb((void *)ctx->base.data, SOL_COAP_CODE_EMPTY,
                ctx->base.client, NULL, NULL);
        }
        free(data);
        return false;
    }
    if (!_pkt_has_same_token(req, ctx->base.token))
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
    SOL_REENTRANT_CALL(ctx->base.reentrant) {
        ctx->cb((void *)ctx->base.data, code, ctx->base.client, addr,
            map_reader);
    }

    return true;
}

static bool
_one_shot_resource_request_cb(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *addr)
{
    struct resource_request_ctx *ctx = data;

    if (req && addr)
        _resource_request_cb(data, server, req, addr);
    else {
        SOL_REENTRANT_CALL(ctx->base.reentrant) {
            ctx->cb((void *)ctx->base.data, SOL_COAP_CODE_EMPTY,
                ctx->base.client, NULL, NULL);
        }
    }

    /* free the ctx */
    SOL_REENTRANT_FREE(ctx->base.reentrant) {
        free(ctx);
    }
    return false;
}

static int
_resource_request_unobserve(struct sol_oic_client *client, struct sol_oic_resource *res)
{
    struct sol_coap_server *server;
    struct sol_network_link_addr addr;
    struct sol_oic_resource_internal *r =
        (struct sol_oic_resource_internal *)res;

    server = _best_server_for_resource(client, res, &addr);
    return sol_coap_unobserve_by_token(server, &addr,
        (uint8_t *)&r->observe.token, (uint8_t)sizeof(r->observe.token));
}

static struct sol_oic_pending *
_resource_request(struct sol_oic_client_request *request,
    struct sol_oic_client *client,
    void (*callback)(void *data, enum sol_coap_response_code response_code,
    struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    void *data)
{
    struct resource_request_ctx *ctx = sol_util_memdup
            (&(struct resource_request_ctx) {
                .base.client = client,
                .cb = callback,
                .base.data = data,
                .res = request->res,
                .base.token = request->token,
            }, sizeof(*ctx));
    struct sol_network_link_addr addr;
    CborError err;
    int r;

    if (!ctx) {
        errno = ENOMEM;
        return NULL;
    }

    err = sol_oic_packet_cbor_close(request->base.pkt, &request->writer);
    if (err != CborNoError) {
        errno = EINVAL;
        goto cbor_error;
    }

    ctx->base.server = _best_server_for_resource
            (client, (struct sol_oic_resource *)request->res, &addr);
    ctx->base.req = request->base.pkt;
    request->base.pkt = NULL;
    r = sol_coap_send_packet_with_reply(ctx->base.server, ctx->base.req,
        &addr, request->cb, ctx);
    if (r < 0) {
        SOL_DBG("Failed to send CoAP packet through %s server (port %d)",
            ctx->base.server == client->dtls_server ? "secure" : "non-secure",
            addr.port);
        goto error;
    }

    SOL_DBG("Sending CoAP packet through %s server (port %d)",
        ctx->base.server == client->dtls_server ? "secure" : "non-secure",
        addr.port);
    sol_oic_client_request_free((struct sol_oic_request *)request);
    return (struct sol_oic_pending *)ctx;

cbor_error:
    SOL_ERR("Could not encode CBOR representation: %s", cbor_error_string(err));
    r = -EBADMSG;
error:
    SOL_REENTRANT_FREE(ctx->base.reentrant) {
        free(ctx);
    }
    sol_oic_client_request_free((struct sol_oic_request *)request);
    errno = r;
    return NULL;
}

static struct sol_oic_request *
request_new(enum sol_coap_method method,
    enum sol_coap_message_type type,
    struct sol_oic_resource *res,
    bool is_observe)
{
    struct sol_oic_client_request *request;
    struct sol_oic_resource_internal *r =
        (struct sol_oic_resource_internal *)res;
    char *path;
    int ret = 0;

    if (type != SOL_COAP_MESSAGE_TYPE_CON && type != SOL_COAP_MESSAGE_TYPE_NON_CON) {
        SOL_WRN("Only SOL_COAP_MESSAGE_TYPE_CON and SOL_COAP_MESSAGE_TYPE_NON_CON requests are"
            " supported");
        errno = EINVAL;
        return NULL;
    }

    request = calloc(1, sizeof(struct sol_oic_client_request));
    SOL_NULL_CHECK_ERRNO(request, ENOMEM, NULL);

    request->res = r;
    request->base.pkt = sol_coap_packet_new_request(method, type);
    if (!request->base.pkt) {
        SOL_WRN("Could not create CoAP packet");
        ret = -errno;
        goto error_pkt;
    }

    ret = _set_token_and_mid(request->base.pkt, &request->token);
    SOL_INT_CHECK_GOTO(ret, < 0, error; );

    if (is_observe) {
        uint8_t reg = 0;

        r->observe.token = request->token;
        sol_coap_add_option(request->base.pkt, SOL_COAP_OPTION_OBSERVE, &reg,
            sizeof(reg));
        request->cb = _resource_request_cb;
    } else
        request->cb = _one_shot_resource_request_cb;

    path = strndupa(res->path.data, res->path.len);
    if (!path) {
        ret = -ENOMEM;
        goto error;
    }
    ret = sol_coap_packet_add_uri_path_option(request->base.pkt, path);
    if (ret < 0) {
        SOL_WRN("Invalid URI: %s", path);
        goto error;
    }

    sol_oic_packet_cbor_create(request->base.pkt, &request->writer);

    return (struct sol_oic_request *)request;

error:
    sol_coap_packet_unref(request->base.pkt);
error_pkt:
    free(request);
    errno = -ret;
    return NULL;
}

SOL_API struct sol_oic_request *
sol_oic_client_request_new(enum sol_coap_method method, struct sol_oic_resource *res)
{
    OIC_RESOURCE_CHECK_API(res, EINVAL, NULL);

    return request_new(method, SOL_COAP_MESSAGE_TYPE_CON, res, false);
}

SOL_API struct sol_oic_request *
sol_oic_client_non_confirmable_request_new(enum sol_coap_method method, struct sol_oic_resource *res)
{
    OIC_RESOURCE_CHECK_API(res, EINVAL, NULL);

    return request_new(method, SOL_COAP_MESSAGE_TYPE_NON_CON, res, false);
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

SOL_API struct sol_oic_pending *
sol_oic_client_request(struct sol_oic_client *client,
    struct sol_oic_request *request,
    void (*callback)(void *data, enum sol_coap_response_code response_code,
    struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    const void *callback_data)
{
    SOL_NULL_CHECK(client, NULL);
    CHECK_REQUEST(request, NULL);

    return _resource_request((struct sol_oic_client_request *)request, client,
        callback, (void *)callback_data);
}

SOL_API void
sol_oic_pending_cancel(struct sol_oic_pending *pending)
{
    struct ctx *ctx = (struct ctx *)pending;

    SOL_NULL_CHECK(ctx);
    if (!ctx->req || !ctx->server)
        return;

    sol_coap_cancel_send_packet(ctx->server, ctx->req, &ctx->addr);
    SOL_REENTRANT_FREE(ctx->reentrant) {
        free(ctx);
    }
}

static bool
_poll_resource(void *data)
{
    struct resource_request_ctx *ctx = data;
    struct sol_oic_request *request;

    if (ctx->res->observe.clear_data) {
        ctx->res->observe.clear_data--;
        SOL_REENTRANT_FREE(ctx->base.reentrant) {
            free(ctx);
        }
        return false;
    }

    /* FIXME: find a way to cancel any previous requests here */
    request = sol_oic_client_request_new(SOL_COAP_METHOD_GET,
        (struct sol_oic_resource *)ctx->res);
    SOL_NULL_CHECK_GOTO(request, error);

    SOL_NULL_CHECK_GOTO(
        _resource_request((struct sol_oic_client_request *)request,
        ctx->base.client, ctx->cb, (void *)ctx->base.data), error);

    return true;

error:
    SOL_WRN("Could not send polling packet to observable resource");
    return true;
}

static int
_observe_with_polling(struct sol_oic_client *client,
    struct sol_oic_resource_internal *res,
    void (*callback)(void *data,
    enum sol_coap_response_code response_code,
    struct sol_oic_client *cli,
    const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    void *data)
{
    struct resource_request_ctx *ctx = sol_util_memdup(&(struct resource_request_ctx) {
            .base.client = client,
            .cb = callback,
            .base.data = data,
            .res = res
        }, sizeof(*ctx));

    SOL_NULL_CHECK(ctx, -ENOMEM);

    SOL_INF("Resource does not support observation, polling every %dms", POLL_OBSERVE_TIMEOUT_MS);
    res->observe.timeout = sol_timeout_add(POLL_OBSERVE_TIMEOUT_MS, _poll_resource, ctx);
    if (!res->observe.timeout) {
        SOL_REENTRANT_FREE(ctx->base.reentrant) {
            free(ctx);
        }
        SOL_WRN("Could not add timeout to observe resource via polling");
        return -ENOMEM;
    }

    sol_oic_resource_ref((struct sol_oic_resource *)res);
    return 0;
}

static bool
_stop_observing_with_polling(struct sol_oic_resource_internal *res)
{
    SOL_INF("Deactivating resource polling timer");

    /* Set timeout to NULL and increment clear_data so that context is properly freed */
    res->observe.timeout = NULL;
    res->observe.clear_data++;

    sol_oic_resource_unref((struct sol_oic_resource *)res);

    return true;
}

static int
client_resource_set_observable(struct sol_oic_client *client,
    struct sol_oic_resource_internal *res,
    void (*callback)(void *data,
    enum sol_coap_response_code response_code,
    struct sol_oic_client *cli,
    const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    void *data,
    bool observe,
    bool non_confirmable)
{
    int ret = -EINVAL;
    struct sol_oic_resource *r = (struct sol_oic_resource *)res;

    SOL_NULL_CHECK(client, -EINVAL);
    SOL_NULL_CHECK(res, -EINVAL);
    OIC_RESOURCE_CHECK_API(r, 0, -EINVAL);

    if (observe) {
        if (res->is_observed)
            return -EINVAL;

        if (!res->base.observable) {
            ret = _observe_with_polling(client, res, callback, data);
            res->is_observed = (ret == 0);
        } else {
            struct sol_oic_request *request;
            struct sol_oic_pending *tmp;

            request = request_new(SOL_COAP_METHOD_GET,
                non_confirmable ? SOL_COAP_MESSAGE_TYPE_NON_CON :
                SOL_COAP_MESSAGE_TYPE_CON, r, true);
            SOL_NULL_CHECK(request, -ENOMEM);

            tmp = _resource_request((struct sol_oic_client_request *)request,
                client, callback, data);
            res->is_observed = (tmp != NULL);
            ret = res->is_observed ? 0 : -errno;
        }
        return ret;
    }

    if (!res->is_observed) {
        SOL_WRN("Attempting to stop observing resource without ever being "
            "observed");
        return -EINVAL;
    }

    if (res->observe.timeout) {
        res->is_observed = !_stop_observing_with_polling(res);
        ret = 0;
    } else if (res->base.observable) {
        ret = _resource_request_unobserve(client, r);
        if (ret == 0)
            res->is_observed = false;
    }

    return ret;
}

SOL_API int
sol_oic_client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    const void *data, bool observe)
{
    return client_resource_set_observable
               (client, (struct sol_oic_resource_internal *)res, callback,
               (void *)data, observe, false);
}

SOL_API int
sol_oic_client_resource_set_observable_non_confirmable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec),
    const void *data, bool observe)
{
    return client_resource_set_observable
               (client, (struct sol_oic_resource_internal *)res, callback,
               (void *)data, observe, true);
}

SOL_API struct sol_oic_client *
sol_oic_client_new(void)
{
    struct sol_oic_client *client = malloc(sizeof(*client));
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = 0 };
    int r = 0;


    SOL_NULL_CHECK_ERRNO(client, EINVAL, NULL);

    client->server = sol_coap_server_new(&servaddr, false);
    if (!client->server) {
        r = -errno;
        goto error_create_server;
    }

    client->dtls_server = sol_coap_server_new(&servaddr, true);
    if (!client->dtls_server) {
        client->security = NULL;

        SOL_INT_CHECK_GOTO(errno, != ENOSYS, error_create_dtls_server);
        SOL_INF("DTLS support not built-in, only making non-secure requests");
        r = -errno;
    } else {
        client->security = sol_oic_client_security_add(client->server,
            client->dtls_server);
        if (!client->security) {
            r = -errno;
            SOL_WRN("Could not enable security features for OIC client");
        }
    }

    return client;

error_create_dtls_server:
    sol_coap_server_unref(client->server);

error_create_server:
    sol_util_clear_memory_secure(client, sizeof(*client));
    free(client);
    errno = -r;
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
