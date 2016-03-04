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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sol-log.h"
#include "sol-util-internal.h"
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

        num = sol_util_be16_to_cpu((uint16_t)*buf) + 269;
        hdrlen += 2;
        break;
    case 15:
        return -EINVAL;
    }

    *decoded = num;

    return hdrlen;
}

int
coap_parse_option(struct option_context *context,
    uint8_t **value, uint16_t *vlen)
{
    uint16_t delta, len;
    int r;

    if (context->buf->used - context->pos < 1)
        return 0;

    /* This indicates that options have ended */
    if (((uint8_t *)sol_buffer_at(context->buf, context->pos))[0] ==
        COAP_MARKER)
        return 0;

    delta = coap_option_header_get_delta((((uint8_t *)sol_buffer_at
            (context->buf, context->pos))[0]));
    len = coap_option_header_get_len((((uint8_t *)sol_buffer_at
            (context->buf, context->pos))[0]));
    context->pos += 1;
    context->used += 1;

    /* In case 'delta' doesn't fit the option fixed header. */
    r = decode_delta(delta, sol_buffer_at(context->buf, context->pos),
        context->buf->used - context->pos, &delta);
    if (r < 0)
        return r;

    context->pos += r;
    context->used += r;

    /* In case 'len' doesn't fit the option fixed header. */
    r = decode_delta(len, sol_buffer_at(context->buf, context->pos),
        context->buf->used - context->pos, &len);
    if (r < 0)
        return r;

    if (context->buf->used - context->pos < (size_t)(r + len))
        return -EINVAL;

    if (value)
        *value = sol_buffer_at(context->buf, context->pos + r);

    if (vlen)
        *vlen = len;

    context->pos += r + len;
    context->used += r + len;

    context->delta += delta;

    return context->used;
}

static int
coap_parse_options(struct sol_coap_packet *pkt, unsigned int offset)
{
    struct option_context context = { .delta = 0,
                                      .used = 0,
                                      .buf = &pkt->buf,
                                      .pos = offset };

    while (true) {
        int r = coap_parse_option(&context, NULL, NULL);
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

    if (pkt->buf.used < hdrlen)
        return -EINVAL;

    hdr = (struct coap_header *)sol_buffer_at(&pkt->buf, 0);
    tkl = hdr->tkl;

    // Token lenghts 9-15 are reserved.
    if (tkl > 8)
        return -EINVAL;

    if (pkt->buf.used < hdrlen + tkl)
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
        return hdrlen;

    optlen = coap_parse_options(pkt, hdrlen);
    if (optlen < 0)
        return optlen;

    if (pkt->buf.used < (size_t)(hdrlen + optlen))
        return -EINVAL;

    if (pkt->buf.used > COAP_UDP_MTU)
        return -EINVAL;

    /* +1 for COAP_MARKER */
    if (pkt->buf.used <= (size_t)(hdrlen + optlen + 1)) {
        pkt->payload_start = 0;
        return 0;
    }

    pkt->payload_start = hdrlen + optlen + 1;
    return 0;
}

static int
delta_encode(int num, uint8_t *value, struct sol_buffer *buf, size_t offset)
{
    uint16_t v;
    int r;

    if (num < 13) {
        *value = num;
        return 0;
    }

    if (num < 269) {
        *value = 13;
        r = sol_buffer_insert_char(buf, offset, num - 13);
        SOL_INT_CHECK(r, < 0, r);

        return 1;
    }

    *value = 14;

    v = sol_util_cpu_to_be16(num - 269);

    r = sol_buffer_insert_bytes(buf, offset, (uint8_t *)&v, sizeof(v));
    SOL_INT_CHECK(r, < 0, r);

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

    /* write zero on this reserved space, just to advance buffer's 'used' */
    r = sol_buffer_set_char_at(context->buf, context->pos, 0);
    SOL_INT_CHECK(r, < 0, r);

    r = delta_encode(delta, &data, context->buf, context->pos + offset);
    if (r < 0)
        return -EINVAL;

    offset += r;
    coap_option_header_set_delta(sol_buffer_at(context->buf, context->pos),
        data);

    r = delta_encode(len, &data, context->buf, context->pos + offset);
    SOL_INT_CHECK(r, < 0, r);

    offset += r;
    coap_option_header_set_len(sol_buffer_at(context->buf, context->pos),
        data);
    r = sol_buffer_insert_bytes(context->buf,
        context->pos + offset, value, len);
    SOL_INT_CHECK(r, < 0, r);

    return offset + len;
}
