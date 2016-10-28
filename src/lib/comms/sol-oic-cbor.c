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

#include "sol-log.h"
#include "sol-oic-cbor.h"
#include "sol-oic.h"

#define TYPICAL_OIC_PAYLOAD_SZ (64)

static inline int
buffer_used_bump(struct sol_oic_map_writer *writer, size_t inc)
{
    struct sol_buffer *buf;
    int r = sol_coap_packet_get_payload(writer->pkt, &buf, NULL);

    SOL_INT_CHECK(r, < 0, r);
    /* Ugly, but since tinycbor operates on memory slices directly, we
     * have to resort to that */
    buf->used += inc;

    return 0;
}

static CborError
initialize_cbor_payload(struct sol_oic_map_writer *encoder)
{
    int r;
    CborError err;
    size_t offset;
    struct sol_buffer *buf;
    uint8_t *old_ptr, *new_ptr;
    const uint8_t format_cbor = SOL_COAP_CONTENT_TYPE_APPLICATION_CBOR;

    r = sol_coap_add_option(encoder->pkt, SOL_COAP_OPTION_CONTENT_FORMAT,
        &format_cbor, sizeof(format_cbor));
    SOL_INT_CHECK(r, < 0, CborUnknownError);

    r = sol_coap_packet_get_payload(encoder->pkt, &buf, &offset);
    if (r < 0) {
        SOL_WRN("Could not get CoAP payload");
        return CborUnknownError;
    }

    r = sol_buffer_ensure(buf, offset + TYPICAL_OIC_PAYLOAD_SZ);
    SOL_INT_CHECK(r, < 0, CborErrorOutOfMemory);

    encoder->payload = sol_buffer_at(buf, offset);

    cbor_encoder_init(&encoder->encoder, encoder->payload,
        buf->capacity - offset, 0);
    old_ptr = encoder->encoder.data.ptr;

    encoder->type = SOL_OIC_MAP_CONTENT;

    /* With the call to sol_buffer_ensure() before, we're safe to open
     * a container */
    err = cbor_encoder_create_map(&encoder->encoder, &encoder->rep_map,
        CborIndefiniteLength);
    if (err == CborNoError) {
        new_ptr = encoder->rep_map.data.ptr;
        r = buffer_used_bump(encoder, new_ptr - old_ptr);
        if (r < 0)
            err = CborErrorUnknownType;
    }

    return err;
}

void
sol_oic_packet_cbor_create(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder)
{
    encoder->pkt = pkt;
    encoder->payload = NULL;
    encoder->type = SOL_OIC_MAP_NO_CONTENT;
}

/* Enlarge a CoAP packet's buffer and update CoAP encoder states
 * accordingly */
static inline int
enlarge_buffer(struct sol_oic_map_writer *writer,
    CborEncoder orig_encoder,
    CborEncoder orig_map,
    size_t needed)
{
    uint8_t *old_payload = writer->payload;
    struct sol_buffer *buf;
    size_t offset;
    int r;

    r = sol_coap_packet_get_payload(writer->pkt, &buf, &offset);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_ensure(buf, buf->capacity + needed);
    SOL_INT_CHECK(r, < 0, r);

    /* restore state */
    writer->encoder = orig_encoder;
    writer->rep_map = orig_map;

    /* update all encoder states */
    writer->payload = writer->encoder.data.ptr = sol_buffer_at(buf, offset);
    /* sol_buffer_at() directly to somewhere past the used portion fails*/
    writer->encoder.end = (uint8_t *)sol_buffer_at(buf, 0) + buf->capacity;

    /* new offset + how much it had progressed so far */
    writer->rep_map.data.ptr = writer->payload + (orig_map.data.ptr - old_payload);
    writer->rep_map.end = writer->encoder.end;

    return 0;
}

CborError
sol_oic_packet_cbor_close(struct sol_coap_packet *pkt,
    struct sol_oic_map_writer *writer)
{
    int r;
    uint8_t *old_ptr;
    CborError err = CborNoError;
    CborEncoder orig_encoder, orig_map;

    if (!writer->payload) {
        if (writer->type == SOL_OIC_MAP_NO_CONTENT)
            return CborNoError;
        err = initialize_cbor_payload(writer);
        SOL_INT_CHECK(err, != CborNoError, err);
    }

    while (err == CborNoError || err & CborErrorOutOfMemory) {
        orig_encoder = writer->encoder, orig_map = writer->rep_map;

        /* When you close an encoder, it will get its ptr set to the
         * topmost container's. Also, this would append one byte to the
         * payload, thus '1' below */
        old_ptr = writer->rep_map.data.ptr;
        err = cbor_encoder_close_container(&writer->encoder, &writer->rep_map);
        if (err & CborErrorOutOfMemory) {
            r = enlarge_buffer(writer, orig_encoder, orig_map, 1);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (err == CborNoError) {
            r = buffer_used_bump(writer, writer->encoder.data.ptr - old_ptr);
            if (r < 0) {
                err = CborErrorUnknownType;
                goto end;
            }
            break;
        } else
            return err;
    }

end:
    return err;
}

CborError
sol_oic_packet_cbor_append(struct sol_oic_map_writer *writer,
    struct sol_oic_repr_field *repr)
{
    CborEncoder orig_encoder, orig_map;
    CborError err = CborNoError;
    size_t next_buf_sz;
    int r;

    if (!writer->payload) {
        err = initialize_cbor_payload(writer);
        SOL_INT_CHECK(err, != CborNoError, err);
    }
    next_buf_sz = strlen(repr->key) + sizeof(uint64_t);

/* The next_buf_sz argument of enlarge_buffer() is assigned like so
 * because, according to tinycbor's code: i) all scalar types should
 * fit to (at most) that size and ii) strings and byte arrays get a
 * size sentinel, also with that maximum size, encoded together.
 */

    while (err == CborNoError || err & CborErrorOutOfMemory) {
        orig_encoder = writer->encoder, orig_map = writer->rep_map;
        err = cbor_encode_text_stringz(&writer->rep_map, repr->key);

        if (err & CborErrorOutOfMemory) {
            r = enlarge_buffer(writer, orig_encoder, orig_map, next_buf_sz);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (err == CborNoError) {
            r = buffer_used_bump(writer, writer->rep_map.data.ptr - orig_map.data.ptr);
            if (r < 0) {
                err = CborErrorUnknownType;
                goto end;
            }
            break;
        } else
            return err;
    }

    while (err == CborNoError || err & CborErrorOutOfMemory) {
        orig_encoder = writer->encoder, orig_map = writer->rep_map;

        switch (repr->type) {
        case SOL_OIC_REPR_TYPE_UINT:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_uint(&writer->rep_map, repr->v_uint);
            break;
        case SOL_OIC_REPR_TYPE_INT:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_int(&writer->rep_map, repr->v_int);
            break;
        case SOL_OIC_REPR_TYPE_SIMPLE:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_simple_value(&writer->rep_map, repr->v_simple);
            break;
        case SOL_OIC_REPR_TYPE_TEXT_STRING: {
            const char *p = repr->v_slice.data ? repr->v_slice.data : "";
            next_buf_sz = repr->v_slice.len + sizeof(uint64_t);
            err = cbor_encode_text_string(&writer->rep_map, p,
                repr->v_slice.len);
            break;
        }
        case SOL_OIC_REPR_TYPE_BYTE_STRING: {
            const uint8_t *empty = (const uint8_t *)"";
            const uint8_t *p = repr->v_slice.data ?
                (const uint8_t *)repr->v_slice.data : empty;
            next_buf_sz = repr->v_slice.len + sizeof(uint64_t);
            err = cbor_encode_byte_string(&writer->rep_map, p,
                repr->v_slice.len);
            break;
        }
        case SOL_OIC_REPR_TYPE_HALF_FLOAT:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_half_float(&writer->rep_map, repr->v_voidptr);
            break;
        case SOL_OIC_REPR_TYPE_FLOAT:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_float(&writer->rep_map, repr->v_float);
            break;
        case SOL_OIC_REPR_TYPE_DOUBLE:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_double(&writer->rep_map, repr->v_double);
            break;
        case SOL_OIC_REPR_TYPE_BOOL:
            next_buf_sz = sizeof(uint64_t);
            err = cbor_encode_boolean(&writer->rep_map, repr->v_boolean);
            break;
        default:
            err = CborErrorUnknownType;
            break;
        }

        if (err & CborErrorOutOfMemory) {
            r = enlarge_buffer(writer, orig_encoder, orig_map, next_buf_sz);
            SOL_INT_CHECK_GOTO(r, < 0, end);
        } else if (err == CborNoError) {
            r = buffer_used_bump(writer, writer->rep_map.data.ptr - orig_map.data.ptr);
            if (r < 0) {
                err = CborErrorUnknownType;
                goto end;
            }
            break;
        } else
            return err;
    }

end:

    return err;
}

CborError
sol_oic_cbor_repr_map_get_next_field(CborValue *value, struct sol_oic_repr_field *repr)
{
    CborError err;
    size_t len;

    err = cbor_value_dup_text_string(value, (char **)&repr->key, &len, NULL);
    err |= cbor_value_advance(value);

    switch (cbor_value_get_type(value)) {
    case CborIntegerType:
        err |= cbor_value_get_int64(value, &repr->v_int);
        repr->type = SOL_OIC_REPR_TYPE_INT;
        break;
    case CborTextStringType:
        err |= cbor_value_dup_text_string(value, (char **)&repr->v_slice.data, &repr->v_slice.len, NULL);
        if (err != CborNoError)
            goto harmless;
        repr->type = SOL_OIC_REPR_TYPE_TEXT_STRING;
        break;
    case CborByteStringType:
        err |= cbor_value_dup_byte_string(value, (uint8_t **)&repr->v_slice.data, &repr->v_slice.len, NULL);
        if (err != CborNoError)
            goto harmless;
        repr->type = SOL_OIC_REPR_TYPE_BYTE_STRING;
        break;
    case CborDoubleType:
        err |= cbor_value_get_double(value, &repr->v_double);
        repr->type = SOL_OIC_REPR_TYPE_DOUBLE;
        break;
    case CborFloatType:
        err |= cbor_value_get_float(value, &repr->v_float);
        repr->type = SOL_OIC_REPR_TYPE_FLOAT;
        break;
    case CborHalfFloatType:
        err |= cbor_value_get_half_float(value, &repr->v_voidptr);
        repr->type = SOL_OIC_REPR_TYPE_HALF_FLOAT;
        break;
    case CborBooleanType:
        err |= cbor_value_get_boolean(value, &repr->v_boolean);
        repr->type = SOL_OIC_REPR_TYPE_BOOL;
        break;
    default:
        SOL_WRN("While parsing representation map, got unexpected type %d",
            cbor_value_get_type(value));
        repr->type = SOL_OIC_REPR_TYPE_UNSUPPORTED;
        repr->v_int = cbor_value_get_type(value);
        break;

harmless:
        /* Initialize repr with harmless data so cleanup works. */
        repr->v_boolean = false;
        repr->type = SOL_OIC_REPR_TYPE_BOOL;
    }
    err |= cbor_value_advance(value);

    return err;
}

CborError
sol_oic_packet_cbor_extract_repr_map(struct sol_coap_packet *pkt, CborParser *parser, CborValue *repr_map)
{
    CborError err;
    size_t offset;
    struct sol_buffer *buf;

    err = sol_coap_packet_get_payload(pkt, &buf, &offset);
    SOL_INT_CHECK(err, < 0, CborErrorUnknownLength);

    err = cbor_parser_init(sol_buffer_at(buf, offset),
        buf->used - offset, 0, parser, repr_map);
    if (err != CborNoError)
        return err;

    if (!cbor_value_is_map(repr_map))
        return CborErrorIllegalType;

    return CborNoError;
}

bool
sol_oic_pkt_has_cbor_content(const struct sol_coap_packet *pkt)
{
    const uint8_t *ptr;
    uint16_t len;

    ptr = sol_coap_find_first_option(pkt, SOL_COAP_OPTION_CONTENT_FORMAT, &len);

    return ptr && len == 1 && *ptr == SOL_COAP_CONTENT_TYPE_APPLICATION_CBOR;
}

int
sol_cbor_array_to_vector(CborValue *array, struct sol_vector *vector)
{
    CborError err;
    CborValue iter;

    for (err = cbor_value_enter_container(array, &iter);
        cbor_value_is_text_string(&iter) && err == CborNoError;
        err |= cbor_value_advance(&iter)) {
        struct sol_str_slice *slice = sol_vector_append(vector);

        if (!slice)
            return -ENOMEM;

        err |= cbor_value_dup_text_string(&iter, (char **)&slice->data, &slice->len, NULL);
    }

    err |= cbor_value_leave_container(array, &iter);
    return err == CborNoError ? 0 : -EIO;
}

int
sol_cbor_map_get_array(const CborValue *map, const char *key,
    struct sol_vector *vector)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return -ENOENT;

    if (!cbor_value_is_array(&value))
        return -ENOENT;

    return sol_cbor_array_to_vector(&value, vector);
}

int
sol_cbor_map_get_str_value(const CborValue *map, const char *key,
    struct sol_str_slice *slice)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return -ENOENT;

    if (!cbor_value_is_text_string(&value))
        return -ENOENT;

    return cbor_value_dup_text_string(&value, (char **)&slice->data, &slice->len, NULL) == CborNoError ? 0 : -ENOMEM;
}

int
sol_cbor_bsv_to_vector(const CborValue *value, char **data, struct sol_vector *vector)
{
    size_t len;
    char *last, *p;

    if (*data) {
        free(*data);
        *data = NULL;
    }
    sol_vector_clear(vector);
    if (cbor_value_dup_text_string(value, data, &len, NULL) != CborNoError)
        return -ENOMEM;

    for (last = *data; last != NULL;) {
        size_t cur_len;

        p = strchr(last, ' ');
        if (p == NULL)
            cur_len = len;
        else {
            cur_len = p - last;
            p++;
            len--;
        }

        if (cur_len > 0) {
            struct sol_str_slice *slice = sol_vector_append(vector);
            if (!slice)
                goto error;
            *slice = SOL_STR_SLICE_STR(last, cur_len);
        }
        last = p;
        len -= cur_len;
    }

    return 0;

error:
    free(*data);
    *data = NULL;
    sol_vector_clear(vector);
    return -ENOMEM;
}

int
sol_cbor_map_get_bsv(const CborValue *map, const char *key,
    char **data, struct sol_vector *vector)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return -ENOENT;

    if (!cbor_value_is_text_string(&value))
        return -ENOENT;

    return sol_cbor_bsv_to_vector(&value, data, vector);
}

int
sol_cbor_map_get_bytestr_value(const CborValue *map, const char *key,
    struct sol_str_slice *slice)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return -ENOENT;

    return cbor_value_dup_byte_string(&value, (uint8_t **)&slice->data, &slice->len, NULL) == CborNoError ? 0 : -EIO;
}

void
sol_cbor_map_get_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type *type)
{
    *type = oic_map_writer->type;
}

int
sol_cbor_map_set_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type type)
{
    if (oic_map_writer->type == type)
        return 0;

    if (oic_map_writer->payload) {
        SOL_WRN("Payload was already created. Impossible to change its type");
        return -EPERM;
    }

    oic_map_writer->type = type;
    return 0;

}
