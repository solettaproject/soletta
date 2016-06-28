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

#include "sol-oic.h"
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
    repr->type = SOL_OIC_REPR_TYPE_BOOL;
    repr->key = NULL;
    return SOL_OIC_MAP_LOOP_OK;
}

SOL_API void
sol_oic_repr_field_clear(struct sol_oic_repr_field *field)
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

    sol_oic_repr_field_clear(repr);
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

SOL_API int
sol_oic_map_append(struct sol_oic_map_writer *oic_map_writer, struct sol_oic_repr_field *repr)
{
    SOL_NULL_CHECK(oic_map_writer, -EINVAL);
    SOL_NULL_CHECK(repr, -EINVAL);

    return sol_oic_packet_cbor_append(oic_map_writer, repr) == CborNoError ? 0 :
           -EIO;
}

SOL_API int
sol_oic_map_get_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type *type)
{
    SOL_NULL_CHECK(oic_map_writer, -EINVAL);
    SOL_NULL_CHECK(type, -EINVAL);

    sol_cbor_map_get_type(oic_map_writer, type);

    return 0;
}

SOL_API int
sol_oic_map_set_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type type)
{
    SOL_NULL_CHECK(oic_map_writer, -EINVAL);

    return sol_cbor_map_set_type(oic_map_writer, type);
}

#ifdef SOL_LOG_ENABLED
SOL_API void
sol_oic_payload_debug(struct sol_coap_packet *pkt)
{
    SOL_NULL_CHECK(pkt);

#ifdef HAVE_STDOUT
    struct sol_buffer *buf;
    CborParser parser;
    CborValue root;
    CborError err;
    size_t offset;

    if (!sol_oic_pkt_has_cbor_content(pkt) ||
        !sol_coap_packet_has_payload(pkt)) {
        return;
    }
    if (sol_coap_packet_get_payload(pkt, &buf, &offset) < 0) {
        SOL_DBG("Failed to get packet payload");
        return;
    }

    err = cbor_parser_init(sol_buffer_at(buf, offset),
        buf->used - offset, 0, &parser, &root);
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
