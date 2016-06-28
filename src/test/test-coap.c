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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_header_get_version(pkt, &ver);
    sol_coap_header_get_type(pkt, &type);
    sol_coap_header_get_code(pkt, &code);
    sol_coap_header_get_id(pkt, &id);

    ASSERT_INT_EQ(ver, 1);
    ASSERT_INT_EQ(type, SOL_COAP_MESSAGE_TYPE_CON);
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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(!coap_packet_parse(pkt));

    sol_coap_header_get_version(pkt, &ver);
    sol_coap_header_get_type(pkt, &type);

    ASSERT_INT_EQ(ver, 1);
    ASSERT_INT_EQ(type, SOL_COAP_MESSAGE_TYPE_NON_CON);

    token = sol_coap_header_get_token(pkt, &tkl);
    ASSERT(token);
    ASSERT_INT_EQ(tkl, 5);
    ASSERT(!strcmp((char *)token, "token"));

    sol_coap_header_get_code(pkt, &code);
    sol_coap_header_get_id(pkt, &id);
    ASSERT_INT_EQ(code, SOL_COAP_RESPONSE_CODE_PROXYING_NOT_SUPPORTED);
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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
    ASSERT(!sol_buffer_insert_bytes(buf, 0, pdu, sizeof(pdu)));

    ASSERT(coap_packet_parse(pkt));

    pdu[0] = 0x5f;
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
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
    ASSERT(!sol_buffer_remove_data(buf, 0, offset));
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
