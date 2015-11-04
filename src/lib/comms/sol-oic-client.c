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
#include <sys/socket.h>

#include "cbor.h"
#include "sol-coap.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-random.h"
#include "sol-util.h"

#include "sol-oic-client.h"
#include "sol-oic-cbor.h"
#include "sol-oic-common.h"
#include "sol-oic-server.h"

#define POLL_OBSERVE_TIMEOUT_MS 10000
#define DISCOVERY_RESPONSE_TIMEOUT_MS 10000

#define OIC_RESOURCE_CHECK_API(ptr, ...) \
    do {                                        \
        if (unlikely(ptr->api_version != \
            SOL_OIC_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle oic client resource that has unsupported " \
                "version '%u', expected version is '%u'", \
                ptr->api_version, SOL_OIC_RESOURCE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define OIC_CLIENT_CHECK_API(ptr, ...) \
    do {                                        \
        if (unlikely(ptr->api_version != SOL_OIC_CLIENT_API_VERSION)) { \
            SOL_WRN("Couldn't handle oic client that has unsupported " \
                "version '%u', expected version is '%u'", \
                ptr->api_version, SOL_OIC_CLIENT_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)

struct find_resource_ctx {
    struct sol_oic_client *client;
    void (*cb)(struct sol_oic_client *cli, struct sol_oic_resource *res, void *data);
    void *data;
    int64_t token;
};

struct server_info_ctx {
    struct sol_oic_client *client;
    void (*cb)(struct sol_oic_client *cli, const struct sol_oic_server_information *info, void *data);
    void *data;
    int64_t token;
};

struct resource_request_ctx {
    struct sol_oic_client *client;
    struct sol_oic_resource *res;
    void (*cb)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
        const struct sol_str_slice *href, const struct sol_vector *reprs, void *data);
    void *data;
    int64_t token;
};

SOL_LOG_INTERNAL_DECLARE(_sol_oic_client_log_domain, "oic-client");

static struct sol_ptr_vector pending_discovery = SOL_PTR_VECTOR_INIT;

static struct sol_coap_server *
_best_server_for_resource(const struct sol_oic_client *client,
    const struct sol_oic_resource *res, struct sol_network_link_addr *addr)
{
    *addr = res->addr;

    if (client->dtls_server && res->secure) {
        addr->port = 5684;
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

    if (unlikely(!random)) {
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
    if (unlikely(!token_data))
        return false;

    if (unlikely(token_len != sizeof(token)))
        return false;

    return likely(memcmp(token_data, &token, sizeof(token)) == 0);
}

static bool
_cbor_array_to_vector(CborValue *array, struct sol_vector *vector)
{
    CborError err;
    CborValue iter;

    for (err = cbor_value_enter_container(array, &iter);
        cbor_value_is_text_string(&iter) && err == CborNoError;
        err |= cbor_value_advance(&iter)) {
        struct sol_str_slice *slice = sol_vector_append(vector);

        if (!slice) {
            err = CborErrorOutOfMemory;
            break;
        }

        err |= cbor_value_dup_text_string(&iter, (char **)&slice->data, &slice->len, NULL);
    }

    return (err | cbor_value_leave_container(array, &iter)) == CborNoError;
}

static bool
_cbor_map_get_array(const CborValue *map, const char *key,
    struct sol_vector *vector)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    if (!cbor_value_is_array(&value))
        return false;

    return _cbor_array_to_vector(&value, vector);
}

static bool
_cbor_map_get_str_value(const CborValue *map, const char *key,
    struct sol_str_slice *slice)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    return cbor_value_dup_text_string(&value, (char **)&slice->data, &slice->len, NULL) == CborNoError;
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
        struct sol_str_slice *slice;
        uint16_t idx;

        free((char *)r->href.data);
        free((char *)r->device_id.data);

        SOL_VECTOR_FOREACH_IDX (&r->types, slice, idx)
            free((char *)slice->data);
        sol_vector_clear(&r->types);

        SOL_VECTOR_FOREACH_IDX (&r->interfaces, slice, idx)
            free((char *)slice->data);
        sol_vector_clear(&r->interfaces);

        free(r);
    }
}

static bool
_parse_server_info_payload(struct sol_oic_server_information *info,
    uint8_t *payload, uint16_t payload_len)
{
    CborParser parser;
    CborError err;
    CborValue root, array, value, map;
    int payload_type;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    if (err != CborNoError)
        return false;
    if (!cbor_value_is_array(&root))
        return false;

    err |= cbor_value_enter_container(&root, &array);

    err |= cbor_value_get_int(&array, &payload_type);
    err |= cbor_value_advance_fixed(&array);
    if (err != CborNoError)
        return false;
    if (payload_type != SOL_OIC_PAYLOAD_PLATFORM)
        return false;

    if (!cbor_value_is_map(&array))
        return false;

    /* href is intentionally ignored */

    err |= cbor_value_map_find_value(&map, SOL_OIC_KEY_REPRESENTATION, &value);
    if (!cbor_value_is_map(&value))
        return false;

    if (err != CborNoError)
        return false;

    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_PLATFORM_ID, &info->platform_id))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_MANUF_NAME, &info->manufacturer_name))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_MANUF_URL, &info->manufacturer_url))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_MODEL_NUM, &info->model_number))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_MANUF_DATE, &info->manufacture_date))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_PLATFORM_VER, &info->platform_version))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_OS_VER, &info->os_version))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_HW_VER, &info->hardware_version))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_FIRMWARE_VER, &info->firmware_version))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_SUPPORT_URL, &info->support_url))
        return false;
    if (!_cbor_map_get_str_value(&value, SOL_OIC_KEY_SYSTEM_TIME, &info->system_time))
        return false;

    return err == CborNoError;
}

static int
_server_info_reply_cb(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct server_info_ctx *ctx = data;
    uint8_t *payload;
    uint16_t payload_len;
    struct sol_oic_server_information info = { 0 };
    int error = 0;

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        error = -ENOENT;
        goto free_ctx;
    }

    if (!_pkt_has_same_token(req, ctx->token)) {
        error = -EINVAL;
        goto free_ctx;
    }

    if (!sol_oic_pkt_has_cbor_content(req)) {
        error = -EINVAL;
        goto free_ctx;
    }

    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        SOL_WRN("Could not get pkt payload");
        error = -ENOMEM;
        goto free_ctx;
    }

    if (_parse_server_info_payload(&info, payload, payload_len)) {
        info.api_version = SOL_OIC_SERVER_INFORMATION_API_VERSION;
        ctx->cb(ctx->client, &info, ctx->data);
    } else {
        SOL_WRN("Could not parse payload");
        error = -EINVAL;
    }

    free((char *)info.platform_id.data);
    free((char *)info.manufacturer_name.data);
    free((char *)info.manufacturer_url.data);
    free((char *)info.model_number.data);
    free((char *)info.manufacture_date.data);
    free((char *)info.platform_version.data);
    free((char *)info.os_version.data);
    free((char *)info.hardware_version.data);
    free((char *)info.support_url.data);
    free((char *)info.system_time.data);

free_ctx:
    free(ctx);
    return error;
}

SOL_API bool
sol_oic_client_get_server_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_server_information *info, void *data),
    void *data)
{
    static const char device_uri[] = "/oic/d";
    struct server_info_ctx *ctx;
    struct sol_coap_packet *req;
    struct sol_coap_server *server;
    struct sol_network_link_addr addr;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);
    OIC_RESOURCE_CHECK_API(resource, false);

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

    server = _best_server_for_resource(client, resource,  &addr);
    r = sol_coap_send_packet_with_reply(server, req, &addr, _server_info_reply_cb, ctx);
    if (!r)
        return true;

    goto out_no_pkt;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return false;
}

static bool
_has_observable_option(struct sol_coap_packet *pkt)
{
    const uint8_t *ptr;
    uint16_t len;

    ptr = sol_coap_find_first_option(pkt, SOL_COAP_OPTION_OBSERVE, &len);

    return ptr && len == 1 && *ptr;
}

static bool
_cbor_map_get_bytestr_value(const CborValue *map, const char *key,
    struct sol_str_slice *slice)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    return cbor_value_dup_byte_string(&value, (uint8_t **)&slice->data, &slice->len, NULL) == CborNoError;
}

static struct sol_oic_resource *
_new_resource(void)
{
    struct sol_oic_resource *res = malloc(sizeof(*res));

    SOL_NULL_CHECK(res, NULL);

    res->href = SOL_STR_SLICE_STR(NULL, 0);
    res->device_id = SOL_STR_SLICE_STR(NULL, 0);
    sol_vector_init(&res->types, sizeof(struct sol_str_slice));
    sol_vector_init(&res->interfaces, sizeof(struct sol_str_slice));

    res->observe.timeout = NULL;
    res->observe.clear_data = 0;

    res->observable = false;
    res->slow = false;
    res->secure = false;
    res->active = false;

    res->refcnt = 1;

    res->api_version = SOL_OIC_RESOURCE_API_VERSION;

    return res;
}

static bool
_iterate_over_resource_reply_payload(struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    const struct find_resource_ctx *ctx)
{
    CborParser parser;
    CborError err;
    CborValue root, array, value, map;
    int payload_type;
    uint8_t *payload;
    uint16_t payload_len;

    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        SOL_WRN("Could not get payload form discovery packet response");
        return false;
    }

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    if (err != CborNoError)
        return false;
    if (!cbor_value_is_array(&root))
        return false;

    err |= cbor_value_enter_container(&root, &array);

    err |= cbor_value_get_int(&array, &payload_type);
    err |= cbor_value_advance_fixed(&array);
    if (err != CborNoError)
        return false;
    if (payload_type != SOL_OIC_PAYLOAD_DISCOVERY)
        return false;

    for (; cbor_value_is_map(&array) && err == CborNoError;
        err |= cbor_value_advance(&array)) {
        struct sol_oic_resource *res = _new_resource();

        SOL_NULL_CHECK(res, false);

        if (!_cbor_map_get_str_value(&array, SOL_OIC_KEY_HREF, &res->href))
            return false;
        if (!_cbor_map_get_bytestr_value(&array, SOL_OIC_KEY_DEVICE_ID, &res->device_id))
            return false;

        err |= cbor_value_map_find_value(&array, SOL_OIC_KEY_PROPERTIES, &value);
        if (!cbor_value_is_map(&value))
            return false;

        if (!_cbor_map_get_array(&value, SOL_OIC_KEY_RESOURCE_TYPES, &res->types))
            return false;
        if (!_cbor_map_get_array(&value, SOL_OIC_KEY_INTERFACES, &res->interfaces))
            return false;

        err |= cbor_value_map_find_value(&value, SOL_OIC_KEY_POLICY, &map);
        if (!cbor_value_is_map(&map)) {
            err = CborErrorUnknownType;
        } else {
            CborValue bitmap_value;
            uint64_t bitmap = 0;

            err |= cbor_value_map_find_value(&map, SOL_OIC_KEY_BITMAP, &bitmap_value);
            err |= cbor_value_get_uint64(&bitmap_value, &bitmap);

            res->observable = (bitmap & SOL_OIC_FLAG_OBSERVABLE);
            res->active = (bitmap & SOL_OIC_FLAG_ACTIVE);
            res->slow = (bitmap & SOL_OIC_FLAG_SLOW);
            res->secure = (bitmap & SOL_OIC_FLAG_SECURE);
        }

        if (err == CborNoError) {
            res->observable = res->observable || _has_observable_option(req);
            res->addr = *cliaddr;
            ctx->cb(ctx->client, res, ctx->data);
        }

        sol_oic_resource_unref(res);
    }

    return (err | cbor_value_leave_container(&root, &array)) == CborNoError;
}

static bool
_is_discovery_pending_for_ctx(const struct find_resource_ctx *ctx)
{
    void *iter;
    uint16_t idx;

    SOL_PTR_VECTOR_FOREACH_IDX (&pending_discovery, iter, idx) {
        if (iter == ctx) {
            SOL_DBG("Context %p is in pending discovery list", ctx);

            return true;
        }
    }

    SOL_DBG("Context %p is _not_ in pending discovery list", ctx);
    return false;
}

static int
_find_resource_reply_cb(struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct find_resource_ctx *ctx = data;

    if (!_is_discovery_pending_for_ctx(ctx)) {
        SOL_WRN("Received discovery response packet while not waiting for one");
        return -ENOTCONN;
    }

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        return -ENOENT;
    }

    if (!_pkt_has_same_token(req, ctx->token)) {
        SOL_WRN("Discovery packet token differs from expected");
        return -ENOENT;
    }

    if (!sol_oic_pkt_has_cbor_content(req)) {
        SOL_WRN("Discovery packet not in CBOR format");
        return -EINVAL;
    }

    if (!_iterate_over_resource_reply_payload(req, cliaddr, ctx)) {
        SOL_WRN("Could not iterate over find resource reply packet");
        return -EINVAL;
    }

    return 0;
}

static bool
_remove_from_pending_discovery_list(void *data)
{
    int r;

    SOL_DBG("Removing context %p from pending discovery list after %dms",
        data, DISCOVERY_RESPONSE_TIMEOUT_MS);

    free(data);

    r = sol_ptr_vector_remove(&pending_discovery, data);
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

SOL_API bool
sol_oic_client_find_resource(struct sol_oic_client *client,
    struct sol_network_link_addr *cliaddr, const char *resource_type,
    void (*resource_found_cb)(struct sol_oic_client *cli,
    struct sol_oic_resource *res,
    void *data),
    void *data)
{
    static const char oic_well_known[] = "/oic/res";
    struct sol_coap_packet *req;
    struct find_resource_ctx *ctx;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);

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
        if (r < 0 || r > (int)sizeof(query))
            goto out;

        sol_coap_add_option(req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    /* Discovery packets can't be sent through a DTLS server. */
    r = sol_coap_send_packet_with_reply(client->server, req, cliaddr, _find_resource_reply_cb, ctx);
    if (!r) {
        /* Safe to free ctx, as _find_resource_cb() will not free if ctx
         * is not on pending_discovery vector. */
        if (sol_ptr_vector_append(&pending_discovery, ctx) < 0) {
            SOL_WRN("Discovery responses will be blocked: couldn't save context to pending discovery response list.");

            goto out_no_pkt;
        }

        /* 10s should be plenty. */
        if (!sol_timeout_add(DISCOVERY_RESPONSE_TIMEOUT_MS, _remove_from_pending_discovery_list, ctx)) {
            SOL_WRN("Could not create timeout to cancel discovery process");
            sol_ptr_vector_remove(&pending_discovery, ctx);

            goto out_no_pkt;
        }

        return true;
    }

    goto out_no_pkt;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return false;
}

static int
_resource_request_cb(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct resource_request_ctx *ctx = data;
    CborParser parser;
    CborValue root, array;
    CborError err;
    uint8_t *payload;
    uint16_t payload_len;
    int payload_type;

    if (!ctx->cb)
        return -ENOENT;
    if (!_pkt_has_same_token(req, ctx->token))
        return -EINVAL;
    if (!sol_oic_pkt_has_cbor_content(req))
        return -EINVAL;
    if (!sol_coap_packet_has_payload(req))
        return 0;
    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0)
        return 0;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    if (err != CborNoError)
        return -EINVAL;

    if (!cbor_value_is_array(&root))
        return -EINVAL;

    err |= cbor_value_enter_container(&root, &array);

    err |= cbor_value_get_int(&array, &payload_type);
    err |= cbor_value_advance_fixed(&array);
    if (err != CborNoError)
        return -EINVAL;
    if (payload_type != SOL_OIC_PAYLOAD_REPRESENTATION)
        return -EINVAL;

    while (cbor_value_is_map(&array) && err == CborNoError) {
        struct sol_oic_repr_field *repr;
        struct sol_vector reprs = SOL_VECTOR_INIT(struct sol_oic_repr_field);
        CborValue value;
        char *href;
        size_t len;
        uint16_t idx;

        err |= cbor_value_map_find_value(&array, SOL_OIC_KEY_HREF, &value);
        err |= cbor_value_dup_text_string(&value, &href, &len, NULL);

        err |= cbor_value_map_find_value(&array, SOL_OIC_KEY_REPRESENTATION, &value);

        err |= sol_oic_decode_cbor_repr_map(&value, &reprs);
        if (err == CborNoError) {
            struct sol_str_slice href_slice = sol_str_slice_from_str(href);

            /* A sentinel item isn't needed since a sol_vector is passed. */
            ctx->cb(ctx->client, cliaddr, &href_slice, &reprs, ctx->data);
        }

        free(href);
        SOL_VECTOR_FOREACH_IDX (&reprs, repr, idx) {
            free((char *)repr->key);

            if (repr->type != SOL_OIC_REPR_TYPE_TEXT_STRING && repr->type != SOL_OIC_REPR_TYPE_BYTE_STRING)
                continue;

            free((char *)repr->v_slice.data);
        }
        sol_vector_clear(&reprs);

        err |= cbor_value_advance(&array);
    }

    err |= cbor_value_leave_container(&root, &array);

    if (err == CborNoError)
        return 0;

    SOL_ERR("Error while parsing CBOR repr packet: %s", cbor_error_string(err));
    return -EINVAL;
}

static int
_one_shot_resource_request_cb(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    int ret = _resource_request_cb(req, cliaddr, data);

    free(data);
    return ret;
}

static bool
_resource_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method, const struct sol_vector *repr,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_vector *reprs, void *data),
    void *data, bool observe)
{
    const uint8_t format_cbor = SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
    CborError err;
    char *href;

    int (*cb)(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr, void *data);
    struct sol_coap_packet *req;
    struct resource_request_ctx *ctx = sol_util_memdup(&(struct resource_request_ctx) {
            .client = client,
            .cb = callback,
            .data = data,
            .res = res,
        }, sizeof(*ctx));

    SOL_NULL_CHECK(ctx, false);

    req = sol_coap_packet_request_new(method, SOL_COAP_TYPE_CON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        goto out_no_req;
    }

    if (!_set_token_and_mid(req, &ctx->token))
        goto out;

    if (observe) {
        uint8_t reg = 0;

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

    sol_coap_add_option(req, SOL_COAP_OPTION_CONTENT_FORMAT, &format_cbor, sizeof(format_cbor));

    err = sol_oic_encode_cbor_repr(req, href, repr);
    if (err == CborNoError) {
        struct sol_coap_server *server;
        struct sol_network_link_addr addr;

        server = _best_server_for_resource(client, res, &addr);

        SOL_DBG("Sending CoAP packet through %s server (port %d)",
            server == client->dtls_server ? "secure" : "non-secure",
            addr.port);

        return sol_coap_send_packet_with_reply(server, req, &addr,
            cb, ctx) == 0;
    }

    SOL_ERR("Could not encode CBOR representation: %s", cbor_error_string(err));

out:
    sol_coap_packet_unref(req);
out_no_req:
    free(ctx);
    return false;
}

SOL_API bool
sol_oic_client_resource_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method, const struct sol_vector *repr,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_vector *reprs, void *data),
    void *data)
{
    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    return _resource_request(client, res, method, repr, callback, data, false);
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

    r = _resource_request(ctx->client, ctx->res, SOL_COAP_METHOD_GET, NULL, ctx->cb, ctx->data, false);
    if (!r)
        SOL_WRN("Could not send polling packet to observable resource");

    return true;
}

static bool
_observe_with_polling(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_vector *reprs, void *data),
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

SOL_API bool
sol_oic_client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_vector *reprs, void *data),
    void *data, bool observe)
{
    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    if (observe) {
        if (!res->observable)
            return _observe_with_polling(client, res, callback, data);
        return _resource_request(client, res, SOL_COAP_METHOD_GET, NULL, callback, data, true);
    }

    if (res->observe.timeout)
        return _stop_observing_with_polling(res);

    if (!res->observable) {
        SOL_WRN("Attempting to stop observing non-observable resource without ever being observed");
        return false;
    }

    return _resource_request(client, res, SOL_COAP_METHOD_GET, NULL, callback, data, false);
}
