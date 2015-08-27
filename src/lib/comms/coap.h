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

#define COAP_UDP_MTU (576)

struct sol_coap_packet {
    int refcnt;
    struct {
        uint8_t *start;
        uint16_t size;
        uint16_t used;
    } payload;
    uint8_t buf[COAP_UDP_MTU];
};

struct sol_coap_option_value {
    uint8_t *value;
    uint16_t len;
};

struct option_context {
    uint8_t *buf;
    int delta;
    int used;
    int buflen;
};

#define COAP_VERSION 1

#define COAP_MARKER 0xFF

int coap_get_header_len(const struct sol_coap_packet *pkt);

struct sol_coap_packet *coap_new_packet(struct sol_coap_packet *old);

int coap_find_options(const struct sol_coap_packet *pkt, uint16_t code,
    struct sol_coap_option_value *vec, uint16_t veclen);

int coap_parse_option(const struct sol_coap_packet *pkt, struct option_context *context,
    uint8_t **value, uint16_t *vlen);

int coap_option_encode(struct option_context *context, uint16_t code,
    const void *value, uint16_t len);

int coap_packet_parse(struct sol_coap_packet *pkt);
