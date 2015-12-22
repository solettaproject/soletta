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

#include "cbor.h"
#include "sol-coap.h"
#include "sol-json.h"
#include "sol-log-internal.h"
#include "sol-platform.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-oic-cbor.h"
#include "sol-oic-common.h"
#include "sol-oic-server.h"

SOL_LOG_INTERNAL_DECLARE(_sol_oic_server_log_domain, "oic-server");

struct sol_oic_server {
    struct sol_coap_server *server;
    struct sol_coap_server *dtls_server;
    struct sol_vector resources;
    struct sol_oic_server_information *information;
    int refcnt;
};

struct sol_oic_server_resource {
    struct sol_coap_resource *coap;

    char *href;
    char *rt;
    char *iface;
    enum sol_oic_resource_flag flags;

    struct {
        struct {
            sol_coap_responsecode_t (*handle)(const struct sol_network_link_addr *cliaddr,
                const void *data, const struct sol_oic_map_reader *input,
                struct sol_oic_map_writer *output);
        } get, put, post, del;
        const void *data;
    } callback;
};

static struct sol_oic_server oic_server;

#define OIC_SERVER_CHECK(ret) \
    do { \
        if (oic_server.refcnt == 0) { \
            SOL_WRN("OIC API used before initialization"); \
            return ret; \
        } \
    } while (0)

#define OIC_COAP_SERVER_UDP_PORT  5683
#define OIC_COAP_SERVER_DTLS_PORT 5684

static int
_sol_oic_server_d(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    const uint8_t format_cbor = SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
    CborEncoder encoder, root, map, rep_map;
    CborError err;
    struct sol_coap_packet *response;
    const char *os_version;
    uint8_t *payload;
    uint16_t size;

    OIC_SERVER_CHECK(-ENOTCONN);

    response = sol_coap_packet_new(req);
    SOL_NULL_CHECK(response, -ENOMEM);

    sol_coap_add_option(response, SOL_COAP_OPTION_CONTENT_FORMAT, &format_cbor, sizeof(format_cbor));

    if (sol_coap_packet_get_payload(response, &payload, &size) < 0) {
        SOL_WRN("Couldn't obtain payload from CoAP packet");
        goto out;
    }

    cbor_encoder_init(&encoder, payload, size, 0);

    err = cbor_encoder_create_array(&encoder, &root, 2);
    err |= cbor_encode_uint(&root, SOL_OIC_PAYLOAD_PLATFORM);

    err |= cbor_encoder_create_map(&root, &map, CborIndefiniteLength);

    err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_HREF);
    err |= cbor_encode_text_stringz(&map, "/oic/d");

    err |= cbor_encoder_create_map(&map, &rep_map, CborIndefiniteLength);

#define APPEND_KEY_VALUE(k, v) \
    do { \
        err |= cbor_encode_text_stringz(&rep_map, k); \
        err |= cbor_encode_text_string(&rep_map, \
            oic_server.information->v.data, oic_server.information->v.len); \
    } while (0)

    APPEND_KEY_VALUE(SOL_OIC_KEY_MANUF_NAME, manufacturer_name);
    APPEND_KEY_VALUE(SOL_OIC_KEY_MANUF_URL, manufacturer_url);
    APPEND_KEY_VALUE(SOL_OIC_KEY_MODEL_NUM, model_number);
    APPEND_KEY_VALUE(SOL_OIC_KEY_MANUF_DATE, manufacture_date);
    APPEND_KEY_VALUE(SOL_OIC_KEY_PLATFORM_VER, platform_version);
    APPEND_KEY_VALUE(SOL_OIC_KEY_HW_VER, hardware_version);
    APPEND_KEY_VALUE(SOL_OIC_KEY_FIRMWARE_VER, firmware_version);
    APPEND_KEY_VALUE(SOL_OIC_KEY_SUPPORT_URL, support_url);

#undef APPEND_KEY_VALUE

    err |= cbor_encode_text_stringz(&rep_map, SOL_OIC_KEY_PLATFORM_ID);
    err |= cbor_encode_byte_string(&rep_map,
        (const uint8_t *)oic_server.information->platform_id.data,
        oic_server.information->platform_id.len);

    err |= cbor_encode_text_stringz(&rep_map, SOL_OIC_KEY_SYSTEM_TIME);
    err |= cbor_encode_text_stringz(&rep_map, "");

    err |= cbor_encode_text_stringz(&rep_map, SOL_OIC_KEY_OS_VER);
    os_version = sol_platform_get_os_version();
    err |= cbor_encode_text_stringz(&rep_map, os_version ? os_version : "Unknown");

    err |= cbor_encoder_close_container(&rep_map, &map);

    err |= cbor_encoder_close_container(&map, &root);

    err |= cbor_encoder_close_container(&encoder, &root);

    if (err == CborNoError) {
        sol_coap_header_set_type(response, SOL_COAP_TYPE_ACK);
        sol_coap_header_set_code(response, SOL_COAP_RSPCODE_OK);

        sol_coap_packet_set_payload_used(response, encoder.ptr - payload);
        return sol_coap_send_packet(server, response, cliaddr);
    }

    SOL_WRN("Error encoding platform CBOR response: %s",
        cbor_error_string(err));

out:
    sol_coap_packet_unref(response);
    return -ENOMEM;
}

static const struct sol_coap_resource oic_d_coap_resource = {
    SOL_SET_API_VERSION(.api_version = SOL_COAP_RESOURCE_API_VERSION, )
    .path = {
        SOL_STR_SLICE_LITERAL("oic"),
        SOL_STR_SLICE_LITERAL("d"),
        SOL_STR_SLICE_EMPTY
    },
    .get = _sol_oic_server_d,
    .flags = SOL_COAP_FLAGS_NONE
};

static unsigned int
as_nibble(const char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    SOL_WRN("Invalid hex character: %d", c);
    return 0;
}

static const uint8_t *
get_machine_id(void)
{
    static uint8_t machine_id[16] = { 0 };
    static bool machine_id_set = false;
    const char *machine_id_buf;

    if (unlikely(!machine_id_set)) {
        machine_id_buf = sol_platform_get_machine_id();

        if (!machine_id_buf) {
            SOL_WRN("Could not get machine ID");
            memset(machine_id, 0xFF, sizeof(machine_id));
        } else {
            const char *p;
            size_t i;

            for (p = machine_id_buf, i = 0; i < 16; i++, p += 2)
                machine_id[i] = as_nibble(*p) << 4 | as_nibble(*(p + 1));
        }

        machine_id_set = true;
    }

    return machine_id;
}

static int
_sol_oic_server_res(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    CborEncoder encoder, array;
    CborError err;
    struct sol_oic_server_resource *iter;
    struct sol_coap_packet *resp;
    uint16_t size;
    const uint8_t format_cbor = SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
    uint8_t *payload;
    uint16_t idx;
    const uint8_t *uri_query;
    uint16_t uri_query_len;

    uri_query = sol_coap_find_first_option(req, SOL_COAP_OPTION_URI_QUERY, &uri_query_len);
    if (uri_query && uri_query_len > sizeof("rt=") - 1) {
        uri_query += sizeof("rt=") - 1;
        uri_query_len -= 3;
    } else {
        uri_query = NULL;
        uri_query_len = 0;
    }

    resp = sol_coap_packet_new(req);
    SOL_NULL_CHECK(resp, -ENOMEM);

    sol_coap_header_set_type(resp, SOL_COAP_TYPE_ACK);
    sol_coap_add_option(resp, SOL_COAP_OPTION_CONTENT_FORMAT, &format_cbor, sizeof(format_cbor));

    sol_coap_packet_get_payload(resp, &payload, &size);

    cbor_encoder_init(&encoder, payload, size, 0);
    if (uri_query) {
        err = cbor_encoder_create_array(&encoder, &array, CborIndefiniteLength);
    } else {
        err = cbor_encoder_create_array(&encoder, &array, 1 + oic_server.resources.len);
    }

    err |= cbor_encode_uint(&array, SOL_OIC_PAYLOAD_DISCOVERY);

    SOL_VECTOR_FOREACH_IDX (&oic_server.resources, iter, idx) {
        CborEncoder map, prop_map, policy_map;

        if (uri_query && iter->rt) {
            size_t rt_len = strlen(iter->rt);

            if (rt_len != uri_query_len)
                continue;
            if (memcmp(uri_query, iter->rt, rt_len) != 0)
                continue;
        }

        if (!(iter->flags & SOL_OIC_FLAG_DISCOVERABLE))
            continue;
        if (!(iter->flags & SOL_OIC_FLAG_ACTIVE))
            continue;

        err |= cbor_encoder_create_map(&array, &map, 3);

        err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_HREF);
        err |= cbor_encode_text_stringz(&map, iter->href);

        err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_DEVICE_ID);
        err |= cbor_encode_byte_string(&map, get_machine_id(), 16);

        err |= cbor_encode_text_stringz(&map, SOL_OIC_KEY_PROPERTIES);
        err |= cbor_encoder_create_map(&map, &prop_map, !!iter->iface + !!iter->rt + 1);

        if (iter->iface) {
            CborEncoder if_array;

            err |= cbor_encode_text_stringz(&prop_map, SOL_OIC_KEY_INTERFACES);
            err |= cbor_encoder_create_array(&prop_map, &if_array, 1);
            err |= cbor_encode_text_stringz(&if_array, iter->iface);
            err |= cbor_encoder_close_container(&prop_map, &if_array);
        }

        if (iter->rt) {
            CborEncoder rt_array;

            err |= cbor_encode_text_stringz(&prop_map, SOL_OIC_KEY_RESOURCE_TYPES);
            err |= cbor_encoder_create_array(&prop_map, &rt_array, 1);
            err |= cbor_encode_text_stringz(&rt_array, iter->rt);
            err |= cbor_encoder_close_container(&prop_map, &rt_array);
        }

        err |= cbor_encode_text_stringz(&prop_map, SOL_OIC_KEY_POLICY);
        err |= cbor_encoder_create_map(&prop_map, &policy_map, CborIndefiniteLength);
        err |= cbor_encode_text_stringz(&policy_map, SOL_OIC_KEY_BITMAP);
        err |= cbor_encode_uint(&policy_map, iter->flags);
        err |= cbor_encoder_close_container(&prop_map, &policy_map);

        err |= cbor_encoder_close_container(&map, &prop_map);

        err |= cbor_encoder_close_container(&array, &map);
    }

    err |= cbor_encoder_close_container(&encoder, &array);

    if (err != CborNoError) {
        char addr[SOL_INET_ADDR_STRLEN];
        sol_network_addr_to_str(cliaddr, addr, sizeof(addr));
        SOL_WRN("Error building response for /oc/core, server %p client %s: %s",
            oic_server.server, addr, cbor_error_string(err));

        sol_coap_header_set_code(resp, SOL_COAP_RSPCODE_INTERNAL_ERROR);
    } else {
        sol_coap_header_set_code(resp, SOL_COAP_RSPCODE_OK);
        sol_coap_packet_set_payload_used(resp, encoder.ptr - payload);
    }

    return sol_coap_send_packet(server, resp, cliaddr);
}

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

static struct sol_oic_server_information *
init_static_info(void)
{
    struct sol_oic_server_information information = {
        .manufacturer_name = SOL_STR_SLICE_LITERAL(OIC_MANUFACTURER_NAME),
        .manufacturer_url = SOL_STR_SLICE_LITERAL(OIC_MANUFACTURER_URL),
        .model_number = SOL_STR_SLICE_LITERAL(OIC_MODEL_NUMBER),
        .manufacture_date = SOL_STR_SLICE_LITERAL(OIC_MANUFACTURE_DATE),
        .platform_version = SOL_STR_SLICE_LITERAL(OIC_PLATFORM_VERSION),
        .hardware_version = SOL_STR_SLICE_LITERAL(OIC_HARDWARE_VERSION),
        .firmware_version = SOL_STR_SLICE_LITERAL(OIC_FIRMWARE_VERSION),
        .support_url = SOL_STR_SLICE_LITERAL(OIC_SUPPORT_URL)
    };
    struct sol_oic_server_information *info;

    information.platform_id = SOL_STR_SLICE_STR((const char *)get_machine_id(), 16);

    info = sol_util_memdup(&information, sizeof(*info));
    SOL_NULL_CHECK(info, NULL);

    return info;
}

SOL_API int
sol_oic_server_init(void)
{
    struct sol_oic_server_information *info;

    if (oic_server.refcnt > 0) {
        oic_server.refcnt++;
        return 0;
    }

    SOL_LOG_INTERNAL_INIT_ONCE;

    info = init_static_info();
    SOL_NULL_CHECK(info, -1);

    oic_server.server = sol_coap_server_new(OIC_COAP_SERVER_UDP_PORT);
    if (!oic_server.server)
        goto error;

    if (!sol_coap_server_register_resource(oic_server.server, &oic_d_coap_resource, NULL))
        goto error;
    if (!sol_coap_server_register_resource(oic_server.server, &oic_res_coap_resource, NULL)) {
        sol_coap_server_unregister_resource(oic_server.server, &oic_d_coap_resource);
        goto error;
    }

    oic_server.dtls_server = sol_coap_secure_server_new(OIC_COAP_SERVER_DTLS_PORT);
    if (!oic_server.dtls_server) {
        if (errno == ENOSYS) {
            SOL_INF("DTLS support not built in, OIC server running in insecure mode");
        } else {
            SOL_INF("DTLS server could not be created for OIC server: %s",
                sol_util_strerrora(errno));
        }
    } else {
        if (!sol_coap_server_register_resource(oic_server.dtls_server, &oic_d_coap_resource, NULL)) {
            SOL_WRN("Could not register device info secure resource, OIC server running in insecure mode");
            sol_coap_server_unref(oic_server.dtls_server);
            oic_server.dtls_server = NULL;
        }
    }

    oic_server.information = info;
    sol_vector_init(&oic_server.resources, sizeof(struct sol_oic_server_resource));

    oic_server.refcnt++;
    return 0;

error:
    free(info);
    return -1;
}

SOL_API void
sol_oic_server_release(void)
{
    struct sol_oic_server_resource *res;
    uint16_t idx;

    OIC_SERVER_CHECK();

    if (--oic_server.refcnt > 0)
        return;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&oic_server.resources, res, idx)
        sol_coap_server_unregister_resource(oic_server.server, res->coap);
    if (oic_server.dtls_server) {
        SOL_VECTOR_FOREACH_REVERSE_IDX (&oic_server.resources, res, idx)
            sol_coap_server_unregister_resource(oic_server.dtls_server, res->coap);

        sol_coap_server_unregister_resource(oic_server.dtls_server, &oic_d_coap_resource);
        sol_coap_server_unref(oic_server.dtls_server);
    }
    sol_vector_clear(&oic_server.resources);

    sol_coap_server_unregister_resource(oic_server.server, &oic_d_coap_resource);
    sol_coap_server_unregister_resource(oic_server.server, &oic_res_coap_resource);

    sol_coap_server_unref(oic_server.server);

    free(oic_server.information);
}

static int
_sol_oic_resource_type_handle(
    sol_coap_responsecode_t (*handle_fn)(const struct sol_network_link_addr *cliaddr, const void *data,
    const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output),
    struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    struct sol_oic_server_resource *res, bool expect_payload)
{
    const uint8_t format_cbor = SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
    struct sol_coap_packet *response;
    struct sol_oic_map_reader input;
    struct sol_oic_map_writer output;
    sol_coap_responsecode_t code = SOL_COAP_RSPCODE_INTERNAL_ERROR;
    CborParser parser;

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
        if (!sol_oic_pkt_has_cbor_content(req)) {
            code = SOL_COAP_RSPCODE_BAD_REQUEST;
            goto done;
        }
        if (sol_oic_packet_cbor_extract_repr_map(req, &parser, (CborValue *)&input) != CborNoError) {
            code = SOL_COAP_RSPCODE_BAD_REQUEST;
            goto done;
        }
    }

    sol_coap_add_option(response, SOL_COAP_OPTION_CONTENT_FORMAT, &format_cbor, sizeof(format_cbor));

    if (sol_oic_packet_cbor_create(response, res->href, &output) != CborNoError)
        goto done;
    code = handle_fn(cliaddr, res->callback.data, &input, &output);
    if (sol_oic_packet_cbor_close(response, &output) != CborNoError)
        code = SOL_COAP_RSPCODE_INTERNAL_ERROR;

done:
    sol_coap_header_set_type(response, SOL_COAP_TYPE_ACK);
    sol_coap_header_set_code(response, code);

    return sol_coap_send_packet(server, response, cliaddr);
}

#define DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(method, expect_payload) \
    static int \
    _sol_oic_resource_type_ ## method(struct sol_coap_server *server, \
    const struct sol_coap_resource *resource, struct sol_coap_packet *req, \
    const struct sol_network_link_addr *cliaddr, void *data) \
    { \
        struct sol_oic_server_resource *res = data; \
        return _sol_oic_resource_type_handle(res->callback.method.handle, \
            server, req, cliaddr, res, expect_payload); \
    }

DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(get, false)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(put, true)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(post, true)
DEFINE_RESOURCE_TYPE_CALLBACK_FOR_METHOD(del, true)

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

    r = asprintf(&buffer, "/sol/%x", id++);
    return r < 0 ? NULL : buffer;
}

SOL_API struct sol_oic_server_resource *
sol_oic_server_add_resource(const struct sol_oic_resource_type *rt,
    const void *handler_data, enum sol_oic_resource_flag flags)
{
    struct sol_oic_server_resource *res;

    OIC_SERVER_CHECK(NULL);
    SOL_NULL_CHECK(rt, NULL);

#ifndef SOL_NO_API_VERSION
    if (unlikely(rt->api_version != SOL_OIC_RESOURCE_TYPE_API_VERSION)) {
        SOL_WRN("Couldn't add resource_type with "
            "version '%u'. Expected version '%u'.",
            rt->api_version, SOL_OIC_RESOURCE_TYPE_API_VERSION);
        return NULL;
    }
#endif

    res = sol_vector_append(&oic_server.resources);
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

    res->href = create_endpoint();
    SOL_NULL_CHECK_GOTO(res->href, free_iface);

    res->coap = create_coap_resource(res);
    SOL_NULL_CHECK_GOTO(res->coap, free_coap);

    if (!sol_coap_server_register_resource(oic_server.server, res->coap, res))
        goto free_coap;

    if (oic_server.dtls_server) {
        if (!sol_coap_server_register_resource(oic_server.dtls_server, res->coap, res)) {
            SOL_WRN("Could not register resource in DTLS server");
            goto unregister_resource;
        }
    }

    return res;

unregister_resource:
    sol_coap_server_unregister_resource(oic_server.server, res->coap);
free_coap:
    free(res->coap);
free_iface:
    free(res->iface);
free_rt:
    free(res->rt);
remove_res:
    sol_vector_del_last(&oic_server.resources);

    return NULL;
}

SOL_API void
sol_oic_server_del_resource(struct sol_oic_server_resource *resource)
{
    struct sol_oic_server_resource *iter;
    uint16_t idx;

    OIC_SERVER_CHECK();
    SOL_NULL_CHECK(resource);

    sol_coap_server_unregister_resource(oic_server.server, resource->coap);
    if (oic_server.dtls_server)
        sol_coap_server_unregister_resource(oic_server.dtls_server, resource->coap);
    free(resource->coap);

    free(resource->href);
    free(resource->iface);
    free(resource->rt);
    SOL_VECTOR_FOREACH_REVERSE_IDX (&oic_server.resources, iter, idx) {
        if (iter == resource) {
            sol_vector_del(&oic_server.resources, idx);
            return;
        }
    }

    SOL_ERR("Could not find resource %p in OIC server resource list",
        resource);
}

static bool
send_notification_to_server(struct sol_oic_server_resource *resource,
    struct sol_coap_server *server,
    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *oic_map_writer),
    void *data)
{
    const uint8_t format_cbor = SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
    struct sol_coap_packet *pkt;
    struct sol_oic_map_writer oic_map_writer;
    uint8_t code = SOL_COAP_RSPCODE_INTERNAL_ERROR;
    CborError err;

    pkt = sol_coap_packet_notification_new(oic_server.server, resource->coap);
    SOL_NULL_CHECK(pkt, false);

    sol_coap_add_option(pkt, SOL_COAP_OPTION_CONTENT_FORMAT, &format_cbor,
        sizeof(format_cbor));

    err = sol_oic_packet_cbor_create(pkt, resource->href, &oic_map_writer);
    SOL_INT_CHECK_GOTO(err, != CborNoError, end);
    if (!fill_repr_map(data, &oic_map_writer))
        goto end;
    err = sol_oic_packet_cbor_close(pkt, &oic_map_writer);
    SOL_INT_CHECK_GOTO(err, != CborNoError, end);

    code = SOL_COAP_RSPCODE_OK;
end:
    sol_coap_header_set_code(pkt, code);
    sol_coap_header_set_type(pkt, SOL_COAP_TYPE_ACK);

    return !sol_coap_packet_send_notification(oic_server.server, resource->coap, pkt);
}

SOL_API bool
sol_oic_notify_observers(struct sol_oic_server_resource *resource,
    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *oic_map_writer),
    void *data)
{
    bool sent_server = false;
    bool sent_dtls_server = false;

    SOL_NULL_CHECK(resource, false);
    SOL_NULL_CHECK(fill_repr_map, false);

    sent_server = send_notification_to_server(resource, oic_server.server, fill_repr_map, data);
    if (oic_server.dtls_server)
        sent_dtls_server = send_notification_to_server(resource, oic_server.dtls_server, fill_repr_map, data);

    return sent_server || sent_dtls_server;
}
