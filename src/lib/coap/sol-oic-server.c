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
#include <netinet/in.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define SOL_LOG_DOMAIN &_sol_oic_server_log_domain
#include "sol-log-internal.h"
#include "sol-vector.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-oic-server.h"
#include "sol-json.h"

#include "coap.h"
#include "sol-coap.h"

SOL_LOG_INTERNAL_DECLARE(_sol_oic_server_log_domain, "oic-server");

struct sol_oic_server {
    struct sol_coap_server *server;
    struct sol_vector device_definitions;
    struct sol_oic_server_information *information;
    int refcnt;
};

struct sol_oic_device_definition {
    struct sol_str_slice resource_type_prefix;
    struct sol_str_slice endpoint;
    struct sol_vector resource_types;
    struct sol_coap_resource *resource;
};

struct resource_type_data {
    struct sol_oic_resource_type *resource_type;
    void *data;
    struct sol_coap_resource *resource;
};

static struct sol_oic_server oic_server;

#define OIC_SERVER_CHECK(ret)                               \
    do {                                                    \
        if (oic_server.refcnt == 0) {                       \
            SOL_WRN("OIC API used before initialization");   \
            return ret;                                     \
        }                                                   \
    } while (0)

static uint16_t
_append_json_key_value_full(uint8_t *payload,
    uint16_t payload_size,
    uint16_t payload_len,
    const char *prefix,
    const char *suffix,
    const char *key,
    struct sol_str_slice value)
{
    int ret;

    ret = snprintf((char *)payload, (size_t)(payload_size - payload_len), "%s\"%s\":\"%.*s\"%s,",
        prefix, key, (int)value.len, value.data, suffix);
    if (ret < 0 || ret >= payload_size - payload_len)
        return 0;
    return payload_len + ret;
}

static uint16_t
_append_json_object(uint8_t *payload,
    uint16_t payload_size,
    uint16_t payload_len,
    const char *key,
    struct sol_str_slice value)
{
    return _append_json_key_value_full(payload, payload_size, payload_len, "{", "}", key, value);
}

static uint16_t
_append_json_key_value(uint8_t *payload,
    uint16_t payload_size,
    uint16_t payload_len,
    const char *key,
    struct sol_str_slice value)
{
    return _append_json_key_value_full(payload, payload_size, payload_len, "", "", key, value);
}

static int
_sol_oic_server_d(const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct sol_coap_packet *response;
    uint8_t *payload;
    uint16_t payload_size, payload_len = 0;

    OIC_SERVER_CHECK(-ENOTCONN);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    if (sol_coap_packet_get_payload(response, &payload, &payload_size) < 0)
        goto no_memory;

    if (payload_len + 1 < payload_size)
        goto no_memory;
    *payload++ = '{';
    payload_len++;

#define APPEND_KEY_VALUE(k, v)                          \
    do {                                    \
        uint16_t r;                             \
        r = _append_json_key_value(payload, payload_size, payload_len, k,   \
            sol_str_slice_from_str(oic_server.information->v));     \
        if (!r) goto no_memory;                     \
        payload += (r - payload_len);                   \
        payload_len = r;                            \
    } while (0)

    APPEND_KEY_VALUE("dt", device.name);
    APPEND_KEY_VALUE("drt", device.resource_type);
    APPEND_KEY_VALUE("id", device.id);
    APPEND_KEY_VALUE("mnmn", manufacturer.name);
    APPEND_KEY_VALUE("mnmo", manufacturer.model);
    APPEND_KEY_VALUE("mndt", manufacturer.date);
    APPEND_KEY_VALUE("mnpv", platform.version);
    APPEND_KEY_VALUE("mnfv", firmware.version);
    APPEND_KEY_VALUE("icv", interface.version);
    APPEND_KEY_VALUE("mnsl", support_link);
    APPEND_KEY_VALUE("loc", location);
    APPEND_KEY_VALUE("epi", epi);

#undef APPEND_KEY_VALUE

    /* Do not advance payload/payload_len: substitute last "," to "}" */
    *payload = '}';

    sol_coap_packet_set_payload_used(response, payload_len);
    return sol_coap_send_packet(oic_server.server, response, cliaddr);

no_memory:
    SOL_WRN("Discarding CoAP response due to insufficient memory");
    sol_coap_packet_unref(response);
    return -ENOMEM;
}


static int
_sol_oic_server_res(const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    void *data)
{
    static const char resource_list_json[] = "{\"resourceList\":[";
    struct sol_coap_packet *response;
    struct sol_oic_device_definition *iter;
    uint16_t idx;
    uint8_t *payload;
    uint16_t payload_size, payload_len = 0;

    OIC_SERVER_CHECK(-ENOTCONN);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    if (sol_coap_packet_get_payload(response, &payload, &payload_size) < 0)
        goto no_memory;

    if (payload_len + sizeof(resource_list_json) - 1 < payload_size)
        goto no_memory;
    payload = mempcpy(payload, resource_list_json, sizeof(resource_list_json) - 1);
    payload_len += sizeof(resource_list_json) - 1;

    SOL_VECTOR_FOREACH_IDX (&oic_server.device_definitions, iter, idx) {
        uint16_t r;
        r = _append_json_object(payload, payload_size, payload_len,
            "link", iter->resource_type_prefix);
        if (!r)
            goto no_memory;
        payload += (r - payload_len);
        payload_len = r;
    }

    /* Eat last "," */
    payload--;
    payload_len--;

    if (payload_len + 2 > payload_size)
        goto no_memory;
    memcpy(payload, "]}", 2);
    payload_len += 2;

    sol_coap_packet_set_payload_used(response, payload_len);
    return sol_coap_send_packet(oic_server.server, response, cliaddr);

no_memory:
    SOL_WRN("Discarding CoAP response due to insufficient memory");
    sol_coap_packet_unref(response);
    return -ENOMEM;
}

static int
_sol_oic_server_rts(const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    void *data)
{
    static const char resource_types_json[] = "{\"resourceTypes\":[";
    struct sol_coap_packet *response;
    struct sol_oic_device_definition *iter;
    uint16_t idx;
    uint8_t *payload;
    uint16_t payload_size, payload_len = 0;

    OIC_SERVER_CHECK(-ENOTCONN);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    if (sol_coap_packet_get_payload(response, &payload, &payload_size) < 0)
        goto no_memory;

    if (payload_len + sizeof(resource_types_json) - 1 < payload_size)
        goto no_memory;
    payload = mempcpy(payload, resource_types_json, sizeof(resource_types_json) - 1);
    payload_len += sizeof(resource_types_json) - 1;

    /* FIXME: ensure elements are unique in the generated JSON */
    SOL_VECTOR_FOREACH_IDX (&oic_server.device_definitions, iter, idx) {
        struct resource_type_data *rt_iter;
        uint16_t rt_idx;
        uint16_t r;

        r = _append_json_object(payload, payload_size, payload_len,
            "type", iter->endpoint);
        if (!r)
            goto no_memory;
        payload += (r - payload_len);
        payload_len = r;

        SOL_VECTOR_FOREACH_IDX (&iter->resource_types, rt_iter, rt_idx) {
            r = _append_json_object(payload, payload_size, payload_len,
                "type", rt_iter->resource_type->endpoint);
            if (!r)
                goto no_memory;
            payload += (r - payload_len);
            payload_len = r;
        }
    }

    /* Eat last "," */
    payload--;
    payload_len--;

    if (payload_len + 2 > payload_size)
        goto no_memory;
    memcpy(payload, "]}", 2);
    payload_len += 2;

    sol_coap_packet_set_payload_used(response, payload_len);
    return sol_coap_send_packet(oic_server.server, response, cliaddr);

no_memory:
    SOL_WRN("Discarding CoAP response due to insufficient memory");
    sol_coap_packet_unref(response);
    return -ENOMEM;
}

static const struct sol_coap_resource d_coap_resorce = {
    .path = {
        SOL_STR_SLICE_LITERAL("d"),
        SOL_STR_SLICE_EMPTY
    },
    .get = _sol_oic_server_d,
    .flags = SOL_COAP_FLAGS_NONE
};
static const struct sol_coap_resource res_coap_resorce = {
    .path = {
        SOL_STR_SLICE_LITERAL("res"),
        SOL_STR_SLICE_EMPTY
    },
    .get = _sol_oic_server_res,
    .flags = SOL_COAP_FLAGS_NONE
};
static const struct sol_coap_resource rts_coap_resorce = {
    .path = {
        SOL_STR_SLICE_LITERAL("rts"),
        SOL_STR_SLICE_EMPTY
    },
    .get = _sol_oic_server_rts,
    .flags = SOL_COAP_FLAGS_NONE
};

static struct sol_oic_server_information *
init_static_info(void)
{
    struct sol_oic_server_information information = {
        .device = {
            .name = OIC_DEVICE_NAME,
            .resource_type = OIC_DEVICE_RESOURCE_TYPE,
            .id = OIC_DEVICE_ID
        },
        .manufacturer = {
            .name = OIC_MANUFACTURER_NAME,
            .model = OIC_MANUFACTORER_MODEL,
            .date = OIC_MANUFACTORER_DATE
        },
        .interface = {
            .version = OIC_INTERFACE_VERSION
        },
        .platform = {
            .version = OIC_PLATFORM_VERSION
        },
        .firmware = {
            .version = OIC_FIRMWARE_VERSION
        },
        .support_link = OIC_SUPPORT_LINK,
        .location = OIC_LOCATION,
        .epi = OIC_EPI
    };

    struct sol_oic_server_information *info = sol_util_memdup(&information, sizeof(*info));

    SOL_NULL_CHECK(info, NULL);

    return info;
}

SOL_API bool
sol_oic_server_init(int port)
{
    struct sol_oic_server_information *info;

    if (oic_server.refcnt > 0) {
        oic_server.refcnt++;
        return true;
    }

    SOL_LOG_INTERNAL_INIT_ONCE;

    info = init_static_info();
    SOL_NULL_CHECK(info, NULL);

    oic_server.server = sol_coap_server_new(port);
    if (!oic_server.server) {
        goto error;
    }

    if (!sol_coap_server_register_resource(oic_server.server, &d_coap_resorce, NULL)) {
        goto error;
    }
    if (!sol_coap_server_register_resource(oic_server.server, &res_coap_resorce, NULL))
        goto unregister_d;
    if (!sol_coap_server_register_resource(oic_server.server, &rts_coap_resorce, NULL))
        goto unregister_res;

    oic_server.information = info;
    sol_vector_init(&oic_server.device_definitions, sizeof(struct sol_oic_device_definition));

    oic_server.refcnt++;
    return true;

unregister_res:
    /* FIXME: sol_coap_server_unregister_resource(res); */
unregister_d:
    /* FIXME: sol_coap_server_unregister_resource(d); */
error:
    free(info);
    return false;
}

SOL_API void
sol_oic_server_release(void)
{
    OIC_SERVER_CHECK();

    if (--oic_server.refcnt > 0)
        return;

    sol_vector_clear(&oic_server.device_definitions);
    /* FIXME: sol_coap_server_unregister_resource(res); */
    /* FIXME: sol_coap_server_unregister_resource(rts); */
    /* FIXME: sol_coap_server_unregister_resource(d); */
    sol_coap_server_unref(oic_server.server);
    free(oic_server.information);
}

SOL_API struct sol_oic_device_definition *
sol_oic_server_get_definition(struct sol_str_slice endpoint,
    struct sol_str_slice resource_type_prefix)
{
    struct sol_oic_device_definition *iter;
    uint16_t idx;

    OIC_SERVER_CHECK(NULL);

    SOL_VECTOR_FOREACH_IDX (&oic_server.device_definitions, iter, idx) {
        if (sol_str_slice_eq(iter->endpoint, endpoint)
            && sol_str_slice_eq(iter->resource_type_prefix, resource_type_prefix))
            return iter;
    }

    return NULL;
}

static int
_sol_oic_device_definition_specific_get(const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    void *data)
{
    struct sol_oic_device_definition *def = data;
    struct sol_coap_packet *response;
    struct resource_type_data *iter;
    uint16_t idx;
    uint8_t *payload;
    uint16_t payload_size, payload_len = 0;
    int ret;

    OIC_SERVER_CHECK(-ENOTCONN);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    if (sol_coap_packet_get_payload(response, &payload, &payload_size) < 0)
        goto no_memory;

    ret = snprintf((char *)payload, payload_size, "{\"rt\":\"%.*s\",",
        (int)def->resource_type_prefix.len, def->resource_type_prefix.data);
    if (ret < 0 || ret >= payload_size)
        goto no_memory;
    payload_len += ret;
    payload += ret;

    /* FIXME: Don't know where to get this information from in RAML! */
    ret = snprintf((char *)payload, payload_size, "\"if\":\"%s\",", "oic.if.fixme");
    if (ret < 0 || ret >= payload_size)
        goto no_memory;
    payload_len += ret;
    payload += ret;

    ret = snprintf((char *)payload, payload_size, "\"resources\":[");
    if (ret < 0 || ret >= payload_size)
        goto no_memory;
    payload_len += ret;
    payload += ret;

    /* FIXME: ensure elements are unique in the generated JSON */
    SOL_VECTOR_FOREACH_IDX (&def->resource_types, iter, idx) {
        struct sol_oic_resource_type *rt = iter->resource_type;
        ret = snprintf((char *)payload, payload_size,
            "{\"link\":\"/%.*s\",\"rel\":\"contains\",\"rt\":\"%.*s.%.*s\"},",
            (int)rt->endpoint.len, rt->endpoint.data,
            (int)def->resource_type_prefix.len, def->resource_type_prefix.data,
            (int)rt->endpoint.len, rt->endpoint.data);
        if (ret < 0 || ret >= payload_size)
            goto no_memory;
        payload_len += ret;
        payload += ret;
    }

    /* Eat last "," */
    payload--;
    payload_len--;

    if (payload_len + 2 > payload_size)
        goto no_memory;
    memcpy(payload, "]}", 2);
    payload_len += 2;

    sol_coap_packet_set_payload_used(response, payload_len);
    return sol_coap_send_packet(oic_server.server, response, cliaddr);

no_memory:
    SOL_WRN("Discarding CoAP response due to insufficient memory");
    sol_coap_packet_unref(response);
    return -ENOMEM;
}

static struct sol_coap_resource *
create_coap_resource(struct sol_str_slice endpoint)
{
    struct sol_coap_resource *res;
    unsigned int count = 0;
    unsigned int current;
    size_t i;

    for (i = 0; i < endpoint.len; i++)
        if (endpoint.data[i] == '/')
            count++;

    SOL_INT_CHECK(count, == 0, NULL);

    if (endpoint.data[0] != '/') {
        SOL_WRN("Invalid endpoint - Path '%.*s' does not start with '/'", (int)endpoint.len, endpoint.data);
        return NULL;
    }

    if (endpoint.data[endpoint.len - 1] == '/') {
        SOL_WRN("Invalid endpoint - Path '%.*s' ends with '/'", (int)endpoint.len, endpoint.data);
        return NULL;
    }

    /* alloc space for the path plus empty slice at the end */
    res = calloc(1, sizeof(struct sol_coap_resource) + (count + 1) * sizeof(struct sol_str_slice));
    SOL_NULL_CHECK(res, NULL);

    res->path[0].data = &endpoint.data[1];
    for (i = 1, current = 0; i < endpoint.len; i++) {
        if (endpoint.data[i] == '/')
            res->path[++current].data = &endpoint.data[i + 1];
        else
            res->path[current].len++;
    }

    return res;
}

SOL_API struct sol_oic_device_definition *
sol_oic_server_register_definition(struct sol_str_slice endpoint,
    struct sol_str_slice resource_type_prefix,
    enum sol_coap_flags flags)
{
    struct sol_oic_device_definition *def;

    OIC_SERVER_CHECK(NULL);

    def = sol_oic_server_get_definition(endpoint, resource_type_prefix);
    if (def)
        return def;

    def = sol_vector_append(&oic_server.device_definitions);
    SOL_NULL_CHECK(def, NULL);

    def->resource_type_prefix = resource_type_prefix;
    def->endpoint = endpoint;
    def->resource = create_coap_resource(endpoint);
    SOL_NULL_CHECK_GOTO(def->resource, error);
    def->resource->flags = flags;
    def->resource->get = _sol_oic_device_definition_specific_get;
    def->resource->resource_type = resource_type_prefix;

    sol_vector_init(&def->resource_types, sizeof(struct resource_type_data));

    if (!sol_coap_server_register_resource(oic_server.server, def->resource, def))
        goto error;

    return def;

error:
    free(def->resource);
    (void)sol_vector_del(&oic_server.device_definitions, oic_server.device_definitions.len - 1);
    return NULL;
}

static void
_sol_oic_device_definitions_free_resource_types(struct sol_oic_device_definition *def)
{
    struct resource_type_data *iter;
    uint16_t idx;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&def->resource_types, iter, idx) {
        free(iter->resource_type);
        /* FIXME: sol_coap_server_unregister_resource(iter->resource) */
        free(iter->resource);
    }
    sol_vector_clear(&def->resource_types);
}

SOL_API bool
sol_oic_server_unregister_definition(const struct sol_oic_device_definition *definition)
{
    struct sol_oic_device_definition *iter;
    uint16_t idx;

    OIC_SERVER_CHECK(false);
    SOL_NULL_CHECK(definition, false);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&oic_server.device_definitions, iter, idx) {
        if (!sol_str_slice_eq(iter->resource_type_prefix, definition->resource_type_prefix))
            continue;
        if (!sol_str_slice_eq(iter->endpoint, definition->endpoint))
            continue;

        _sol_oic_device_definitions_free_resource_types(iter);
        /* FIXME: sol_coap_server_unregister_resource(iter->resource) */

        free(iter->resource);

        (void)sol_vector_del(&oic_server.device_definitions, idx);
        return true;
    }

    return false;
}

static bool
_get_oc_response_array_from_payload(uint8_t **payload, uint16_t *payload_len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, *payload, *payload_len);
    SOL_JSON_SCANNER_OBJECT_LOOP(&scanner, &token, &key, &value, reason) {
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
_get_rep_object(uint8_t **payload, uint16_t *payload_len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    if (!_get_oc_response_array_from_payload(payload, payload_len))
        goto out;

    sol_json_scanner_init(&scanner, *payload, *payload_len);
    SOL_JSON_SCANNER_ARRAY_LOOP(&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        SOL_JSON_SCANNER_OBJECT_LOOP_NEST(&scanner, &token, &key, &value, reason) {
            if (sol_json_token_str_eq(&key, "rep", 3)
                && sol_json_token_get_type(&value) == SOL_JSON_TYPE_OBJECT_START) {
                *payload = (uint8_t *)value.start;
                *payload_len = (uint16_t)(value.end - value.start);
                return true;
            }
        }
    }

out:
    SOL_WRN("Invalid JSON");
    return false;
}

static int
_sol_oic_resource_type_handle(
    sol_coap_responsecode_t (*handle_fn)(const struct sol_network_link_addr *cliaddr, const void *data,
        uint8_t *payload, uint16_t *payload_len),
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    struct resource_type_data *res, bool expect_payload)
{
    struct sol_coap_packet *response;
    sol_coap_responsecode_t code = SOL_COAP_RSPCODE_INTERNAL_ERROR;
    uint8_t *payload;
    uint16_t payload_len;

    OIC_SERVER_CHECK(-ENOTCONN);

    response = sol_coap_packet_new(req);
    if (!response) {
        SOL_WRN("Could not build response packet.");
        return -1;
    }

    if (!handle_fn) {
        code = SOL_COAP_RSPCODE_NOT_IMPLEMENTED;
        goto done;
    }

    if (expect_payload) {
        if (sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
            code = SOL_COAP_RSPCODE_BAD_REQUEST;
            goto done;
        }

        if (!_get_rep_object(&payload, &payload_len)) {
            code = SOL_COAP_RSPCODE_BAD_REQUEST;
            goto done;
        }
    } else {
        payload_len = sizeof(req->buf);
        payload = req->buf;
        memset(payload, 0, payload_len);
    }

    code = handle_fn(cliaddr, res->data, payload, &payload_len);
    if (code == SOL_COAP_RSPCODE_CONTENT) {
        uint8_t *response_payload;
        uint16_t response_len;
        int r;

        if (sol_coap_packet_get_payload(response, &response_payload, &response_len) < 0)
            goto done;

        r = snprintf((char *)response_payload, response_len, "{\"oc\":[{\"rep\":%.*s}]}",
            (int)payload_len, payload);
        if (r < 0 || r >= response_len) {
            code = SOL_COAP_RSPCODE_INTERNAL_ERROR;
            goto done;
        }

        if (sol_coap_packet_set_payload_used(response, r) < 0) {
            code = SOL_COAP_RSPCODE_INTERNAL_ERROR;
            goto done;
        }
    }

done:
    sol_coap_header_set_type(response, SOL_COAP_TYPE_ACK);
    sol_coap_header_set_code(response, code);

    return sol_coap_send_packet(oic_server.server, response, cliaddr);
}

#define DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(method, expect_payload)                     \
    static int                                                                               \
    _sol_oic_resource_type_ ## method(const struct sol_coap_resource *resource,                  \
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr, void *data)  \
    {                                                                                        \
        struct resource_type_data *res = data;                                               \
        return _sol_oic_resource_type_handle(res->resource_type->method.handle,               \
            req, cliaddr, res, expect_payload);                                              \
    }

DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(get, false)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(put, true)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(post, true)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(delete, true)

#undef DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD

SOL_API struct sol_coap_resource *
sol_oic_device_definition_register_resource_type(struct sol_oic_device_definition *definition,
    const struct sol_oic_resource_type *resource_type, void *handler_data, enum sol_coap_flags flags)
{
    struct resource_type_data *res;
    struct sol_oic_resource_type *res_type_copy;

    OIC_SERVER_CHECK(NULL);
    SOL_NULL_CHECK(definition, NULL);
    SOL_NULL_CHECK(resource_type, NULL);

    res_type_copy = sol_util_memdup(resource_type, sizeof(*resource_type));
    SOL_NULL_CHECK(res_type_copy, NULL);

    res = sol_vector_append(&definition->resource_types);
    if (!res) {
        free(res_type_copy);
        return false;
    }

    res->data = handler_data;
    res->resource_type = res_type_copy;
    res->resource = create_coap_resource(resource_type->endpoint);
    SOL_NULL_CHECK(res->resource, NULL);

    res->resource->flags = flags;
    res->resource->get = _sol_oic_resource_type_get;
    res->resource->post = _sol_oic_resource_type_post;
    res->resource->put = _sol_oic_resource_type_put;
    res->resource->delete = _sol_oic_resource_type_delete;

    res->resource->resource_type = resource_type->resource_type;
    res->resource->iface = resource_type->iface;

    if (!sol_coap_server_register_resource(oic_server.server, res->resource, res)) {
        SOL_WRN("Could not register OIC resource type");
        (void)sol_vector_del(&definition->resource_types, definition->resource_types.len - 1);
        return NULL;
    }

    return res->resource;
}

static char *
path_array_to_str(const struct sol_str_slice path[])
{
    char *buffer, *ptr;
    size_t len = 0;
    int i;

    for (i = 0; path[i].len; i++)
        len += path[i].len + 1; /* +1 for the slash */

    buffer = malloc(len + 1);
    if (!buffer)
        return NULL;

    for (ptr = buffer, i = 0; path[i].len; i++) {
        ptr = mempcpy(ptr, path[i].data, path[i].len);
        *ptr++ = '/';
    }

    *(ptr - 1) = '\0';
    return buffer;
}

SOL_API bool
sol_oic_notify_observers(struct sol_coap_resource *resource, uint8_t *msg, uint16_t msg_len)
{
    struct sol_coap_packet *pkt;
    char *href;
    uint8_t *payload;
    uint16_t len;
    int r;

    SOL_NULL_CHECK(resource, false);
    SOL_NULL_CHECK(msg, false);

    pkt = sol_coap_packet_notification_new(oic_server.server, resource);
    SOL_NULL_CHECK(pkt, false);

    if (sol_coap_packet_get_payload(pkt, &payload, &len) < 0) {
        sol_coap_packet_unref(pkt);
        return false;
    }

    href = path_array_to_str(resource->path);
    if (!href) {
        sol_coap_packet_unref(pkt);
        return false;
    }

    r = snprintf((char *)payload, len, "{\"oc\":[{\"href\":\"/%s\",\"rep\":%.*s}]}",
        href, (int)msg_len, msg);
    if (r < 0 || r >= len) {
        sol_coap_header_set_code(pkt, SOL_COAP_RSPCODE_INTERNAL_ERROR);
    } else {
        sol_coap_header_set_code(pkt, SOL_COAP_RSPCODE_CONTENT);
        sol_coap_packet_set_payload_used(pkt, r);
    }

    free(href);
    return !sol_coap_packet_send_notification(oic_server.server, resource, pkt);
}
