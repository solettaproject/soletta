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

#pragma once

#include "tinycbor/cbor.h"
#include "sol-coap.h"
#include "sol-oic.h"
#include "sol-vector.h"

CborError sol_oic_packet_cbor_extract_repr_map(struct sol_coap_packet *pkt, CborParser *parser, CborValue *repr_map);
CborError sol_oic_cbor_repr_map_get_next_field(CborValue *value, struct sol_oic_repr_field *repr);
CborError sol_oic_packet_cbor_close(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder);
void sol_oic_packet_cbor_create(struct sol_coap_packet *pkt, struct sol_oic_map_writer *encoder);
CborError sol_oic_packet_cbor_append(struct sol_oic_map_writer *encoder, struct sol_oic_repr_field *repr);
bool sol_oic_pkt_has_cbor_content(const struct sol_coap_packet *pkt);
int sol_cbor_map_get_bytestr_value(const CborValue *map, const char *key, struct sol_str_slice *slice);
/* BSV is a blank separated string, as defined in oic documentation. It is a
 * string with list of values, separated by a blank space. */
int sol_cbor_map_get_bsv(const CborValue *map, const char *key, char **data, struct sol_vector *vector);
int sol_cbor_map_get_str_value(const CborValue *map, const char *key, struct sol_str_slice *slice);
int sol_cbor_map_get_array(const CborValue *map, const char *key, struct sol_vector *vector);
int sol_cbor_array_to_vector(CborValue *array, struct sol_vector *vector);
int sol_cbor_bsv_to_vector(const CborValue *value, char **data, struct sol_vector *vector);

void sol_cbor_map_get_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type *type);
int sol_cbor_map_set_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type type);

struct sol_oic_map_writer {
    CborEncoder encoder, rep_map;
    uint8_t *payload;
    struct sol_coap_packet *pkt;
    enum sol_oic_map_type type;
};

#define SOL_OIC_DEVICE_PATH "/oic/d"
#define SOL_OIC_PLATFORM_PATH "/oic/p"

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
#define SOL_OIC_KEY_DEVICE_ID "di"
#define SOL_OIC_KEY_RESOURCE_LINKS "links"
#define SOL_OIC_KEY_PROPERTIES "prop"
#define SOL_OIC_KEY_RESOURCE_TYPES "rt"
#define SOL_OIC_KEY_INTERFACES "if"
#define SOL_OIC_KEY_POLICY "p"
#define SOL_OIC_KEY_POLICY_SECURE "sec"
#define SOL_OIC_KEY_POLICY_PORT "port"
#define SOL_OIC_KEY_BITMAP "bm"
#define SOL_OIC_KEY_DEVICE_NAME "n"
#define SOL_OIC_KEY_SPEC_VERSION "lcv"
#define SOL_OIC_KEY_DATA_MODEL_VERSION "dmv"
