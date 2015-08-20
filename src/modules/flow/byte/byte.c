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

#include "byte-gen.h"

#include "sol-flow-internal.h"

#include <sol-util.h>
#include <limits.h>
#include <errno.h>

struct bitwise_data {
    unsigned char in0;
    unsigned char in1;
    bool in0_init : 1;
    bool in1_init : 1;
};

static int
two_port_process(struct sol_flow_node *node, void *data, uint16_t port_in, uint16_t port_out, const struct sol_flow_packet *packet, int (*func)(unsigned char, unsigned char))
{
    struct bitwise_data *mdata = data;
    int r;
    unsigned char in_value;
    unsigned char out_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (port_in) {
        mdata->in1 = in_value;
        mdata->in1_init = true;
    } else {
        mdata->in0 = in_value;
        mdata->in0_init = true;
    }

    if (!(mdata->in0_init && mdata->in1_init))
        return 0;

    out_value = func(mdata->in0, mdata->in1);

    return sol_flow_send_byte_packet(node, port_out, out_value);
}

static int
and_func(unsigned char in0, unsigned char in1)
{
    return in0 & in1;
}

static int
and_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_AND__OUT__OUT, packet, and_func);
}

static int
or_func(unsigned char in0, unsigned char in1)
{
    return in0 | in1;
}

static int
or_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_OR__OUT__OUT, packet, or_func);
}

static int
xor_func(unsigned char in0, unsigned char in1)
{
    return in0 ^ in1;
}

static int
xor_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_XOR__OUT__OUT, packet, xor_func);
}

static int
validate_shift(const struct sol_flow_packet *packet)
{
    unsigned char in;
    int r;

    r = sol_flow_packet_get_byte(packet, &in);
    SOL_INT_CHECK(r, < 0, r);
    if (in > (CHAR_BIT - 1))
        return -EINVAL;
    return 0;
}

static int
shift_left_func(unsigned char in0, unsigned char in1)
{
    return in0 << in1;
}

static int
shift_left_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r = 0;

    if (port == SOL_FLOW_NODE_TYPE_BYTE_SHIFT_LEFT__IN__SHIFT)
        r = validate_shift(packet);
    if (r < 0) {
        unsigned char in;
        sol_flow_packet_get_byte(packet, &in);
        sol_flow_send_error_packet(node, -r,
            "Invalid values for a shift left operation: operation: %d, "
            "Maximum is %d", in, (CHAR_BIT - 1));
        return r;
    }
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_SHIFT_LEFT__OUT__OUT, packet, shift_left_func);
}

static int
shift_right_func(unsigned char in0, unsigned char in1)
{
    return in0 >> in1;
}

static int
shift_right_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r = 0;

    if (port == SOL_FLOW_NODE_TYPE_BYTE_SHIFT_RIGHT__IN__SHIFT)
        r = validate_shift(packet);
    if (r < 0) {
        unsigned char in;
        sol_flow_packet_get_byte(packet, &in);
        sol_flow_send_error_packet(node, -r,
            "Invalid values for a shift left operation: operation: %d,"
            " Maximum is %d", in, (CHAR_BIT - 1));
        return r;
    }
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_SHIFT_RIGHT__OUT__OUT, packet, shift_right_func);
}

static int
not_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    unsigned char in_value;
    unsigned char out_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = ~in_value;
    return sol_flow_send_byte_packet(node, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_NOT__OUT__OUT, out_value);
}

// =============================================================================
// BYTE FILTER
// =============================================================================

struct byte_filter_data {
    unsigned char max;
    unsigned char min;
};

static int
byte_filter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct byte_filter_data *mdata = data;
    const struct sol_flow_node_type_byte_filter_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_BYTE_FILTER_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_byte_filter_options *)options;
    if (opts->max >= opts->min) {
        mdata->min = opts->min;
        mdata->max = opts->max;
    } else {
        SOL_DBG("min %d should be smaller than max %d.",
            opts->min, opts->max);
        mdata->min = opts->max;
        mdata->max = opts->min;
    }
    return 0;
}


static int
byte_filter_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct  sol_flow_packet *packet)
{
    unsigned char value;
    int r;
    struct byte_filter_data *mdata = data;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value >= mdata->min && value <= mdata->max ) {
        return sol_flow_send_byte_packet(node, SOL_FLOW_NODE_TYPE_BYTE_FILTER__OUT__OUT, value);
    }
    return 0;
}


#include "byte-gen.c"
