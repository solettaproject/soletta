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

#pragma once

#include "cbor.h"
#include "sol-coap.h"
#include "sol-oic-common.h"
#include "sol-vector.h"

CborError sol_oic_packet_cbor_extract_repr_map(struct sol_coap_packet *pkt, CborParser *parser, CborValue *repr_map);
CborError sol_oic_cbor_repr_map_get_next_field(CborValue *value, struct sol_oic_repr_field *repr);
CborError sol_oic_packet_cbor_close(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder);
CborError sol_oic_packet_cbor_create(struct sol_coap_packet *pkt, const char *href, struct sol_oic_map_writer *encoder);
CborError sol_oic_packet_cbor_append(struct sol_oic_map_writer *encoder, struct sol_oic_repr_field *repr);
bool sol_oic_pkt_has_cbor_content(const struct sol_coap_packet *pkt);

struct sol_oic_map_writer {
    CborEncoder encoder, rep_map, array, map;
    uint8_t *payload;
};

enum sol_oic_payload_type {
    SOL_OIC_PAYLOAD_DISCOVERY = 1,
    SOL_OIC_PAYLOAD_PLATFORM = 3,
    SOL_OIC_PAYLOAD_REPRESENTATION = 4,
};

#define SOL_OIC_KEY_REPRESENTATION "rep"
#define SOL_OIC_KEY_HREF "href"
#define SOL_OIC_KEY_PLATFORM_ID "pi"
#define SOL_OIC_KEY_MANUF_NAME "mnmn"
#define SOL_OIC_KEY_MANUF_URL "mnml"
#define SOL_OIC_KEY_MODEL_NUM "mnmo"
#define SOL_OIC_KEY_MANUF_DATE "mndt"
#define SOL_OIC_KEY_PLATFORM_VER "mnpv"
#define SOL_OIC_KEY_OS_VER "mnos"
#define SOL_OIC_KEY_HW_VER "mnhw"
#define SOL_OIC_KEY_FIRMWARE_VER "mnfv"
#define SOL_OIC_KEY_SUPPORT_URL "mnsl"
#define SOL_OIC_KEY_SYSTEM_TIME "st"
#define SOL_OIC_KEY_DEVICE_ID "sid"
#define SOL_OIC_KEY_PROPERTIES "prop"
#define SOL_OIC_KEY_RESOURCE_TYPES "rt"
#define SOL_OIC_KEY_INTERFACES "if"
#define SOL_OIC_KEY_POLICY "p"
#define SOL_OIC_KEY_BITMAP "bm"

