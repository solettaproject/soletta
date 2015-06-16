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

#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "coap.h"

static uint8_t
coap_option_header_get_delta(uint8_t buf)
{
    return (buf & 0xF0) >> 4;
}

static uint8_t
coap_option_header_get_len(uint8_t buf)
{
    return buf & 0x0F;
}

static void
coap_option_header_set_delta(uint8_t *buf, uint8_t delta)
{
    *buf |= (delta & 0xF) << 4;
}

static void
coap_option_header_set_len(uint8_t *buf, uint8_t len)
{
    *buf |= (len & 0xF);
}

static int
decode_delta(int num, const uint8_t *buf, int16_t buflen, uint16_t *decoded)
{
    int hdrlen = 0;

    switch (num) {
    case 13:
        if (buflen < 1)
            return -EINVAL;

        num = *buf + 13;
        hdrlen += 1;
        break;
    case 14:
        if (buflen < 2)
            return -EINVAL;

        num = ntohs((uint16_t)*buf) + 269;
        hdrlen += 2;
        break;
    case 15:
        return -EINVAL;
    }

    *decoded = num;

    return hdrlen;
}

int
coap_parse_option(const struct sol_coap_packet *pkt, struct option_context *context,
    uint8_t **value, uint16_t *vlen)
{
    uint16_t delta, len;
    int r;

    if (context->buflen < 1)
        return 0;

    /* This indicates that options have ended */
    if (context->buf[0] == COAP_MARKER)
        return 0;

    delta = coap_option_header_get_delta(context->buf[0]);
    len = coap_option_header_get_len(context->buf[0]);
    context->buf += 1;
    context->used += 1;
    context->buflen -= 1;

    /* In case 'delta' doesn't fit the option fixed header. */
    r = decode_delta(delta, context->buf, context->buflen, &delta);
    if (r < 0)
        return -EINVAL;

    context->buf += r;
    context->used += r;
    context->buflen -= r;

    /* In case 'len' doesn't fit the option fixed header. */
    r = decode_delta(len, context->buf, context->buflen, &len);
    if (r < 0)
        return -EINVAL;

    if (context->buflen < r + len)
        return -EINVAL;

    if (value)
        *value = context->buf + r;

    if (vlen)
        *vlen = len;

    context->buf += r + len;
    context->used += r + len;
    context->buflen -= r + len;

    context->delta += delta;

    return context->used;
}

static int
coap_parse_options(struct sol_coap_packet *pkt, unsigned int offset)
{
    struct option_context context = { .delta = 0,
                                      .used = 0,
                                      .buflen = pkt->payload.size - offset,
                                      .buf = &pkt->buf[offset] };

    while (true) {
        int r = coap_parse_option(pkt, &context, NULL, NULL);
        if (r < 0)
            return -EINVAL;

        if (r == 0)
            break;
    }
    return context.used;
}

int
coap_get_header_len(const struct sol_coap_packet *pkt)
{
    struct coap_header *hdr;
    unsigned int hdrlen;
    uint8_t tkl;

    hdrlen = sizeof(struct coap_header);

    if (pkt->payload.size < hdrlen)
        return -EINVAL;

    hdr = (struct coap_header *)pkt->buf;
    tkl = hdr->tkl;

    // Token lenghts 9-15 are reserved.
    if (tkl > 8)
        return -EINVAL;

    if (pkt->payload.size < hdrlen + tkl)
        return -EINVAL;

    return hdrlen + tkl;
}

int
coap_packet_parse(struct sol_coap_packet *pkt)
{
    int optlen, hdrlen;

    SOL_NULL_CHECK(pkt, -EINVAL);

    hdrlen = coap_get_header_len(pkt);
    if (hdrlen < 0)
        return -EINVAL;

    optlen = coap_parse_options(pkt, hdrlen);
    if (optlen < 0)
        return -EINVAL;

    if (pkt->payload.size < hdrlen + optlen)
        return -EINVAL;

    if (pkt->payload.size > COAP_UDP_MTU)
        return -EINVAL;

    if (pkt->payload.size <= hdrlen + optlen + 1) {
        pkt->payload.start = NULL;
        pkt->payload.used = pkt->payload.size;
        return 0;
    }

    pkt->payload.start = pkt->buf + hdrlen + optlen + 1;
    pkt->payload.used = hdrlen + optlen + 1;
    return 0;
}

int
coap_find_options(const struct sol_coap_packet *pkt, uint16_t code,
    struct sol_coap_option_value *vec, uint16_t veclen)
{

    struct option_context context = { .delta = 0,
                                      .used = 0 };
    int used, count = 0;
    int hdrlen;

    SOL_NULL_CHECK(vec, -EINVAL);
    SOL_NULL_CHECK(pkt, -EINVAL);

    hdrlen = coap_get_header_len(pkt);
    SOL_INT_CHECK(hdrlen, < 0, -EINVAL);

    context.buflen = pkt->payload.used - hdrlen;
    context.buf = (uint8_t *)pkt->buf + hdrlen;

    while (context.delta <= code && count < veclen) {
        used = coap_parse_option(pkt, &context, &vec[count].value, &vec[count].len);
        if (used < 0)
            return -ENOENT;

        if (used == 0)
            break;

        if (code != context.delta)
            continue;

        count++;
    }

    return count;
}

static int
delta_encode(int num, uint8_t *value, uint8_t *buf, size_t buflen)
{
    uint16_t v;

    if (num < 13) {
        *value = num;
        return 0;
    }

    if (num < 269) {
        if (buflen < 1)
            return -EINVAL;

        *value = 13;
        *buf = num - 13;
        return 1;
    }

    if (buflen < 2)
        return -EINVAL;

    *value = 14;

    v = htons(num - 269);
    memcpy(buf, &v, sizeof(v));

    return 2;
}

int
coap_option_encode(struct option_context *context, uint16_t code,
    const void *value, uint16_t len)
{
    int delta, offset, r;
    uint8_t data;

    delta = code - context->delta;

    offset = 1;

    r = delta_encode(delta, &data, context->buf + offset, context->buflen - offset);
    if (r < 0)
        return -EINVAL;

    offset += r;
    coap_option_header_set_delta(context->buf, data);

    r = delta_encode(len, &data, context->buf + offset, context->buflen - offset);
    if (r < 0)
        return -EINVAL;

    offset += r;
    coap_option_header_set_len(context->buf, data);

    if (context->buflen < offset + len)
        return -EINVAL;

    memcpy(context->buf + offset, value, len);

    return offset + len;
}
