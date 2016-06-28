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

#include <inttypes.h>
#include "sol-buffer.h"

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
struct coap_header {
    uint8_t ver : 2;
    uint8_t type : 2;
    uint8_t tkl : 4;
    uint8_t code;
    uint16_t id;
} __attribute__ ((packed));
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
struct coap_header {
    uint8_t tkl : 4;
    uint8_t type : 2;
    uint8_t ver : 2;
    uint8_t code;
    uint16_t id;
} __attribute__ ((packed));
#else
#error "Unknown byte order"
#endif

struct sol_coap_packet {
    int refcnt;
    struct sol_buffer buf;
    size_t payload_start;
};

struct option_context {
    struct sol_buffer *buf;
    size_t pos; /* current position on buf */
    int delta;
    int used; /* size used of options */
};

#define COAP_VERSION 1

#define COAP_MARKER 0xFF

int coap_get_header_len(const struct sol_coap_packet *pkt);

struct sol_coap_packet *coap_new_packet(struct sol_coap_packet *old);

int coap_parse_option(struct option_context *context, uint8_t **value, uint16_t *vlen);

int coap_option_encode(struct option_context *context, uint16_t code,
    const void *value, uint16_t len);

int coap_packet_parse(struct sol_coap_packet *pkt);
