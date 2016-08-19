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
#include <float.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

#define SOL_LOG_DOMAIN &_lwm2m_common_domain

#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-list.h"
#include "sol-lwm2m.h"
#include "sol-lwm2m-common.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-monitors.h"
#include "sol-random.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-http.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_lwm2m_common_domain, "lwm2m-common");

int sol_lwm2m_common_init(void);
void sol_lwm2m_common_shutdown(void);

bool
sec_mode_is_repeated(enum sol_lwm2m_security_mode new_sec_mode,
    enum sol_lwm2m_security_mode *sec_modes, uint16_t sec_modes_len)
{
    uint16_t i;

    for (i = 0; i < sec_modes_len; i++)
        if (sec_modes[i] == new_sec_mode)
            return true;

    return false;
}

const char *
get_security_mode_str(enum sol_lwm2m_security_mode sec_mode)
{
    switch (sec_mode) {
    case SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY:
        return "Pre-Shared Key";
    case SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY:
        return "Raw Public Key";
    case SOL_LWM2M_SECURITY_MODE_CERTIFICATE:
        return "Certificate";
    case SOL_LWM2M_SECURITY_MODE_NO_SEC:
        return "NoSec";
    default:
        return "Unknown";
    }
}

int
read_resources(struct sol_lwm2m_client *client,
    struct obj_ctx *obj_ctx, struct obj_instance *instance,
    struct sol_lwm2m_resource *res, size_t res_len, ...)
{
    size_t i;
    int r = 0;
    va_list ap;

    SOL_NULL_CHECK(obj_ctx->obj->read, -ENOTSUP);

    va_start(ap, res_len);

    // The va_list contains the resources IDs that should be read.
    for (i = 0; i < res_len; i++) {
        r = obj_ctx->obj->read((void *)instance->data,
            (void *)client->user_data, client, instance->id,
            (uint16_t)va_arg(ap, int), &res[i]);

        if (r == -ENOENT) {
            res[i].data_len = 0;
            res[i].data = NULL;
            continue;
        }

        LWM2M_RESOURCE_CHECK_API_GOTO(res[i], err_exit_api);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    va_end(ap);
    return 0;

#ifndef SOL_NO_API_VERSION
err_exit_api:
    r = -EINVAL;
#endif
err_exit:
    clear_resource_array(res, i);
    va_end(ap);
    return r;
}

struct obj_ctx *
find_object_ctx_by_id(struct sol_lwm2m_client *client, uint16_t id)
{
    uint16_t i;
    struct obj_ctx *ctx;

    SOL_VECTOR_FOREACH_IDX (&client->objects, ctx, i) {
        if (ctx->obj->id == id)
            return ctx;
    }

    return NULL;
}

void
clear_resource_array(struct sol_lwm2m_resource *array, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++)
        sol_lwm2m_resource_clear(&array[i]);
}

int
get_server_id_by_link_addr(const struct sol_ptr_vector *connections,
    const struct sol_network_link_addr *cliaddr, int64_t *server_id)
{
    struct server_conn_ctx *conn_ctx;
    struct sol_network_link_addr *server_addr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (connections, conn_ctx, i) {
        server_addr = sol_vector_get_no_check(&conn_ctx->server_addr_list, conn_ctx->addr_list_idx);
        if (sol_network_link_addr_eq_full(cliaddr, server_addr, true)) {
            if (conn_ctx->server_id == DEFAULT_SHORT_SERVER_ID)
                *server_id = UINT16_MAX;
            else
                *server_id = conn_ctx->server_id;
            return 0;
        }
    }

    return -ENOENT;
}

void
send_ack_if_needed(struct sol_coap_server *coap, struct sol_coap_packet *msg,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_coap_packet *ack;
    uint8_t type;

    sol_coap_header_get_type(msg, &type);

    if (type == SOL_COAP_MESSAGE_TYPE_CON) {
        ack = sol_coap_packet_new(msg);
        SOL_NULL_CHECK(ack);
        if (sol_coap_send_packet(coap, ack, cliaddr) < 0)
            SOL_WRN("Could not send the response ACK");
    }
}

enum sol_lwm2m_binding_mode
get_binding_mode_from_str(const struct sol_str_slice binding)
{
    static const struct sol_str_table map[] = {
        SOL_STR_TABLE_PTR_ITEM("U", SOL_LWM2M_BINDING_MODE_U),
        //TODO: The modes below are not supported for now.
        SOL_STR_TABLE_PTR_ITEM("UQ", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("S", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("SQ", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("US", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        SOL_STR_TABLE_PTR_ITEM("UQS", SOL_LWM2M_BINDING_MODE_UNKNOWN),
        { }
    };

    return sol_str_table_lookup_fallback(map, binding,
        SOL_LWM2M_BINDING_MODE_UNKNOWN);
}

void
client_objects_clear(struct sol_ptr_vector *objects)
{
    uint16_t i, j, *id;
    struct sol_lwm2m_client_object *object;

    SOL_PTR_VECTOR_FOREACH_IDX (objects, object, i) {
        SOL_PTR_VECTOR_FOREACH_IDX (&object->instances, id, j)
            free(id);
        sol_ptr_vector_clear(&object->instances);
        free(object);
    }

    sol_ptr_vector_clear(objects);
}

struct sol_lwm2m_client_object *
find_client_object_by_id(struct sol_ptr_vector *objects,
    uint16_t id)
{
    uint16_t i;
    struct sol_lwm2m_client_object *cobject;

    SOL_PTR_VECTOR_FOREACH_IDX (objects, cobject, i) {
        if (cobject->id == id)
            return cobject;
    }

    return NULL;
}

int
add_to_monitors(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data)
{
    struct sol_monitors_entry *m;

    SOL_NULL_CHECK(cb, -EINVAL);

    m = sol_monitors_append(monitors, cb, data);
    SOL_NULL_CHECK(m, -ENOMEM);

    return 0;
}

int
remove_from_monitors(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data)
{
    int i;

    SOL_NULL_CHECK(cb, -EINVAL);

    i = sol_monitors_find(monitors, cb, data);
    SOL_INT_CHECK(i, < 0, i);

    return sol_monitors_del(monitors, i);
}

size_t
get_int_size(int64_t i)
{
    if (i >= INT8_MIN && i <= INT8_MAX)
        return 1;
    if (i >= INT16_MIN && i <= INT16_MAX)
        return 2;
    if (i >= INT32_MIN && i <= INT32_MAX)
        return 4;
    return 8;
}

int
get_resource_len(const struct sol_lwm2m_resource *resource, uint16_t index,
    size_t *len)
{
    switch (resource->data_type) {
    case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        *len = resource->data[index].content.blob->size;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
        *len = get_int_size(resource->data[index].content.integer);
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL:
        *len = 1;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
        *len = 8;
        return 0;
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
        *len = OBJ_LINK_LEN;
        return 0;
    default:
        return -EINVAL;
    }
}

void
swap_bytes(uint8_t *to_swap, size_t len)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return;
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t swap;
    size_t i, j, stop;

    stop = len / 2;

    for (i = 0, j = len - 1; i != stop; i++, j--) {
        swap = to_swap[i];
        to_swap[i] = to_swap[j];
        to_swap[j] = swap;
    }
#else
#error "Unknown byte order"
#endif
}

int
add_float_resource(struct sol_buffer *buf, double fp, size_t len)
{
    uint8_t *bytes = NULL;
    float f;
    double d;

    if (len == 4) {
        f = (float)fp;
        swap_bytes((uint8_t *)&f, len);
        bytes = (uint8_t *)&f;
    } else {
        d = fp;
        swap_bytes((uint8_t *)&d, len);
        bytes = (uint8_t *)&d;
    }

    return sol_buffer_append_bytes(buf, bytes, len);
}

int
add_int_resource(struct sol_buffer *buf, int64_t i, size_t len)
{
    swap_bytes((uint8_t *)&i, len);
    return sol_buffer_append_bytes(buf, (uint8_t *)&i, len);
}

int
add_resource_bytes_to_buffer(const struct sol_lwm2m_resource *resource,
    struct sol_buffer *buf, uint16_t idx)
{
    int r;
    uint8_t b;
    size_t len;

    r = get_resource_len(resource, idx, &len);
    SOL_INT_CHECK(r, < 0, r);

    switch (resource->data_type) {
    case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        return sol_buffer_append_slice(buf, sol_str_slice_from_blob(resource->data[idx].content.blob));
    case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
    case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
        return add_int_resource(buf, resource->data[idx].content.integer, len);
    case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL:
        b = resource->data[idx].content.integer != 0 ? 1 : 0;
        return sol_buffer_append_bytes(buf, (uint8_t *)&b, 1);
    case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
        return add_float_resource(buf, resource->data[idx].content.fp, len);
    default:
        return -EINVAL;
    }
}

int
set_packet_payload(struct sol_coap_packet *pkt,
    const uint8_t *data, uint16_t len)
{
    struct sol_buffer *buf;
    int r;

    r = sol_coap_packet_get_payload(pkt, &buf, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return sol_buffer_append_bytes(buf, data, len);
}

int
setup_tlv_header(enum sol_lwm2m_tlv_type tlv_type, uint16_t res_id,
    struct sol_buffer *buf, size_t data_len)
{
    int r;
    uint8_t tlv_data[6];
    size_t tlv_data_len;

    tlv_data_len = 2;

    tlv_data[0] = tlv_type;

    if (res_id > UINT8_MAX) {
        tlv_data[0] |= ID_HAS_16BITS_MASK;
        tlv_data[1] = (res_id >> 8) & 255;
        tlv_data[2] = res_id & 255;
        tlv_data_len++;
    } else
        tlv_data[1] = res_id;

    if (data_len <= 7)
        tlv_data[0] |= data_len;
    else if (data_len <= UINT8_MAX) {
        tlv_data[tlv_data_len++] = data_len;
        tlv_data[0] |= LEN_IS_8BITS_MASK;
    } else if (data_len <= UINT16_MAX) {
        tlv_data[tlv_data_len++] = (data_len >> 8) & 255;
        tlv_data[tlv_data_len++] = data_len & 255;
        tlv_data[0] |= LEN_IS_16BITS_MASK;
    } else if (data_len <= UINT24_MAX) {
        tlv_data[tlv_data_len++] = (data_len >> 16) & 255;
        tlv_data[tlv_data_len++] = (data_len >> 8) & 255;
        tlv_data[tlv_data_len++] = data_len & 255;
        tlv_data[0] |= LEN_IS_24BITS_MASK;
    } else
        return -ENOMEM;

    r = sol_buffer_append_bytes(buf, tlv_data, tlv_data_len);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

int
setup_tlv(struct sol_lwm2m_resource *resource, struct sol_buffer *buf)
{
    int r;
    size_t data_len, len;
    uint16_t i;
    enum sol_lwm2m_tlv_type type;

    LWM2M_RESOURCE_CHECK_API(resource, -EINVAL);

    for (i = 0, data_len = 0; i < resource->data_len; i++) {
        r = get_resource_len(resource, i, &len);
        SOL_INT_CHECK(r, < 0, r);
        data_len += len;
    }

    switch (resource->type) {
    case SOL_LWM2M_RESOURCE_TYPE_SINGLE:
        type = SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE;
        break;
    case SOL_LWM2M_RESOURCE_TYPE_MULTIPLE:
        type = SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES;
        data_len += (resource->data_len * 2);
        break;
    default:
        SOL_WRN("Unknown resource type '%d'", (int)resource->type);
        return -EINVAL;
    }

    r = setup_tlv_header(type, resource->id, buf, data_len);
    SOL_INT_CHECK(r, < 0, r);

    if (type == SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE)
        return add_resource_bytes_to_buffer(resource, buf, 0);

    for (i = 0; i < resource->data_len; i++) {
        r = get_resource_len(resource, i, &data_len);
        SOL_INT_CHECK(r, < 0, r);
        r = setup_tlv_header(SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE, resource->data[i].id,
            buf, data_len);
        SOL_INT_CHECK(r, < 0, r);
        r = add_resource_bytes_to_buffer(resource, buf, i);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

int
resources_to_tlv(struct sol_lwm2m_resource *resources,
    size_t len, struct sol_buffer *tlvs)
{
    int r;
    size_t i;

    for (i = 0; i < len; i++) {
        r = setup_tlv(&resources[i], tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    return 0;

exit:
    return r;
}

int
instances_to_tlv(struct sol_lwm2m_resource **instances,
    size_t *instances_len, uint16_t *instances_ids, size_t len, struct sol_buffer *tlvs)
{
    int r;
    size_t i, j, res, instance_data_len, data_len;

    for (i = 0; i < len; i++) {
        for (res = 0, instance_data_len = 0; res < instances_len[i]; res++)
            for (j = 0; j < instances[i][res].data_len; j++) {
                r = get_resource_len(&instances[i][res], j, &data_len);
                SOL_INT_CHECK(r, < 0, r);

                instance_data_len += data_len;
            }

        r = setup_tlv_header(SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE,
            instances_ids[i], tlvs, instance_data_len);
        SOL_INT_CHECK(r, < 0, r);

        r = resources_to_tlv(instances[i], instances_len[i], tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    return 0;

exit:
    return r;
}

int
add_coap_int_option(struct sol_coap_packet *pkt,
    enum sol_coap_option opt, const void *data, uint16_t len)
{
    uint8_t buf[sizeof(int64_t)] = { };

    memcpy(buf, data, len);
    swap_bytes(buf, len);
    return sol_coap_add_option(pkt, opt, buf, len);
}

int
get_coap_int_option(struct sol_coap_packet *pkt,
    enum sol_coap_option opt, uint16_t *value)
{
    const void *v;
    uint16_t len;

    v = sol_coap_find_first_option(pkt, opt, &len);

    if (!v)
        return -ENOENT;

    memcpy(value, v, len);
    swap_bytes((uint8_t *)value, len);
    return 0;
}

int
setup_coap_packet(enum sol_coap_method method,
    enum sol_coap_message_type type, const char *objects_path, const char *path,
    uint8_t *obs, int64_t *token, struct sol_lwm2m_resource *resources,
    struct sol_lwm2m_resource **instances, size_t *instances_len,
    uint16_t *instances_ids, size_t len,
    const char *execute_args,
    struct sol_coap_packet **pkt)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_buffer tlvs =
        SOL_BUFFER_INIT_FLAGS(NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    struct sol_random *random;
    uint16_t content_type, content_len = 0;
    const uint8_t *content_data;
    int64_t t;
    int r;

    random = sol_random_new(SOL_RANDOM_DEFAULT, 0);
    SOL_NULL_CHECK(random, -ENOMEM);

    *pkt = sol_coap_packet_new_request(method, type);
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(*pkt, exit);

    r = sol_random_get_int64(random, &t);
    if (r < 0) {
        SOL_WRN("Could not generate a random number");
        goto exit;
    }

    r = sol_coap_header_set_token(*pkt, (uint8_t *)&t,
        (uint8_t)sizeof(int64_t));
    if (r < 0) {
        SOL_WRN("Could not set the token");
        goto exit;
    }
    SOL_DBG("Setting token as %" PRId64 ", len = %zu", t, sizeof(int64_t));

    if (token)
        *token = t;

    if (obs) {
        r = add_coap_int_option(*pkt, SOL_COAP_OPTION_OBSERVE, obs,
            sizeof(uint8_t));
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    if (objects_path) {
        r = sol_buffer_append_slice(&buf,
            sol_str_slice_from_str(objects_path));
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sol_buffer_append_slice(&buf, sol_str_slice_from_str(path));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (strcmp(path, "/") != 0) {
        r = sol_coap_packet_add_uri_path_option(*pkt, buf.data);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    if (execute_args) {
        size_t str_len;
        content_type = SOL_LWM2M_CONTENT_TYPE_TEXT;
        content_data = (const uint8_t *)execute_args;
        str_len = strlen(execute_args);
        r = -ENOMEM;
        SOL_INT_CHECK_GOTO(str_len, >= UINT16_MAX, exit);
        content_len = str_len;
    } else if (resources) {
        content_type = SOL_LWM2M_CONTENT_TYPE_TLV;
        r = resources_to_tlv(resources, len, &tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        r = -ENOMEM;
        SOL_INT_CHECK_GOTO(tlvs.used, >= UINT16_MAX, exit);
        content_data = tlvs.data;
        content_len = tlvs.used;
    } else if (instances) {
        content_type = SOL_LWM2M_CONTENT_TYPE_TLV;
        r = instances_to_tlv(instances, instances_len, instances_ids, len, &tlvs);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        r = -ENOMEM;
        SOL_INT_CHECK_GOTO(tlvs.used, >= UINT16_MAX, exit);
        content_data = tlvs.data;
        content_len = tlvs.used;
    }

    if (content_len > 0) {
        r = add_coap_int_option(*pkt, SOL_COAP_OPTION_CONTENT_FORMAT,
            &content_type, sizeof(content_type));
        SOL_INT_CHECK_GOTO(r, < 0, exit);

        r = set_packet_payload(*pkt, content_data, content_len);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = 0;

exit:
    if (r < 0)
        sol_coap_packet_unref(*pkt);
    sol_buffer_fini(&tlvs);
    sol_buffer_fini(&buf);
    sol_random_del(random);
    return r;
}

void
tlv_clear(struct sol_lwm2m_tlv *tlv)
{
    LWM2M_TLV_CHECK_API(tlv);
    sol_buffer_fini(&tlv->content);
}

int
is_resource(struct sol_lwm2m_tlv *tlv)
{
    if (tlv->type != SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE &&
        tlv->type != SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE)
        return -EINVAL;
    return 0;
}

SOL_API int
sol_lwm2m_client_object_get_id(const struct sol_lwm2m_client_object *object,
    uint16_t *id)
{
    SOL_NULL_CHECK(object, -EINVAL);
    SOL_NULL_CHECK(id, -EINVAL);

    *id = object->id;
    return 0;
}

SOL_API const struct sol_ptr_vector *
sol_lwm2m_client_object_get_instances(
    const struct sol_lwm2m_client_object *object)
{
    SOL_NULL_CHECK(object, NULL);

    return &object->instances;
}

SOL_API int
sol_lwm2m_resource_init(struct sol_lwm2m_resource *resource,
    uint16_t id, enum sol_lwm2m_resource_type type, uint16_t resource_len,
    enum sol_lwm2m_resource_data_type data_type, ...)
{
    uint16_t i;
    va_list ap;
    struct sol_blob *blob;
    int r = -EINVAL;

    SOL_NULL_CHECK(resource, -EINVAL);
    SOL_INT_CHECK(data_type, == SOL_LWM2M_RESOURCE_DATA_TYPE_NONE, -EINVAL);
    SOL_INT_CHECK(resource_len, <= 0, -EINVAL);

    LWM2M_RESOURCE_CHECK_API(resource, -EINVAL);

    resource->id = id;
    resource->type = type;
    resource->data_type = data_type;
    resource->data = calloc(resource_len, sizeof(struct sol_lwm2m_resource_data));
    SOL_NULL_CHECK(resource->data, -ENOMEM);
    resource->data_len = resource_len;

    va_start(ap, data_type);

    for (i = 0; i < resource_len; i++) {
        if (resource->type == SOL_LWM2M_RESOURCE_TYPE_MULTIPLE)
            resource->data[i].id = va_arg(ap, int);

        switch (resource->data_type) {
        case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
            blob = va_arg(ap, struct sol_blob *);
            SOL_NULL_CHECK_GOTO(blob, err_exit);
            resource->data[i].content.blob = sol_blob_ref(blob);
            SOL_NULL_CHECK_GOTO(resource->data[i].content.blob, err_ref);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
            resource->data[i].content.fp = va_arg(ap, double);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
        case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
            resource->data[i].content.integer = va_arg(ap, int64_t);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL:
            resource->data[i].content.integer = va_arg(ap, int);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
            resource->data[i].content.integer = (uint16_t)va_arg(ap, int);
            resource->data[i].content.integer = (resource->data[i].content.integer << 16) |
                (uint16_t)va_arg(ap, int);
            break;
        default:
            SOL_WRN("Unknown resource data type");
            goto err_exit;
        }
    }

    va_end(ap);
    return 0;

err_ref:
    r = -EOVERFLOW;
err_exit:
    if (data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE ||
        data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_STRING) {
        uint16_t until = i;

        for (i = 0; i < until; i++)
            sol_blob_unref(resource->data[i].content.blob);
    }
    free(resource->data);
    va_end(ap);
    return r;
}

SOL_API int
sol_lwm2m_resource_init_vector(struct sol_lwm2m_resource *resource,
    uint16_t id, enum sol_lwm2m_resource_data_type data_type,
    struct sol_vector *res_instances)
{
    uint16_t i;
    int r = -EINVAL;

    SOL_NULL_CHECK(resource, -EINVAL);
    SOL_INT_CHECK(data_type, == SOL_LWM2M_RESOURCE_DATA_TYPE_NONE, -EINVAL);
    SOL_NULL_CHECK(res_instances, -EINVAL);
    SOL_INT_CHECK(res_instances->len, <= 0, -EINVAL);

    LWM2M_RESOURCE_CHECK_API(resource, -EINVAL);

    resource->id = id;
    resource->type = SOL_LWM2M_RESOURCE_TYPE_MULTIPLE;
    resource->data_type = data_type;
    resource->data = calloc(res_instances->len, sizeof(struct sol_lwm2m_resource_data));
    SOL_NULL_CHECK(resource->data, -ENOMEM);
    resource->data_len = res_instances->len;

    for (i = 0; i < res_instances->len; i++) {
        void *v = sol_vector_get_no_check(res_instances, i);
        struct sol_lwm2m_resource_data *res_data = v;
        resource->data[i].id = res_data->id;

        if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE ||
            resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_STRING) {
            struct sol_blob *blob = res_data->content.blob;
            SOL_NULL_CHECK_GOTO(blob, err_exit);
            resource->data[i].content.blob = sol_blob_ref(blob);
            SOL_NULL_CHECK_GOTO(resource->data[i].content.blob, err_ref);

        } else if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT) {
            resource->data[i].content.fp = res_data->content.fp;

        } else if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_INT ||
            resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_TIME) {
            resource->data[i].content.integer = res_data->content.integer;

        } else if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL) {
            resource->data[i].content.integer = res_data->content.integer;

        } else if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK) {
            resource->data[i].content.integer = (uint16_t)res_data->content.integer;
            resource->data[i].content.integer = (resource->data[i].content.integer << 16) |
                (uint16_t)res_data->content.integer;

        } else {
            SOL_WRN("Unknown resource data type");
            goto err_exit;
        }
    }

    return 0;

err_ref:
    r = -EOVERFLOW;
err_exit:
    if (data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE ||
        data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_STRING) {
        uint16_t until = i;

        for (i = 0; i < until; i++)
            sol_blob_unref(resource->data[i].content.blob);
    }
    free(resource->data);
    return r;
}

SOL_API void
sol_lwm2m_tlv_clear(struct sol_lwm2m_tlv *tlv)
{
    SOL_NULL_CHECK(tlv);
    tlv_clear(tlv);
}

SOL_API void
sol_lwm2m_tlv_list_clear(struct sol_vector *tlvs)
{
    uint16_t i;
    struct sol_lwm2m_tlv *tlv;

    SOL_NULL_CHECK(tlvs);

    SOL_VECTOR_FOREACH_IDX (tlvs, tlv, i)
        tlv_clear(tlv);
    sol_vector_clear(tlvs);
}

SOL_API int
sol_lwm2m_parse_tlv(const struct sol_str_slice content, struct sol_vector *out)
{
    size_t i, offset;
    struct sol_lwm2m_tlv *tlv;
    int r;

    SOL_NULL_CHECK(out, -EINVAL);

    sol_vector_init(out, sizeof(struct sol_lwm2m_tlv));

    for (i = 0; i < content.len;) {
        struct sol_str_slice tlv_content;
        tlv = sol_vector_append(out);
        r = -ENOMEM;
        SOL_NULL_CHECK_GOTO(tlv, err_exit);

        sol_buffer_init(&tlv->content);

        SOL_SET_API_VERSION(tlv->api_version = SOL_LWM2M_TLV_API_VERSION; )

        tlv->type = content.data[i] & TLV_TYPE_MASK;

        if ((content.data[i] & TLV_ID_SIZE_MASK) != TLV_ID_SIZE_MASK) {
            tlv->id = content.data[i + 1];
            offset = i + 2;
        } else {
            tlv->id = (content.data[i + 1] << 8) | content.data[i + 2];
            offset = i + 3;
        }

        SOL_INT_CHECK_GOTO(offset, >= content.len, err_would_overflow);

        switch (content.data[i] & TLV_CONTENT_LENGTH_MASK) {
        case LENGTH_SIZE_24_BITS:
            tlv_content.len = (content.data[offset] << 16) |
                (content.data[offset + 1] << 8) | content.data[offset + 2];
            offset += 3;
            break;
        case LENGTH_SIZE_16_BITS:
            tlv_content.len = (content.data[offset] << 8) |
                content.data[offset + 1];
            offset += 2;
            break;
        case LENGTH_SIZE_8_BITS:
            tlv_content.len = content.data[offset];
            offset++;
            break;
        default:
            tlv_content.len = content.data[i] & TLV_CONTENT_LENGHT_CUSTOM_MASK;
        }

        SOL_INT_CHECK_GOTO(offset, >= content.len, err_would_overflow);

        tlv_content.data = content.data + offset;

        r = sol_buffer_append_slice(&tlv->content, tlv_content);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        SOL_DBG("tlv type: %u, ID: %" PRIu16 ", Size: %zu, Content: %.*s",
            tlv->type, tlv->id, tlv_content.len,
            SOL_STR_SLICE_PRINT(tlv_content));

        if (tlv->type != SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES &&
            tlv->type != SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE)
            i += ((offset - i) + tlv_content.len);
        else
            i += (offset - i);
    }

    return 0;

err_would_overflow:
    r = -EOVERFLOW;
err_exit:
    sol_lwm2m_tlv_list_clear(out);
    return r;
}

SOL_API int
sol_lwm2m_tlv_get_int(struct sol_lwm2m_tlv *tlv, int64_t *value)
{
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);

#define TO_LOCAL_INT_VALUE(_network_int, _local_int, _out) \
    memcpy(&(_local_int), _network_int, sizeof((_local_int))); \
    swap_bytes((uint8_t *)&(_local_int), sizeof((_local_int))); \
    *(_out) = _local_int;

    switch (tlv->content.used) {
    case 1:
        TO_LOCAL_INT_VALUE(tlv->content.data, i8, value);
        break;
    case 2:
        TO_LOCAL_INT_VALUE(tlv->content.data, i16, value);
        break;
    case 4:
        TO_LOCAL_INT_VALUE(tlv->content.data, i32, value);
        break;
    case 8:
        TO_LOCAL_INT_VALUE(tlv->content.data, i64, value);
        break;
    default:
        SOL_WRN("Invalid int size: %zu", tlv->content.used);
        return -EINVAL;
    }

    SOL_DBG("TLV has integer data. Value: %" PRId64 "", *value);
    return 0;

#undef TO_LOCAL_INT_VALUE
}

SOL_API int
sol_lwm2m_tlv_get_bool(struct sol_lwm2m_tlv *tlv, bool *value)
{
    char v;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);
    SOL_INT_CHECK(tlv->content.used, != 1, -EINVAL);

    v = (((char *)tlv->content.data))[0];

    if (v != 0 && v != 1) {
        SOL_WRN("The TLV value is not '0' or '1'. Actual value:%d", v);
        return -EINVAL;
    }

    *value = (bool)v;
    SOL_DBG("TLV data as bool: %d", (int)*value);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_float(struct sol_lwm2m_tlv *tlv, double *value)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);

    if (tlv->content.used == 4) {
        float f;
        memcpy(&f, tlv->content.data, sizeof(float));
        swap_bytes((uint8_t *)&f, sizeof(float));
        *value = f;
    } else if (tlv->content.used == 8) {
        memcpy(value, tlv->content.data, sizeof(double));
        swap_bytes((uint8_t *)value, sizeof(double));
    } else
        return -EINVAL;

    SOL_DBG("TLV has float data. Value: %g", *value);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_obj_link(struct sol_lwm2m_tlv *tlv,
    uint16_t *object_id, uint16_t *instance_id)
{
    int32_t i = 0;

    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(object_id, -EINVAL);
    SOL_NULL_CHECK(instance_id, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);
    SOL_INT_CHECK(tlv->content.used, != OBJ_LINK_LEN, -EINVAL);


    memcpy(&i, tlv->content.data, OBJ_LINK_LEN);
    swap_bytes((uint8_t *)&i, OBJ_LINK_LEN);
    *object_id = (i >> 16) & 0xFFFF;
    *instance_id = i & 0xFFFF;

    SOL_DBG("TLV has object link value. Object id:%" PRIu16
        "  Instance id:%" PRIu16 "", *object_id, *instance_id);
    return 0;
}

SOL_API int
sol_lwm2m_tlv_get_bytes(struct sol_lwm2m_tlv *tlv, struct sol_buffer *buf)
{
    SOL_NULL_CHECK(tlv, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(is_resource(tlv), < 0, -EINVAL);
    LWM2M_TLV_CHECK_API(tlv, -EINVAL);

    return sol_buffer_append_bytes(buf, (uint8_t *)tlv->content.data, tlv->content.used);
}

SOL_API void
sol_lwm2m_resource_clear(struct sol_lwm2m_resource *resource)
{
    uint16_t i;

    SOL_NULL_CHECK(resource);
    LWM2M_RESOURCE_CHECK_API(resource);

    if (resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE ||
        resource->data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_STRING) {
        for (i = 0; i < resource->data_len; i++)
            sol_blob_unref(resource->data[i].content.blob);
    }
    free(resource->data);
    resource->data = NULL;
}

int
sol_lwm2m_common_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    return 0;
}

void
sol_lwm2m_common_shutdown(void)
{
}

enum sol_lwm2m_path_props
sol_lwm2m_common_get_path_props(const char *path)
{
    size_t i, slashes;
    enum sol_lwm2m_path_props props = PATH_IS_INVALID_OR_EMPTY;

    for (i = 0, slashes = 0; path[i]; i++) {
        if (path[i] == '/') {
            props =  props << 1;
            slashes++;
            if (slashes > 3) {
                SOL_WRN("The path '%s' has an invalid format."
                    " Expected: /Object/Instance/Resource", path);
                return PATH_IS_INVALID_OR_EMPTY;
            }
        } else if (!isdigit(path[i])) {
            SOL_WRN("The path '%s' contains a nondigit character: '%c'",
                path, path[i]);
            return PATH_IS_INVALID_OR_EMPTY;
        }
    }

    if (i == slashes) {
        SOL_DBG("Path '%s' is empty\n", path);
        return PATH_IS_INVALID_OR_EMPTY;
    }

    return props;
}
