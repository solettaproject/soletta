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

#include "sol-lwm2m.h"
#include "sol-lwm2m-common.h"

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
