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

    static_assert(sizeof(*iterator) == sizeof(CborValue),
        "struct sol_oic_map_reader size must be at least the same size of "
        "CborValue struct defined in cbor.h header2");

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

    if (field)
        free((char *)field->key);
}

SOL_API bool
sol_oic_map_loop_next(struct sol_oic_repr_field *repr, struct sol_oic_map_reader *iterator, enum sol_oic_map_loop_reason *reason)
{
    CborError err;

    repr_field_free(repr);
    if (!cbor_value_is_valid((CborValue *)iterator))
        return false;

    err = sol_oic_cbor_repr_map_get_next_field((CborValue *)iterator, repr);
    if (err != CborNoError) {
        *reason = SOL_OIC_MAP_LOOP_ERROR;
        return false;
    }

    return true;
}

SOL_API bool
sol_oic_map_append(struct sol_oic_map_writer *oic_map_writer, struct sol_oic_repr_field *repr)
{
    return sol_oic_packet_cbor_append(oic_map_writer, repr) == CborNoError;
}
