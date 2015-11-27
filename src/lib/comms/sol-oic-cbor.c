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

#include "sol-log.h"
#include "sol-oic-cbor.h"
#include "sol-oic-common.h"

CborError
sol_oic_packet_cbor_create(struct sol_coap_packet *pkt, const char *href, struct sol_oic_map_writer *encoder)
{
    uint16_t size;
    CborError err;

    if (sol_coap_packet_get_payload(pkt, &encoder->payload, &size) < 0) {
        SOL_WRN("Could not get CoAP payload");
        return CborUnknownError;
    }

    cbor_encoder_init(&encoder->encoder, encoder->payload, size, 0);

    err = cbor_encoder_create_array(&encoder->encoder, &encoder->array,
        CborIndefiniteLength);
    err |= cbor_encode_uint(&encoder->array, SOL_OIC_PAYLOAD_REPRESENTATION);

    err |= cbor_encoder_create_map(&encoder->array, &encoder->map,
        CborIndefiniteLength);

    err |= cbor_encode_text_stringz(&encoder->map, SOL_OIC_KEY_HREF);
    err |= cbor_encode_text_stringz(&encoder->map, href);

    err |= cbor_encode_text_stringz(&encoder->map, SOL_OIC_KEY_REPRESENTATION);
    err |= cbor_encoder_create_map(&encoder->map, &encoder->rep_map,
        CborIndefiniteLength);

    return err;
}

CborError
sol_oic_packet_cbor_close(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder)
{
    CborError err;

    err = cbor_encoder_close_container(&encoder->map, &encoder->rep_map);
    err |= cbor_encoder_close_container(&encoder->array, &encoder->map);
    err |= cbor_encoder_close_container(&encoder->encoder, &encoder->array);

    if (err == CborNoError)
        sol_coap_packet_set_payload_used(pkt,
            encoder->encoder.ptr - encoder->payload);

    return err;
}

CborError
sol_oic_packet_cbor_append(struct sol_oic_map_writer *encoder, struct sol_oic_repr_field *repr)
{
    CborError err;

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
    CborValue root, array;
    uint8_t *payload;
    uint16_t size;
    int payload_type;

    if (sol_coap_packet_get_payload(pkt, &payload, &size) < 0)
        return CborErrorUnknownLength;

    err = cbor_parser_init(payload, size, 0, parser, &root);
    if (err != CborNoError)
        return err;

    if (!cbor_value_is_array(&root))
        return CborErrorIllegalType;

    err |= cbor_value_enter_container(&root, &array);

    err |= cbor_value_get_int(&array, &payload_type);
    err |= cbor_value_advance_fixed(&array);
    if (err != CborNoError)
        return err;
    if (payload_type != SOL_OIC_PAYLOAD_REPRESENTATION)
        return CborErrorIllegalType;

    err = cbor_value_map_find_value(&array, SOL_OIC_KEY_REPRESENTATION, repr_map);

    return err;
}

bool
sol_oic_pkt_has_cbor_content(const struct sol_coap_packet *pkt)
{
    const uint8_t *ptr;
    uint16_t len;

    ptr = sol_coap_find_first_option(pkt, SOL_COAP_OPTION_CONTENT_FORMAT, &len);

    return ptr && len == 1 && *ptr == SOL_COAP_CONTENTTYPE_APPLICATION_CBOR;
}
