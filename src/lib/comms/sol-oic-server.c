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
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define SOL_LOG_DOMAIN &_sol_oic_server_log_domain

#include "tinycbor/cbor.h"
#include "sol-coap.h"
#include "sol-json.h"
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-platform.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "sol-oic-cbor.h"
#include "sol-oic.h"
#include "sol-oic-common.h"
#include "sol-oic-server.h"
#include "sol-oic-security.h"

#define STR(s) #s
#define TO_STRING(s) STR(s)

#define OIC_DATA_MODEL_VERSION "res.1.0.0"
#define OIC_SPEC_VERSION "core.1.0.0"

SOL_LOG_INTERNAL_DECLARE(_sol_oic_server_log_domain, "oic-server");

struct sol_oic_server_request {
    struct sol_oic_request base;
    struct sol_network_link_addr cliaddr;
    struct sol_oic_map_reader reader;
    struct sol_coap_server *server;
};

struct sol_oic_server {
    struct sol_coap_server *server;
    struct sol_coap_server *server_unicast;
    struct sol_coap_server *dtls_server;
    struct sol_ptr_vector resources;
    struct sol_oic_platform_info *plat_info;
    struct sol_oic_device_info *server_info;
    struct sol_oic_security *security;
    struct sol_ptr_vector requests;
    struct sol_ptr_vector responses;
    int refcnt;
};

struct sol_oic_response {
    struct sol_coap_packet *pkt;
    struct sol_oic_map_writer writer;
    struct sol_oic_server_resource *resource;
};

struct sol_oic_server_resource {
    struct sol_coap_resource *coap;

    char *href;
    char *rt;
    char *iface;
    enum sol_oic_resource_flag flags;

    struct {
        struct {
            int (*handle)(void *data, struct sol_oic_request *request);
        } get, put, post, del;
        const void *data;
    } callback;
};

static struct sol_oic_server oic_server;
void sol_oic_server_shutdown(void);
static struct sol_oic_server_resource *sol_oic_server_register_resource_internal(const struct sol_oic_resource_type *rt, const void *handler_data, enum sol_oic_resource_flag flags);
static void sol_oic_server_unregister_resource_internal(struct sol_oic_server_resource *resource);
static void sol_oic_server_unref(void);

#define OIC_SERVER_CHECK(ret) \
    do { \
        if (SOL_UNLIKELY(oic_server.refcnt == 0)) { \
            SOL_WRN("OIC API used before initialization"); \
            return ret; \
        } \
    } while (0)

#define OIC_SERVER_CHECK_GOTO(label) \
    do { \
        if (SOL_UNLIKELY(oic_server.refcnt == 0)) { \
            SOL_WRN("OIC API used before initialization"); \
            goto label; \
        } \
    } while (0)

#define OIC_COAP_SERVER_UDP_PORT  5683
#define OIC_COAP_SERVER_DTLS_PORT 5684

#define APPEND_KEY_VALUE(info, k, v) \
    do { \
        r = sol_oic_map_append(&response->writer, &SOL_OIC_REPR_TEXT_STRING(k, \
            oic_server.info->v.data, oic_server.info->v.len)); \
    } while (0)

static int
_sol_oic_server_d(void *data, struct sol_oic_request *request)
{
#ifndef OIC_SERVER_COMPAT_1_0
    SOL_BUFFER_DECLARE_STATIC(dev_id, 37);
#endif
    struct sol_oic_response *response;
    int r;

    response = sol_oic_server_response_new(request);
    SOL_NULL_CHECK(response, -ENOMEM);

    APPEND_KEY_VALUE(server_info, SOL_OIC_KEY_DEVICE_NAME, device_name);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(server_info, SOL_OIC_KEY_SPEC_VERSION, spec_version);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(server_info, SOL_OIC_KEY_DATA_MODEL_VERSION,
        data_model_version);
    SOL_INT_CHECK_GOTO(r, < 0, error);

#ifndef OIC_SERVER_COMPAT_1_0
    r = sol_util_uuid_string_from_bytes(true, true,
        sol_platform_get_machine_id_as_bytes(), &dev_id);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_oic_map_append(&response->writer, &SOL_OIC_REPR_TEXT_STRING(
        SOL_OIC_KEY_DEVICE_ID, dev_id.data, dev_id.used));
#else
    r = sol_oic_map_append(&response->writer, &SOL_OIC_REPR_BYTE_STRING(
        SOL_OIC_KEY_DEVICE_ID, (char *)sol_platform_get_machine_id_as_bytes(),
        MACHINE_ID_LEN));
#endif
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return sol_oic_server_send_response(request, response,
        SOL_COAP_RESPONSE_CODE_CONTENT);

error:
    sol_oic_server_response_free(response);
    return sol_oic_server_send_response(request, NULL,
        SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR);
}

static int
_sol_oic_server_p(void *data, struct sol_oic_request *request)
{
    struct sol_oic_response *response;
    const char *os_version;
    int r;

    response = sol_oic_server_response_new(request);
    SOL_NULL_CHECK(response, -ENOMEM);

    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_MANUF_NAME, manufacturer_name);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_MANUF_URL, manufacturer_url);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_MODEL_NUM, model_number);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_MANUF_DATE, manufacture_date);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_PLATFORM_VER, platform_version);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_HW_VER, hardware_version);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_FIRMWARE_VER, firmware_version);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_SUPPORT_URL, support_url);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_PLATFORM_ID, platform_id);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    APPEND_KEY_VALUE(plat_info, SOL_OIC_KEY_SYSTEM_TIME, platform_id);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_oic_map_append(&response->writer, &SOL_OIC_REPR_TEXT_STRING(
        SOL_OIC_KEY_SYSTEM_TIME, NULL, 0));
    SOL_INT_CHECK_GOTO(r, < 0, error);

    os_version = sol_platform_get_os_version();
    if (!os_version)
        os_version = "Unknown";
    r = sol_oic_map_append(&response->writer, &SOL_OIC_REPR_TEXT_STRING(
        SOL_OIC_KEY_OS_VER, os_version, strlen(os_version)));
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return sol_oic_server_send_response(request, response,
        SOL_COAP_RESPONSE_CODE_CONTENT);

error:
    sol_oic_server_response_free(response);
    return sol_oic_server_send_response(request, NULL,
        SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR);
}

#undef APPEND_KEY_VALUE

static const struct sol_oic_resource_type oic_d_resource_type = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .resource_type = SOL_STR_SLICE_LITERAL("oic.wk.d"),
    .interface = SOL_STR_SLICE_LITERAL("oic.if.r"),
    .path = SOL_STR_SLICE_LITERAL(SOL_OIC_DEVICE_PATH),
    .get = {
        .handle = _sol_oic_server_d
    }
};

static const struct sol_oic_resource_type oic_p_resource_type = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .resource_type = SOL_STR_SLICE_LITERAL("oic.wk.p"),
    .interface = SOL_STR_SLICE_LITERAL("oic.if.r"),
    .path = SOL_STR_SLICE_LITERAL(SOL_OIC_PLATFORM_PATH),
    .get = {
        .handle = _sol_oic_server_p
    }
};

static bool
iface_contains(const char *iface, struct sol_str_slice query)
{

    const char *p = NULL;
    struct sol_str_slice token, slice = sol_str_slice_from_str(iface),
        delim = SOL_STR_SLICE_LITERAL(" ");

    while (sol_str_slice_split_iterate(slice, &token, &p, delim)) {
        if (sol_str_slice_eq(query, token))
            return true;
    }

    return false;
}

static bool
oic_query_split(struct sol_str_slice query, struct sol_str_slice *key, struct sol_str_slice *value)
{
    const char *sep;

    sep = memchr(query.data, '=', query.len);
    SOL_NULL_CHECK(sep, false);
    *key = SOL_STR_SLICE_STR(query.data, sep - query.data);
    *value = SOL_STR_SLICE_STR(sep + 1, query.len - (sep - query.data + 1));

    return true;
}

#ifndef OIC_SERVER_COMPAT_1_0
static CborError
encode_array_from_bsv(CborEncoder *map, const char *val)
{
    const char *p = NULL;
    struct sol_str_slice token, slice = sol_str_slice_from_str(val),
        delim = SOL_STR_SLICE_LITERAL(" ");
    CborEncoder array;
    CborError err;

    err = cbor_encoder_create_array(map, &array, CborIndefiniteLength);

    while (sol_str_slice_split_iterate(slice, &token, &p, delim))
        err |= cbor_encode_text_string(&array, token.data, token.len);

    err |= cbor_encoder_close_container(map, &array);
    return err;
}
#endif

static struct sol_coap_server *
get_server_for_response(struct sol_coap_server *server)
{
    if (server == oic_server.server)
        return oic_server.server_unicast;

    return server;
}

static CborError
res_payload_do(CborEncoder *encoder,
    uint8_t *buf,
    size_t buflen,
    const struct sol_str_slice query_rt,
    const struct sol_str_slice query_if,
    struct sol_buffer *dev_id,
    const uint8_t **encoder_start)
{
    CborEncoder array, device_map, array_res;
    struct sol_oic_server_resource *iter;
    CborError err;
    uint16_t idx;

    cbor_encoder_init(encoder, buf, buflen, 0);
    *encoder_start = encoder->data.ptr;

    err = cbor_encoder_create_array(encoder, &array, 1);
    err |= cbor_encoder_create_map(&array, &device_map, 2);
    err |= cbor_encode_text_stringz(&device_map, SOL_OIC_KEY_DEVICE_ID);

#ifndef OIC_SERVER_COMPAT_1_0
    err |= cbor_encode_text_string(&device_map, dev_id->data, dev_id->used);
#else
    err |= cbor_encode_byte_string(&device_map,
        sol_platform_get_machine_id_as_bytes(), MACHINE_ID_LEN);
#endif
    err |= cbor_encode_text_stringz(&device_map, SOL_OIC_KEY_RESOURCE_LINKS);
    err |= cbor_encoder_create_array(&device_map, &array_res,
        CborIndefiniteLength);

    if (!buflen)
        SOL_INT_CHECK_GOTO(err, != CborErrorOutOfMemory, error);
    else
        SOL_INT_CHECK_GOTO(err, != CborNoError, error);

    SOL_PTR_VECTOR_FOREACH_IDX (&oic_server.resources, iter, idx) {
        CborEncoder map, policy_map;

        if (!(iter->flags & SOL_OIC_FLAG_DISCOVERABLE)) {
            if (!(iter->flags & SOL_OIC_FLAG_DISCOVERABLE_EXPLICIT))
                continue;

            if (!query_rt.len && !query_if.len)
                continue;
        }

        if (!(iter->flags & SOL_OIC_FLAG_ACTIVE))
            continue;

        if (query_rt.len &&
            (!iter->rt || !sol_str_slice_str_eq(query_rt, iter->rt)))
            continue;

        if (query_if.len &&
            (!iter->iface || !iface_contains(iter->iface, query_if)))
            continue;

        err |= cbor_encoder_create_map(&array_res, &map,
            !!iter->iface + !!iter->rt + 2);

        err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_HREF);
        err |= cbor_encode_text_stringz(&map, iter->href);

        if (iter->iface) {
            err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_INTERFACES);
#ifndef OIC_SERVER_COMPAT_1_0
            err |= encode_array_from_bsv(&map, iter->iface);
#else
            err |= cbor_encode_text_stringz(&map, iter->iface);
#endif
        }

        if (iter->rt) {
            err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_RESOURCE_TYPES);
#ifndef OIC_SERVER_COMPAT_1_0
            err |= encode_array_from_bsv(&map, iter->rt);
#else
            err |= cbor_encode_text_stringz(&map, iter->rt);
#endif
        }

        err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_POLICY);
        err |= cbor_encoder_create_map(&map, &policy_map,
            CborIndefiniteLength);
        err |= cbor_encode_text_stringz(&policy_map, SOL_OIC_KEY_BITMAP);
        err |= cbor_encode_uint(&policy_map, iter->flags &
            (SOL_OIC_FLAG_OBSERVABLE | SOL_OIC_FLAG_DISCOVERABLE));
        if ((iter->flags & SOL_OIC_FLAG_SECURE) == SOL_OIC_FLAG_SECURE) {
            err |= cbor_encode_text_stringz(&policy_map,
                SOL_OIC_KEY_POLICY_SECURE);
            err |= cbor_encode_boolean(&policy_map, true);
            err |= cbor_encode_text_stringz(&policy_map,
                SOL_OIC_KEY_POLICY_PORT);
            err |= cbor_encode_uint(&policy_map, OIC_COAP_SERVER_DTLS_PORT);
        }

        err |= cbor_encoder_close_container(&map, &policy_map);

        err |= cbor_encoder_close_container(&array_res, &map);
    }

    err |= cbor_encoder_close_container(&device_map, &array_res);
    err |= cbor_encoder_close_container(&array, &device_map);
    err |= cbor_encoder_close_container(encoder, &array);

error:
    return err;
}

#define QUERY_LEN 2
static int
_sol_oic_server_res(void *data, struct sol_coap_server *server,
    const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
#ifndef OIC_SERVER_COMPAT_1_0
    SOL_BUFFER_DECLARE_STATIC(dev_id, 37);
#else
    struct sol_buffer dev_id = SOL_BUFFER_INIT_EMPTY;
#endif
    const uint8_t format_cbor = SOL_COAP_CONTENT_TYPE_APPLICATION_CBOR;
    const uint8_t *encoder_start;
    struct sol_coap_packet *resp;
    struct sol_str_slice query[QUERY_LEN];
    struct sol_str_slice key, value, query_rt = SOL_STR_SLICE_EMPTY,
        query_if = SOL_STR_SLICE_EMPTY;
    struct sol_buffer *buf;
    CborEncoder encoder;
    size_t offset;
    CborError err;
    int r, query_count;
    uint16_t idx;

    query_count = sol_coap_find_options(req, SOL_COAP_OPTION_URI_QUERY, query,
        QUERY_LEN);
    SOL_INT_CHECK(query_count, < 0, query_count);

    for (idx = 0; idx < query_count; idx++) {
        if (oic_query_split(query[idx], &key, &value)) {
            if (!query_rt.len && sol_str_slice_str_eq(key, "rt")) {
                query_rt = value;
                continue;
            }
            if (!query_if.len && sol_str_slice_str_eq(key, "if")) {
                query_if = value;
                continue;
            }
        }
        SOL_WRN("Invalid query parameter: %.*s", SOL_STR_SLICE_PRINT(query[idx]));
        return SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    }

#ifndef OIC_SERVER_COMPAT_1_0
    r = sol_util_uuid_string_from_bytes(true, true,
        sol_platform_get_machine_id_as_bytes(), &dev_id);
    SOL_INT_CHECK(r, < 0, ENOMEM);
#endif

    resp = sol_coap_packet_new(req);
    SOL_NULL_CHECK(resp, -ENOMEM);

    sol_coap_add_option(resp, SOL_COAP_OPTION_CONTENT_FORMAT, &format_cbor, sizeof(format_cbor));

    r = sol_coap_packet_get_payload(resp, &buf, &offset);
    if (r < 0) {
        sol_coap_packet_unref(resp);
        return r;
    }

    /* phony run, to calc size */
    err = res_payload_do(&encoder, NULL, 0, query_rt, query_if, &dev_id,
        &encoder_start);

    if (err != CborErrorOutOfMemory)
        goto err;

    SOL_DBG("Ensuring OIC (cbor) payload of size %td", cbor_encoder_get_extra_bytes_needed(&encoder));
    r = sol_buffer_ensure(buf, cbor_encoder_get_extra_bytes_needed(&encoder) + offset);
    if (r < 0) {
        sol_coap_packet_unref(resp);
        return r;
    }

    /* now encode for sure */
    err = res_payload_do(&encoder, sol_buffer_at(buf, offset),
        cbor_encoder_get_extra_bytes_needed(&encoder), query_rt, query_if, &dev_id, &encoder_start);

err:
    if (err != CborNoError) {
        SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

        sol_network_link_addr_to_str(cliaddr, &addr);
        SOL_WRN("Error building response for /oic/res, server %p client %.*s: %s",
            oic_server.server, SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)),
            cbor_error_string(err));

        sol_coap_header_set_code(resp, SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR);
    } else {
        sol_coap_header_set_code(resp, SOL_COAP_RESPONSE_CODE_CONTENT);
        /* Ugly, but since tinycbor operates on memory slices
         * directly, we have to resort to that */
        buf->used += encoder.data.ptr - encoder_start;
    }

    return sol_coap_send_packet(get_server_for_response(server), resp, cliaddr);
}
#undef QUERY_LEN

static const struct sol_coap_resource oic_res_coap_resource = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .path = {
        SOL_STR_SLICE_LITERAL("oic"),
        SOL_STR_SLICE_LITERAL("res"),
        SOL_STR_SLICE_EMPTY
    },
    .get = _sol_oic_server_res,
    .flags = SOL_COAP_FLAGS_NONE
};

static struct sol_oic_platform_info *
init_static_plat_info(void)
{
    struct sol_oic_platform_info plat_info = {
        .manufacturer_name = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_MANUFACTURER_NAME)),
        .manufacturer_url = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_MANUFACTURER_URL)),
        .model_number = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_MODEL_NUMBER)),
        .manufacture_date = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_MANUFACTURE_DATE)),
        .platform_version = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_PLATFORM_VERSION)),
        .hardware_version = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_HARDWARE_VERSION)),
        .firmware_version = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_FIRMWARE_VERSION)),
        .support_url = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_SUPPORT_URL)),
    };
    struct sol_oic_platform_info *info;

    info = sol_util_memdup(&plat_info, sizeof(*info));
    SOL_NULL_CHECK(info, NULL);

    return info;
}

static struct sol_oic_device_info *
init_static_server_info(void)
{
    struct sol_oic_device_info server_info = {
        .device_name = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_DEVICE_NAME)),
        .spec_version = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_SPEC_VERSION)),
        .data_model_version = SOL_STR_SLICE_LITERAL(TO_STRING(OIC_DATA_MODEL_VERSION)),
    };
    struct sol_oic_device_info *info;

    info = sol_util_memdup(&server_info, sizeof(*info));
    SOL_NULL_CHECK(info, NULL);

    return info;
}

static bool
oic_dtls_server_init(void)
{
    oic_server.security = sol_oic_server_security_add(oic_server.server,
        oic_server.dtls_server);
    if (!oic_server.security) {
        SOL_WRN("OIC server security subsystem could not be initialized");
        return false;
    }

    return true;
}

static int
sol_oic_server_ref(void)
{
    struct sol_oic_platform_info *plat_info = NULL;
    struct sol_oic_device_info *server_info = NULL;
    struct sol_oic_server_resource *res;
    struct sol_network_link_addr servaddr = { .family = SOL_NETWORK_FAMILY_INET6,
                                              .port = OIC_COAP_SERVER_UDP_PORT };
    int r = -ENOMEM;

    if (oic_server.refcnt > 0) {
        oic_server.refcnt++;
        return 0;
    }

    SOL_LOG_INTERNAL_INIT_ONCE;

    plat_info = init_static_plat_info();
    SOL_NULL_CHECK(plat_info, -ENOMEM);

    server_info = init_static_server_info();
    SOL_NULL_CHECK_GOTO(server_info, error);

    oic_server.server = sol_coap_server_new(&servaddr, false);
    if (!oic_server.server) {
        r = -ENOMEM;
        goto error;
    }

    servaddr.port = 0;
    oic_server.server_unicast = sol_coap_server_new(&servaddr, false);
    if (!oic_server.server_unicast) {
        r = -ENOMEM;
        goto error;
    }

    r = sol_coap_server_register_resource(oic_server.server,
        &oic_res_coap_resource, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_coap_server_register_resource(oic_server.server_unicast,
        &oic_res_coap_resource, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    oic_server.security = NULL;
    servaddr.port = OIC_COAP_SERVER_DTLS_PORT;
    oic_server.dtls_server = sol_coap_server_new(&servaddr, true);
    if (!oic_server.dtls_server) {
        if (errno == ENOSYS) {
            SOL_INF("DTLS support not built in, OIC server running in insecure mode");
        } else {
            SOL_INF("DTLS server could not be created for OIC server: %s",
                sol_util_strerrora(errno));
        }
    } else if (!oic_dtls_server_init()) {
        SOL_INF("OIC server running in insecure mode.");
        sol_coap_server_unref(oic_server.dtls_server);
        oic_server.dtls_server = NULL;
    }

    oic_server.server_info = server_info;
    oic_server.plat_info = plat_info;
    sol_ptr_vector_init(&oic_server.resources);
    sol_ptr_vector_init(&oic_server.requests);
    sol_ptr_vector_init(&oic_server.responses);

    oic_server.refcnt++;

    res = sol_oic_server_register_resource_internal(&oic_d_resource_type, NULL,
        SOL_OIC_FLAG_DISCOVERABLE | SOL_OIC_FLAG_ACTIVE);
    SOL_NULL_CHECK_GOTO(res, error_shutdown);

    res = sol_oic_server_register_resource_internal(&oic_p_resource_type, NULL,
        SOL_OIC_FLAG_DISCOVERABLE | SOL_OIC_FLAG_ACTIVE);
    SOL_NULL_CHECK_GOTO(res, error_shutdown);

    return 0;
error:
    if (oic_server.server)
        sol_coap_server_unref(oic_server.server);

    if (oic_server.server_unicast)
        sol_coap_server_unref(oic_server.server_unicast);

    free(server_info);
    free(plat_info);
    return r;

error_shutdown:
    sol_oic_server_unref();
    return -ENOMEM;
}

static void
sol_oic_server_shutdown_internal(void)
{
    struct sol_oic_server_resource *res;
    uint16_t idx;

    if (oic_server.security)
        sol_oic_server_security_del(oic_server.security);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&oic_server.resources, res, idx)
        sol_oic_server_unregister_resource_internal(res);

    if (oic_server.dtls_server)
        sol_coap_server_unref(oic_server.dtls_server);

    sol_coap_server_unregister_resource(oic_server.server,
        &oic_res_coap_resource);

    sol_coap_server_unref(oic_server.server);
    sol_coap_server_unref(oic_server.server_unicast);

    free(oic_server.server_info);
    free(oic_server.plat_info);

    sol_util_clear_memory_secure(&oic_server, sizeof(oic_server));
}

void
sol_oic_server_shutdown(void)
{
    if (oic_server.refcnt == 0)
        return;

    sol_oic_server_shutdown_internal();
    oic_server.refcnt = 0;
}

static void
sol_oic_server_unref(void)
{

    OIC_SERVER_CHECK();

    if (--oic_server.refcnt > 0)
        return;

    sol_oic_server_shutdown_internal();
}

static void
server_request_free(struct sol_oic_server_request *request)
{
    int32_t i;

    if (!request)
        return;

    i = sol_ptr_vector_find_last(&oic_server.requests, request);
    SOL_INT_CHECK(i, < 0);

    oic_request_free((struct sol_oic_request *)request);
    sol_ptr_vector_del(&oic_server.requests, i);
}

SOL_API void
sol_oic_server_response_free(struct sol_oic_response *response)
{
    int32_t i;

    if (!response)
        return;

    i = sol_ptr_vector_find_last(&oic_server.responses, response);
    SOL_INT_CHECK(i, < 0);

    if (response->pkt)
        sol_coap_packet_unref(response->pkt);
    free(response);

    sol_ptr_vector_del(&oic_server.responses, i);
}

static struct sol_oic_response *
create_response(void)
{
    struct sol_oic_response *response;

    response = calloc(1, sizeof(struct sol_oic_response));
    SOL_NULL_CHECK(response, NULL);

    if (sol_ptr_vector_append(&oic_server.responses, response) < 0) {
        free(response);
        return NULL;
    }

    return response;
}

SOL_API struct sol_oic_response *
sol_oic_server_response_new(struct sol_oic_request *request)
{
    struct sol_oic_response *response;

    response = create_response();
    SOL_NULL_CHECK(response, NULL);

    response->pkt = sol_coap_packet_new(request->pkt);
    SOL_NULL_CHECK_GOTO(response->pkt, error);

    sol_oic_packet_cbor_create(response->pkt, &response->writer);

    return response;

error:
    free(response);
    return NULL;
}

static struct sol_oic_request *
server_request_new(const struct sol_network_link_addr *cliaddr, struct sol_coap_server *server, struct sol_coap_packet *pkt)
{
    struct sol_oic_server_request *request;

    request = calloc(1, sizeof(struct sol_oic_server_request));
    SOL_NULL_CHECK(request, NULL);

    if (sol_ptr_vector_append(&oic_server.requests, request) < 0) {
        free(request);
        return NULL;
    }
    request->cliaddr = *cliaddr;
    request->server = server;
    request->base.pkt = sol_coap_packet_ref(pkt);
    request->base.is_server_request = true;

    return (struct sol_oic_request *)request;
}

static int
_sol_oic_resource_type_handle(
    int (*handle_fn)(void *data, struct sol_oic_request *request),
    struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    struct sol_oic_server_resource *res, bool expect_payload)
{
    struct sol_coap_packet *response_pkt = NULL;
    struct sol_oic_server_request *request = NULL;
    enum sol_coap_response_code code = SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
    CborParser parser;
    int r;

    OIC_SERVER_CHECK(-ENOTCONN);

    if (!handle_fn) {
        code = SOL_COAP_RESPONSE_CODE_NOT_IMPLEMENTED;
        goto error;
    }

    request = (struct sol_oic_server_request *)
        server_request_new(cliaddr, server, req);
    SOL_NULL_CHECK_GOTO(request, error);

    if (expect_payload) {
        if (!sol_oic_pkt_has_cbor_content(req)) {
            code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
            goto error;
        }
        if (sol_oic_packet_cbor_extract_repr_map(req, &parser,
            (CborValue *)&request->reader) != CborNoError) {
            code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
            goto error;
        }
    } else
        ((CborValue *)&request->reader)->type = CborInvalidType;

    r = handle_fn((void *)res->callback.data, (struct sol_oic_request *)request);
    if (r < 0)
        server_request_free(request);
    return r;

error:
    server_request_free(request);

    response_pkt = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response_pkt, -errno);
    sol_coap_header_set_code(response_pkt, code);
    return sol_coap_send_packet(get_server_for_response(server), response_pkt, cliaddr);
}

SOL_API struct sol_oic_map_writer *
sol_oic_server_response_get_writer(struct sol_oic_response *response)
{
    SOL_NULL_CHECK(response, NULL);

    return &response->writer;
}

SOL_API struct sol_oic_map_reader *
sol_oic_server_request_get_reader(struct sol_oic_request *request)
{
    SOL_NULL_CHECK(request, NULL);

    if (!request->is_server_request) {
        SOL_WRN("Request packet is not a request created by oic server");
        return NULL;
    }

    return &((struct sol_oic_server_request *)request)->reader;
}

SOL_API int
sol_oic_server_send_response(struct sol_oic_request *request, struct sol_oic_response *response, enum sol_coap_response_code code)
{
    struct sol_coap_packet *pkt;
    struct sol_oic_server_request *req = (struct sol_oic_server_request *)request;
    int r = -EINVAL;

    SOL_NULL_CHECK_GOTO(request, end);
    OIC_SERVER_CHECK_GOTO(error_server);

    if (response) {
        if (sol_oic_packet_cbor_close(response->pkt,
            &response->writer) != CborNoError) {
            goto end;
        }
        pkt = response->pkt;
        response->pkt = NULL;
    } else
        pkt = sol_coap_packet_new(request->pkt);

    r = sol_coap_header_set_code(pkt, code);
    SOL_INT_CHECK_GOTO(r, < 0, error_pkt);

    r = sol_coap_send_packet(get_server_for_response(req->server), pkt, &(req->cliaddr));
    goto end;

error_pkt:
    sol_coap_packet_unref(pkt);
end:
    server_request_free(req);
    sol_oic_server_response_free(response);
    return r;

error_server:
    server_request_free(req);
    sol_oic_server_response_free(response);
    return -ENOTCONN;
}

#define DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(method, expect_payload) \
    static int \
    _sol_oic_resource_type_ ## method(void *data, struct sol_coap_server *server, \
    const struct sol_coap_resource *resource, struct sol_coap_packet *req, \
    const struct sol_network_link_addr *cliaddr) \
    { \
        struct sol_oic_server_resource *res = data; \
        return _sol_oic_resource_type_handle(res->callback.method.handle, \
            server, req, cliaddr, res, expect_payload); \
    }

DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(get, false)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(put, true)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(post, true)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(del, false)

#undef DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD

static struct sol_coap_resource *
create_coap_resource(struct sol_oic_server_resource *resource)
{
    const struct sol_str_slice endpoint = sol_str_slice_from_str(resource->href);
    struct sol_coap_resource *res;
    unsigned int count = 0;
    unsigned int current;
    size_t i;

    for (i = 0; i < endpoint.len; i++)
        if (endpoint.data[i] == '/')
            count++;

    SOL_INT_CHECK(count, == 0, NULL);

    if (endpoint.data[0] != '/') {
        SOL_WRN("Invalid endpoint - Path '%.*s' does not start with '/'", SOL_STR_SLICE_PRINT(endpoint));
        return NULL;
    }

    if (endpoint.data[endpoint.len - 1] == '/') {
        SOL_WRN("Invalid endpoint - Path '%.*s' ends with '/'", SOL_STR_SLICE_PRINT(endpoint));
        return NULL;
    }

    /* alloc space for the path plus empty slice at the end */
    res = calloc(1, sizeof(struct sol_coap_resource) + (count + 1) * sizeof(struct sol_str_slice));
    SOL_NULL_CHECK(res, NULL);

    SOL_SET_API_VERSION(res->api_version = SOL_COAP_RESOURCE_API_VERSION; )

    res->path[0].data = &endpoint.data[1];
    for (i = 1, current = 0; i < endpoint.len; i++) {
        if (endpoint.data[i] == '/')
            res->path[++current].data = &endpoint.data[i + 1];
        else
            res->path[current].len++;
    }

    res->get = _sol_oic_resource_type_get;
    res->put = _sol_oic_resource_type_put;
    res->post = _sol_oic_resource_type_post;
    res->del = _sol_oic_resource_type_del;

    if (resource->flags & SOL_OIC_FLAG_DISCOVERABLE)
        res->flags |= SOL_COAP_FLAGS_WELL_KNOWN;

    if (oic_server.dtls_server)
        resource->flags |= SOL_OIC_FLAG_SECURE;

    return res;
}

static char *
create_endpoint(void)
{
    static unsigned int id = 0;
    char *buffer = NULL;
    int r;

    if (id == UINT_MAX) {
        SOL_WRN("Resource name overflow. Maximum number of resources reached.");
        return NULL;
    }
    r = asprintf(&buffer, "/sol/%x", id++);
    return r < 0 ? NULL : buffer;
}

struct sol_oic_server_resource *
sol_oic_server_register_resource_internal(const struct sol_oic_resource_type *rt,
    const void *handler_data, enum sol_oic_resource_flag flags)
{
    struct sol_oic_server_resource *res;

    res = malloc(sizeof(struct sol_oic_server_resource));
    SOL_NULL_CHECK(res, NULL);

    res->callback.data = handler_data;
    res->callback.get.handle = rt->get.handle;
    res->callback.put.handle = rt->put.handle;
    res->callback.post.handle = rt->post.handle;
    res->callback.del.handle = rt->del.handle;
    res->flags = flags;

    res->rt = strndup(rt->resource_type.data, rt->resource_type.len);
    SOL_NULL_CHECK_GOTO(res->rt, remove_res);

    res->iface = strndup(rt->interface.data, rt->interface.len);
    SOL_NULL_CHECK_GOTO(res->iface, free_rt);

    if (!rt->path.data || !rt->path.len)
        res->href = create_endpoint();
    else
        res->href = strndup(rt->path.data, rt->path.len);
    SOL_NULL_CHECK_GOTO(res->href, free_iface);

    res->coap = create_coap_resource(res);
    SOL_NULL_CHECK_GOTO(res->coap, free_href);

    if (sol_coap_server_register_resource(oic_server.server, res->coap, res) < 0)
        goto free_coap;

    if (sol_coap_server_register_resource(oic_server.server_unicast, res->coap, res) < 0)
        goto unregister_resource;

    if (oic_server.dtls_server) {
        if (sol_coap_server_register_resource(oic_server.dtls_server, res->coap, res) < 0) {
            SOL_WRN("Could not register resource in DTLS server");
            goto unregister_resource_unicast;
        }
    }

    if (sol_ptr_vector_append(&oic_server.resources, res) < 0)
        goto unregister_resource_dtls;

    return res;

unregister_resource_dtls:
    if (oic_server.dtls_server)
        sol_coap_server_unregister_resource(oic_server.dtls_server, res->coap);
unregister_resource_unicast:
    sol_coap_server_unregister_resource(oic_server.server_unicast, res->coap);
unregister_resource:
    sol_coap_server_unregister_resource(oic_server.server, res->coap);
free_coap:
    free(res->coap);
free_href:
    free(res->href);
free_iface:
    free(res->iface);
free_rt:
    free(res->rt);
remove_res:
    free(res);

    return NULL;
}

SOL_API struct sol_oic_server_resource *
sol_oic_server_register_resource(const struct sol_oic_resource_type *rt,
    const void *handler_data, enum sol_oic_resource_flag flags)
{
    int r;

    SOL_NULL_CHECK(rt, NULL);
    r = sol_oic_server_ref();
    SOL_INT_CHECK(r, < 0, NULL);

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(rt->api_version != SOL_OIC_RESOURCE_TYPE_API_VERSION)) {
        SOL_WRN("Couldn't add resource_type with "
            "version '%" PRIu16 "'. Expected version '%" PRIu16 "'.",
            rt->api_version, SOL_OIC_RESOURCE_TYPE_API_VERSION);
        return NULL;
    }
#endif

    return sol_oic_server_register_resource_internal(rt, handler_data, flags);
}

static void
sol_oic_server_unregister_resource_internal(struct sol_oic_server_resource *resource)
{
    sol_coap_server_unregister_resource(oic_server.server, resource->coap);
    sol_coap_server_unregister_resource(oic_server.server_unicast, resource->coap);
    if (oic_server.dtls_server)
        sol_coap_server_unregister_resource(oic_server.dtls_server, resource->coap);
    free(resource->coap);

    free(resource->href);
    free(resource->iface);
    free(resource->rt);
    if (sol_ptr_vector_del_element(&oic_server.resources, resource) < 0)
        SOL_ERR("Could not find resource %p in OIC server resource list",
            resource);
    free(resource);
}

SOL_API void
sol_oic_server_unregister_resource(struct sol_oic_server_resource *resource)
{
    OIC_SERVER_CHECK();
    SOL_NULL_CHECK(resource);

    sol_oic_server_unregister_resource_internal(resource);
    sol_oic_server_unref();
}

SOL_API struct sol_oic_response *
sol_oic_server_notification_new(struct sol_oic_server_resource *resource)
{
    struct sol_oic_response *notification;

    notification = create_response();
    SOL_NULL_CHECK(notification, NULL);

    notification->resource = resource;
    notification->pkt = sol_coap_packet_new_notification(oic_server.server,
        resource->coap);
    SOL_NULL_CHECK_GOTO(notification->pkt, error);
    sol_oic_packet_cbor_create(notification->pkt, &notification->writer);

    return notification;

error:
    free(notification);
    return NULL;
}

SOL_API int
sol_oic_server_notify(struct sol_oic_response *notification)
{
    int r = -ENOTCONN;
    uint8_t code = SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;
    CborError err;

    SOL_NULL_CHECK(notification, -EINVAL);
    OIC_SERVER_CHECK_GOTO(error);

    if (!notification->resource) {
        SOL_WRN("Response is not a notification response.");
        r = -EINVAL;
        goto error;
    }

    err = sol_oic_packet_cbor_close(notification->pkt, &notification->writer);
    SOL_INT_CHECK_GOTO(err, != CborNoError, close_error);

    code = SOL_COAP_RESPONSE_CODE_CONTENT;
close_error:
    r = sol_coap_header_set_code(notification->pkt, code);
    SOL_INT_CHECK_GOTO(r, < 0, error);
    r = sol_coap_header_set_type(notification->pkt, SOL_COAP_MESSAGE_TYPE_ACK);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    // Keep reference to CoAP packet, because sol_coap_notify()
    // releases its memory, even on errors
    sol_coap_packet_ref(notification->pkt);
    r = sol_coap_notify(oic_server.server_unicast,
        notification->resource->coap, notification->pkt);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_coap_packet_ref(notification->pkt);
    if (oic_server.dtls_server)
        r = sol_coap_notify(oic_server.dtls_server,
            notification->resource->coap, notification->pkt);
    else
        sol_coap_packet_unref(notification->pkt);

error:
    sol_oic_server_response_free(notification);
    return r;
}
