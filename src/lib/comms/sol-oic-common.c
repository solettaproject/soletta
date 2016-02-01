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

#include "sol-oic-common.h"
#include "sol-oic-cbor.h"
#include "sol-log.h"
#include <assert.h>

SOL_API enum sol_oic_map_loop_reason
sol_oic_map_loop_init(const struct sol_oic_map_reader *map, struct sol_oic_map_reader *iterator, struct sol_oic_repr_field *repr)
{
    SOL_NULL_CHECK(map, SOL_OIC_MAP_LOOP_ERROR);
    SOL_NULL_CHECK(iterator, SOL_OIC_MAP_LOOP_ERROR);
    SOL_NULL_CHECK(repr, SOL_OIC_MAP_LOOP_ERROR);

#if __STDC_VERSION__ >= 201112L
    static_assert(sizeof(*iterator) == sizeof(CborValue),
        "struct sol_oic_map_reader size must be at least the same size of "
        "CborValue struct defined in cbor.h header2");
#endif

    if (!cbor_value_is_map((CborValue *)map))
        return SOL_OIC_MAP_LOOP_ERROR;

    if (cbor_value_enter_container((CborValue *)map, (CborValue *)iterator) != CborNoError)
        return SOL_OIC_MAP_LOOP_ERROR;

    /* Initialize repr with harmless data so cleanup works. */
    repr->type = SOL_OIC_REPR_TYPE_BOOLEAN;
    repr->key = NULL;
    return SOL_OIC_MAP_LOOP_OK;
}

static void
repr_field_free(struct sol_oic_repr_field *field)
{
    if (field->type == SOL_OIC_REPR_TYPE_TEXT_STRING ||
        field->type == SOL_OIC_REPR_TYPE_BYTE_STRING)
        free((char *)field->v_slice.data);

    free((char *)field->key);
}

SOL_API bool
sol_oic_map_loop_next(struct sol_oic_repr_field *repr, struct sol_oic_map_reader *iterator, enum sol_oic_map_loop_reason *reason)
{
    CborError err;

    SOL_NULL_CHECK_GOTO(repr, err);
    SOL_NULL_CHECK_GOTO(iterator, err);

    repr_field_free(repr);
    if (cbor_value_at_end((CborValue *)iterator))
        return false;

    if (!cbor_value_is_valid((CborValue *)iterator))
        goto err;

    err = sol_oic_cbor_repr_map_get_next_field((CborValue *)iterator, repr);
    if (err != CborNoError)
        goto err;

    return true;

err:
    if (reason)
        *reason = SOL_OIC_MAP_LOOP_ERROR;
    return false;
}

SOL_API bool
sol_oic_map_append(struct sol_oic_map_writer *oic_map_writer, struct sol_oic_repr_field *repr)
{
    SOL_NULL_CHECK(oic_map_writer, false);
    SOL_NULL_CHECK(repr, false);

    return sol_oic_packet_cbor_append(oic_map_writer, repr) == CborNoError;
}

SOL_API bool
sol_oic_map_get_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type *type)
{
    SOL_NULL_CHECK(oic_map_writer, false);
    SOL_NULL_CHECK(type, false);

    sol_cbor_map_get_type(oic_map_writer, type);

    return true;
}

SOL_API bool
sol_oic_map_set_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type type)
{
    SOL_NULL_CHECK(oic_map_writer, false);

    return sol_cbor_map_set_type(oic_map_writer, type);
}

#ifdef SOL_LOG_ENABLED
SOL_API void
sol_oic_payload_debug(struct sol_coap_packet *pkt)
{
    SOL_NULL_CHECK(pkt);

#ifdef HAVE_STDOUT
    uint8_t *payload;
    uint16_t payload_len;
    CborParser parser;
    CborError err;
    CborValue root;

    if (!sol_oic_pkt_has_cbor_content(pkt) ||
        !sol_coap_packet_has_payload(pkt)) {
        return;
    }
    if (sol_coap_packet_get_payload(pkt, &payload, &payload_len) < 0) {
        SOL_DBG("Failed to get packet payload");
        return;
    }

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    if (err != CborNoError) {
        SOL_DBG("Failed to get cbor payload");
        return;
    }

    cbor_value_to_pretty(stdout, &root);
    fprintf(stdout, "\n");
#else
    SOL_DBG("Failed to log oic payload: stdout not available");
#endif
}
#endif
