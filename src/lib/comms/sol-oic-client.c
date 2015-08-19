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

#include "sol-coap.h"
#include "sol-json.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "sol-oic-client.h"

#define POLL_OBSERVE_TIMEOUT_MS 10000

#define IOTIVITY_CON_REQ_MID 0xd42
#define IOTIVITY_CON_REQ_OBS_MID 0x7d44
#define IOTIVITY_NONCON_REQ_MID 0x7d40

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
};

struct resource_request_ctx {
    struct sol_oic_client *client;
    struct sol_oic_resource *res;
    void (*cb)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
        const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data);
    void *data;
};

static const char json_type[] = "application/json";

SOL_LOG_INTERNAL_DECLARE(_sol_oic_client_log_domain, "oic-client");

static bool
_parse_json_array(const char *data, unsigned int size, struct sol_vector *vec)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, data, size);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_STRING, reason) {
        struct sol_str_slice *slice = sol_vector_append(vec);

        if (!slice) {
            SOL_WRN("Could not append to vector");
            return false;
        }
        slice->len = token.end - token.start - 2; /* 2 = "" */
        slice->data = token.start + 1; /* 1 = " */
    }

    return reason == SOL_JSON_LOOP_REASON_OK;
}

static bool
_parse_resource_reply_props(const char *data, unsigned int size, struct sol_oic_resource *res)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, data, size);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, "obs", 3) && sol_json_token_get_type(&value) == SOL_JSON_TYPE_NUMBER) {
            if (value.end - value.start != 1)
                goto out;
            res->observable = (*value.start != '0');
        } else if (sol_json_token_str_eq(&key, "rt", 2)) {
            if (!_parse_json_array(value.start, value.end - value.start, &res->types))
                goto out;
        } else if (sol_json_token_str_eq(&key, "if", 2)) {
            if (!_parse_json_array(value.start, value.end - value.start, &res->interfaces))
                goto out;
        }
    }
    if (reason == SOL_JSON_LOOP_REASON_OK)
        return true;

out:
    SOL_WRN("Invalid JSON");
    return false;
}

static bool
_get_oc_response_array_from_payload(uint8_t **payload, uint16_t *payload_len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, *payload, *payload_len);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (!sol_json_token_str_eq(&key, "oc", 2))
            continue;
        if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START)
            goto out;

        *payload = (uint8_t *)value.start;
        *payload_len = (uint16_t)(value.end - value.start);
        return true;
    }

out:
    SOL_WRN("Invalid JSON");
    return false;
}

static bool
_parse_resource_reply_payload(struct sol_oic_resource *res, uint8_t *payload, uint16_t payload_len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token;
    enum sol_json_loop_reason reason;

    if (!_get_oc_response_array_from_payload(&payload, &payload_len))
        goto out;

    sol_json_scanner_init(&scanner, payload, payload_len);

    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        struct sol_json_token key, value;

        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&scanner, &token, &key, &value, reason) {
            if (sol_json_token_str_eq(&key, "href", 4) && sol_json_token_get_type(&value) == SOL_JSON_TYPE_STRING) {
                res->href = SOL_STR_SLICE_STR(value.start + 1, (value.end - value.start) - 2);
            } else if (sol_json_token_str_eq(&key, "prop", 4) && sol_json_token_get_type(&value) == SOL_JSON_TYPE_OBJECT_START) {
                if (!_parse_resource_reply_props(value.start, value.end - value.start, res))
                    goto out;
            }
        }

        if (reason == SOL_JSON_LOOP_REASON_OK && !res->href.len)
            goto out;
    }

    if (reason == SOL_JSON_LOOP_REASON_OK)
        return true;

out:
    SOL_WRN("Invalid JSON");
    return false;
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
        sol_vector_clear(&r->types);
        sol_vector_clear(&r->interfaces);
        free(r);
    }
}

static bool
_has_observable_option(struct sol_coap_packet *pkt)
{
    const uint8_t *ptr;
    uint16_t len;

    ptr = sol_coap_find_first_option(pkt, SOL_COAP_OPTION_OBSERVE, &len);

    return ptr && len == 1 && *ptr;
}

static int
_find_resource_reply_cb(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct find_resource_ctx *ctx = data;
    uint8_t *payload;
    uint16_t payload_len;
    struct sol_oic_resource *res;
    int error = 0;

    if (!ctx->cb) {
        SOL_WRN("No user callback provided");
        error = -ENOENT;
        goto free_ctx;
    }

    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        SOL_WRN("Could not get pkt payload");
        error = -ENOMEM;
        goto free_ctx;
    }
    res = malloc(sizeof(*res) + payload_len);
    if (!res) {
        SOL_WRN("Not enough memory");
        error = -errno;
        goto free_ctx;
    }

    payload = memcpy(res + 1, payload, payload_len);
    res->href = (struct sol_str_slice)SOL_STR_SLICE_EMPTY;
    sol_vector_init(&res->types, sizeof(struct sol_str_slice));
    sol_vector_init(&res->interfaces, sizeof(struct sol_str_slice));

    res->observe.timeout = NULL;
    res->observe.clear_data = 0;

    res->refcnt = 1;

    if (_parse_resource_reply_payload(res, payload, payload_len)) {
        res->observable = res->observable || _has_observable_option(req);
        res->addr = *cliaddr;
        ctx->cb(ctx->client, res, ctx->data);
    } else {
        SOL_WRN("Could not parse payload");
        error = -1;
    }

    sol_oic_resource_unref(res);

free_ctx:
    free(ctx);
    return error;
}

SOL_API bool
sol_oic_client_find_resource(struct sol_oic_client *client,
    struct sol_network_link_addr *cliaddr, const char *resource_type,
    void (*resource_found_cb)(struct sol_oic_client *cli,
    struct sol_oic_resource *res,
    void *data),
    void *data)
{
    static const char oc_core_uri[] = "/oc/core";
    struct sol_coap_packet *req;
    struct find_resource_ctx *ctx;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);

    ctx = sol_util_memdup(&(struct find_resource_ctx) {
            .client = client,
            .cb = resource_found_cb,
            .data = data
        }, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, false);

    /* Multicast discovery should be non-confirmable */
    req = sol_coap_packet_request_new(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_NONCON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        goto out_no_pkt;
    }

    sol_coap_header_set_id(req, IOTIVITY_NONCON_REQ_MID);

    if (sol_coap_packet_add_uri_path_option(req, oc_core_uri) < 0) {
        SOL_WRN("Invalid URI: %s", oc_core_uri);
        goto out;
    }

    if (resource_type) {
        char query[64];

        r = snprintf(query, sizeof(query), "rt=%s", resource_type);
        if (r < 0 || r > (int)sizeof(query))
            goto out;

        sol_coap_add_option(req, SOL_COAP_OPTION_URI_QUERY, query, r);
    }

    sol_coap_add_option(req, SOL_COAP_OPTION_ACCEPT, json_type, sizeof(json_type) - 1);

    r = sol_coap_send_packet_with_reply(client->server, req, cliaddr, _find_resource_reply_cb, ctx);
    if (!r)
        return true;

    goto out_no_pkt;

out:
    sol_coap_packet_unref(req);
out_no_pkt:
    free(ctx);
    return false;
}

static void
_call_request_context_for_response_array(struct resource_request_ctx *ctx,
    const struct sol_network_link_addr *cliaddr, uint8_t *payload, uint16_t payload_len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, payload, payload_len);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        struct sol_str_slice href = SOL_STR_SLICE_EMPTY;
        struct sol_str_slice rep = SOL_STR_SLICE_EMPTY;
        struct sol_json_token key, value;

        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&scanner, &token, &key, &value, reason) {
            if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_STRING && sol_json_token_str_eq(&key, "href", 4)) {
                href = SOL_STR_SLICE_STR(value.start + 1, value.end - value.start - 2);
            } else if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_OBJECT_START && sol_json_token_str_eq(&key, "rep", 3)) {
                rep = SOL_STR_SLICE_STR(value.start, value.end - value.start);
            }
        }

        if (reason == SOL_JSON_LOOP_REASON_OK && href.len && rep.len)
            ctx->cb(ctx->client, cliaddr, &href, &rep, ctx->data);
    }
    if (reason != SOL_JSON_LOOP_REASON_OK)
        SOL_WRN("Invalid JSON");
}

static int
_resource_request_cb(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct resource_request_ctx *ctx = data;
    uint8_t *payload;
    uint16_t payload_len;

    if (!ctx->cb)
        return -ENOENT;
    if (!sol_coap_packet_has_payload(req))
        return 0;
    if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0)
        return 0;
    if (_get_oc_response_array_from_payload(&payload, &payload_len))
        _call_request_context_for_response_array(ctx, cliaddr, payload, payload_len);

    return 0;
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
    sol_coap_method_t method, uint8_t *payload, size_t payload_len,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data),
    void *data, bool observe)
{
    int (*cb)(struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr, void *data);
    struct sol_coap_packet *req;
    struct resource_request_ctx *ctx = sol_util_memdup(&(struct resource_request_ctx) {
            .client = client,
            .cb = callback,
            .data = data,
            .res = res
        }, sizeof(*ctx));

    SOL_NULL_CHECK(ctx, false);

    req = sol_coap_packet_request_new(method, SOL_COAP_TYPE_CON);
    if (!req) {
        SOL_WRN("Could not create CoAP packet");
        goto out_no_req;
    }

    if (observe) {
        uint8_t reg = 0;

        sol_coap_header_set_id(req, IOTIVITY_CON_REQ_OBS_MID);
        sol_coap_add_option(req, SOL_COAP_OPTION_OBSERVE, &reg, sizeof(reg));
        cb = _resource_request_cb;
    } else {
        sol_coap_header_set_id(req, IOTIVITY_CON_REQ_MID);
        cb = _one_shot_resource_request_cb;
    }

    if (sol_coap_packet_add_uri_path_option(req, strndupa(res->href.data, res->href.len)) < 0) {
        SOL_WRN("Invalid URI: %.*s", SOL_STR_SLICE_PRINT(res->href));
        goto out;
    }

    if (payload && payload_len) {
        uint8_t *coap_payload;
        uint16_t coap_payload_len;
        int r;

        sol_coap_add_option(req, SOL_COAP_OPTION_ACCEPT, json_type, sizeof(json_type) - 1);

        if (sol_coap_packet_get_payload(req, &coap_payload, &coap_payload_len) < 0) {
            SOL_WRN("Could not get CoAP payload");
            goto out;
        }

        r = snprintf((char *)coap_payload, coap_payload_len, "{\"oc\":[{\"rep\":%.*s}]}",
            (int)payload_len, payload);
        if (r < 0 || r >= coap_payload_len) {
            SOL_WRN("Could not wrap payload");
            goto out;
        }

        if (sol_coap_packet_set_payload_used(req, r) < 0) {
            SOL_WRN("Request payload too large (have %d, want %zu)", r, payload_len);
            goto out;
        }
    }

    return sol_coap_send_packet_with_reply(client->server, req, &res->addr, cb, ctx) == 0;

out:
    sol_coap_packet_unref(req);
out_no_req:
    free(ctx);
    return false;
}

SOL_API bool
sol_oic_client_resource_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method, uint8_t *payload, size_t payload_len,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data),
    void *data)
{
    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    return _resource_request(client, res, method, payload, payload_len, callback, data, false);
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

    r = _resource_request(ctx->client, ctx->res, SOL_COAP_METHOD_GET, NULL, 0, ctx->cb, ctx->data, false);
    if (!r)
        SOL_WRN("Could not send polling packet to observable resource");

    return true;
}

static bool
_observe_with_polling(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data),
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
    const struct sol_str_slice *href, const struct sol_str_slice *payload, void *data),
    void *data, bool observe)
{
    SOL_NULL_CHECK(client, false);
    OIC_CLIENT_CHECK_API(client, false);
    SOL_NULL_CHECK(res, false);
    OIC_RESOURCE_CHECK_API(res, false);

    if (observe) {
        if (!res->observable)
            return _observe_with_polling(client, res, callback, data);
        return _resource_request(client, res, SOL_COAP_METHOD_GET, NULL, 0, callback, data, true);
    }

    if (res->observe.timeout)
        return _stop_observing_with_polling(res);

    if (!res->observable) {
        SOL_WRN("Attempting to stop observing non-observable resource without ever being observed");
        return false;
    }

    return _resource_request(client, res, SOL_COAP_METHOD_GET, NULL, 0, callback, data, false);
}
