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

#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

#include "hub-gen.h"

static int
boolean_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    bool in_value;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_BOOLEAN__OUT__OUT, in_value);
}

static int
byte_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    unsigned char in_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_BYTE__OUT__OUT, in_value);
}

static int
blob_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_blob *in_value;

    r = sol_flow_packet_get_blob(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_BLOB__OUT__OUT, in_value);
}

static int
rgb_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_rgb in_value;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_RGB__OUT__OUT, &in_value);
}

static int
empty_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_EMPTY__OUT__OUT);
}

static int
error_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    int code_value;
    const char *msg;

    r = sol_flow_packet_get_error(packet, &code_value, &msg);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_error_packet(node, code_value, msg);
}

static int
float_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_FLOAT__OUT__OUT, &in_value);
}

static int
int_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_INT__OUT__OUT, &in_value);
}

static int
string_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_HUB_STRING__OUT__OUT, in_value);
}

#include "hub-gen.c"
