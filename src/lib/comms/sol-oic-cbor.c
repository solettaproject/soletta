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

#include "coap.h"
#include "sol-log.h"
#include "sol-oic-cbor.h"
#include "sol-oic-common.h"

static CborError
initialize_cbor_payload(struct sol_oic_map_writer *encoder)
{
    int r;
    size_t offset;
    struct sol_buffer *buf;
    const uint8_t format_cbor = SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;

    r = sol_coap_add_option(encoder->pkt, SOL_COAP_OPTION_CONTENT_FORMAT,
        &format_cbor, sizeof(format_cbor));
    SOL_INT_CHECK(r, < 0, CborUnknownError);

    r = sol_coap_packet_get_payload(encoder->pkt, &buf, &offset);
    if (r < 0) {
        SOL_WRN("Could not get CoAP payload");
        return CborUnknownError;
    }

    /* FIXME: Because we can't now make phony cbor calls to calculate
     * the exact payload in the contexts this call is issued (they
     * involve user callbacks to append cbor data), we ensure this
     * hardcoded size */
    r = sol_buffer_ensure(buf, COAP_UDP_MTU - offset);
    SOL_INT_CHECK(r, < 0, CborErrorOutOfMemory);

    encoder->payload = sol_buffer_at(buf, offset);

    cbor_encoder_init(&encoder->encoder, encoder->payload,
        COAP_UDP_MTU - offset, 0);

    encoder->type = SOL_OIC_MAP_CONTENT;

    return cbor_encoder_create_map(&encoder->encoder, &encoder->rep_map,
        CborIndefiniteLength);
}

void
sol_oic_packet_cbor_create(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder)
{
    encoder->pkt = pkt;
    encoder->payload = NULL;
    encoder->type = SOL_OIC_MAP_NO_CONTENT;
}

CborError
sol_oic_packet_cbor_close(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder)
{
    CborError err;

    if (!encoder->payload) {
        if (encoder->type == SOL_OIC_MAP_NO_CONTENT)
            return CborNoError;
        err = initialize_cbor_payload(encoder);
        SOL_INT_CHECK(err, != CborNoError, err);
    }

    err = cbor_encoder_close_container(&encoder->encoder, &encoder->rep_map);
    if (err == CborNoError) {
        /* Ugly, but since tinycbor operates on memory slices
         * directly, we have to resort to that */
        struct sol_buffer *buf;
        int r = sol_coap_packet_get_payload(pkt, &buf, NULL);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        buf->used += encoder->encoder.ptr - encoder->payload;
    }

error:
    return err;
}

CborError
sol_oic_packet_cbor_append(struct sol_oic_map_writer *encoder, struct sol_oic_repr_field *repr)
{
    CborError err;

    if (!encoder->payload) {
        err = initialize_cbor_payload(encoder);
        SOL_INT_CHECK(err, != CborNoError, err);
    }

    err = cbor_encode_text_stringz(&encoder->rep_map, repr->key);
    switch (repr->type) {
    case SOL_OIC_REPR_TYPE_UINT:
        err |= cbor_encode_uint(&encoder->rep_map, repr->v_uint);
        break;
    case SOL_OIC_REPR_TYPE_INT:
        err |= cbor_encode_int(&encoder->rep_map, repr->v_int);
        break;
    case SOL_OIC_REPR_TYPE_SIMPLE:
        err |= cbor_encode_simple_value(&encoder->rep_map, repr->v_simple);
        break;
    case SOL_OIC_REPR_TYPE_TEXT_STRING: {
        const char *p = repr->v_slice.data ? repr->v_slice.data : "";

        err |= cbor_encode_text_string(&encoder->rep_map, p, repr->v_slice.len);
        break;
    }
    case SOL_OIC_REPR_TYPE_BYTE_STRING: {
        const uint8_t *empty = (const uint8_t *)"";
        const uint8_t *p = repr->v_slice.data ? (const uint8_t *)repr->v_slice.data : empty;

        err |= cbor_encode_byte_string(&encoder->rep_map, p, repr->v_slice.len);
        break;
    }
    case SOL_OIC_REPR_TYPE_HALF_FLOAT:
        err |= cbor_encode_half_float(&encoder->rep_map, repr->v_voidptr);
        break;
    case SOL_OIC_REPR_TYPE_FLOAT:
        err |= cbor_encode_float(&encoder->rep_map, repr->v_float);
        break;
    case SOL_OIC_REPR_TYPE_DOUBLE:
        err |= cbor_encode_double(&encoder->rep_map, repr->v_double);
        break;
    case SOL_OIC_REPR_TYPE_BOOLEAN:
        err |= cbor_encode_boolean(&encoder->rep_map, repr->v_boolean);
        break;
    default:
        err |= CborErrorUnknownType;
    }

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
        repr->type = SOL_OIC_REPR_TYPE_BOOLEAN;
        break;
    default:
        SOL_ERR("While parsing representation map, got unexpected type %d",
            cbor_value_get_type(value));
        if (err == CborNoError)
            err = CborErrorUnknownType;

harmless:
        /* Initialize repr with harmless data so cleanup works. */
        repr->v_boolean = false;
        repr->type = SOL_OIC_REPR_TYPE_BOOLEAN;
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

    return ptr && len == 1 && *ptr == SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
}

bool
sol_cbor_array_to_vector(CborValue *array, struct sol_vector *vector)
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

bool
sol_cbor_map_get_array(const CborValue *map, const char *key,
    struct sol_vector *vector)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    if (!cbor_value_is_array(&value))
        return false;

    return sol_cbor_array_to_vector(&value, vector);
}

bool
sol_cbor_map_get_str_value(const CborValue *map, const char *key,
    struct sol_str_slice *slice)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    if (!cbor_value_is_text_string(&value))
        return false;

    return cbor_value_dup_text_string(&value, (char **)&slice->data, &slice->len, NULL) == CborNoError;
}

bool
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
        return false;

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

    return true;

error:
    free(*data);
    *data = NULL;
    sol_vector_clear(vector);
    return false;
}

bool
sol_cbor_map_get_bsv(const CborValue *map, const char *key,
    char **data, struct sol_vector *vector)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    if (!cbor_value_is_text_string(&value))
        return false;

    return sol_cbor_bsv_to_vector(&value, data, vector);
}

bool
sol_cbor_map_get_bytestr_value(const CborValue *map, const char *key,
    struct sol_str_slice *slice)
{
    CborValue value;

    if (cbor_value_map_find_value(map, key, &value) != CborNoError)
        return false;

    return cbor_value_dup_byte_string(&value, (uint8_t **)&slice->data, &slice->len, NULL) == CborNoError;
}

void
sol_cbor_map_get_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type *type)
{
    *type = oic_map_writer->type;
}

bool
sol_cbor_map_set_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type type)
{
    if (oic_map_writer->type == type)
        return true;

    if (oic_map_writer->payload) {
        SOL_WRN("Payload was already created. Impossible to change its type");
        return false;
    }

    oic_map_writer->type = type;
    return true;

}
