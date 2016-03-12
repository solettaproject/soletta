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

#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>

#include "sol-str-slice.h"
#include "sol-coap.h"
#include "sol-mainloop.h"

#include "coap.h"
#include "test.h"

#include "sol-util-internal.h"

DEFINE_TEST(test_coap_parse_empty_pdu);

static void
test_coap_parse_empty_pdu(void)
{
    uint8_t pdu[] = { 0x40, 0x01, 0, 0 };
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    size_t offset;
    uint8_t ver, type, code;
    uint16_t id;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_header_get_ver(pkt, &ver);
    sol_coap_header_get_type(pkt, &type);
    sol_coap_header_get_code(pkt, &code);
    sol_coap_header_get_id(pkt, &id);

    ASSERT_INT_EQ(ver, 1);
    ASSERT_INT_EQ(type, SOL_COAP_TYPE_CON);
    ASSERT_INT_EQ(code, SOL_COAP_METHOD_GET);
    ASSERT_INT_EQ(id, 0);

    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_parse_simple_pdu);

static void
test_coap_parse_simple_pdu(void)
{
    uint8_t pdu[] = { 0x55, 0xA5, 0x12, 0x34, 't', 'o', 'k', 'e',
                      'n',  0x00, 0xc1, 0x00, 0xff, 'p', 'a', 'y',
                      'l', 'o', 'a', 'd', 0x00 };
    struct sol_str_slice options[16];
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    uint8_t *token;
    int count = 16;
    size_t offset;
    uint8_t tkl, code, ver, type;
    uint16_t id;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_header_get_ver(pkt, &ver);
    sol_coap_header_get_type(pkt, &type);

    ASSERT_INT_EQ(ver, 1);
    ASSERT_INT_EQ(type, SOL_COAP_TYPE_NONCON);

    token = sol_coap_header_get_token(pkt, &tkl);
    ASSERT(token);
    ASSERT_INT_EQ(tkl, 5);
    ASSERT(!strcmp((char *)token, "token"));

    sol_coap_header_get_code(pkt, &code);
    sol_coap_header_get_id(pkt, &id);
    ASSERT_INT_EQ(code, SOL_COAP_RSPCODE_PROXYING_NOT_SUPPORTED);
    ASSERT_INT_EQ(id, 0x1234);

    count = sol_coap_find_options(pkt, SOL_COAP_OPTION_CONTENT_FORMAT, options, count);
    ASSERT_INT_EQ(count, 1);
    ASSERT_INT_EQ(options[0].len, 1);
    ASSERT_INT_EQ((uint8_t)options[0].data[0], 0);

    /* Not existent. */
    count = sol_coap_find_options(pkt, SOL_COAP_OPTION_ETAG, options, count);
    ASSERT_INT_EQ(count, 0);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT_INT_EQ(offset + sizeof("payload"), buf->used);
    ASSERT(!strcmp((char *)buf->data + offset, "payload"));

    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_parse_illegal_token_length);

static void
test_coap_parse_illegal_token_length(void)
{
    uint8_t pdu[] = { 0x59, 0x69, 0x12, 0x34, 't', 'o', 'k', 'e', 'n', '1', '2', '3', '4' };
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    size_t offset;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(coap_packet_parse(pkt));

    pdu[0] = 0x5f;
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(coap_packet_parse(pkt));

    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_parse_options_that_exceed_pdu);

static void
test_coap_parse_options_that_exceed_pdu(void)
{
    uint8_t pdu[] = { 0x55, 0x73, 0x12, 0x34, 't', 'o', 'k', 'e', 'n', 0x00, 0xc1, 0x00, 0xae, 0xf0, 0x03 };
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    size_t offset;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(coap_packet_parse(pkt));
    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_parse_without_options_with_payload);

static void
test_coap_parse_without_options_with_payload(void)
{
    uint8_t pdu[] = { 0x50, 0x73, 0x12, 0x34, 0xff, 'p', 'a', 'y', 'l', 'o', 'a', 'd' };
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    size_t offset;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_payload_simple);

static void
test_coap_payload_simple(void)
{
    uint8_t pdu[] = { 0x50, 0x73, 0x12, 0x34, 0xff, 'p', 'a', 'y', 'l', 'o', 'a', 'd', 0x00 };
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    size_t offset;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT_INT_EQ(buf->used - offset, sizeof("payload"));
    ASSERT(streq((char *)buf->data + offset, "payload"));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_token_simple);

static void
test_coap_token_simple(void)
{
    uint8_t token[] = { 't', 'o', 'k', 'e', 'n' };
    uint8_t token_length;
    uint8_t *token_ptr;
    struct sol_coap_packet *pkt;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_header_set_token(pkt, token, sizeof(token)));

    token_ptr = sol_coap_header_get_token(pkt, &token_length);
    ASSERT(token_ptr);
    ASSERT_INT_EQ(token_length, sizeof(token));
    ASSERT(streqn((char *)token_ptr, (char *)token, sizeof(token)));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_packet_unref(pkt);
}


DEFINE_TEST(test_coap_find_options);

static void
test_coap_find_options(void)
{
    uint8_t pdu[] = { 0x55, 0xA5, 0x12, 0x34, 't', 'o', 'k', 'e', 'n',
                      0x00, 0xC1, 0x00, 0xff, 'p', 'a', 'y', 'l', 'o', 'a', 'd', 0x00 };
    struct sol_str_slice options[16];
    struct sol_coap_packet *pkt;
    struct sol_buffer *buf;
    int count = 16;
    size_t offset;

    pkt = sol_coap_packet_new(NULL);
    ASSERT(pkt);

    ASSERT(!sol_coap_packet_get_payload(pkt, &buf, &offset));
    ASSERT(!sol_buffer_remove_data(buf, offset, 0));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(!coap_packet_parse(pkt));

    count = sol_coap_find_options(pkt, SOL_COAP_OPTION_CONTENT_FORMAT, options, count);
    ASSERT_INT_EQ(count, 1);
    ASSERT_INT_EQ(options[0].len, 1);
    ASSERT_INT_EQ((uint8_t)options[0].data[0], 0);

    count = sol_coap_find_options(pkt, SOL_COAP_OPTION_IF_MATCH, options, count);
    ASSERT_INT_EQ(count, 0);

    sol_coap_packet_unref(pkt);
}


TEST_MAIN();
