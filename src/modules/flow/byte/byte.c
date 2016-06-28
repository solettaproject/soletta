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

#include "sol-flow/byte.h"

#include "sol-flow-internal.h"

#include <sol-util-internal.h>
#include <limits.h>
#include <errno.h>

struct bitwise_data {
    uint8_t in0;
    uint8_t in1;
    bool in0_init : 1;
    bool in1_init : 1;
};

static int
two_port_process(struct sol_flow_node *node, void *data, uint16_t port_in, uint16_t port_out, const struct sol_flow_packet *packet, int (*func)(uint8_t, uint8_t))
{
    struct bitwise_data *mdata = data;
    int r;
    uint8_t in_value;
    uint8_t out_value;

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
and_func(uint8_t in0, uint8_t in1)
{
    return in0 & in1;
}

static int
and_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_AND__OUT__OUT, packet, and_func);
}

static int
or_func(uint8_t in0, uint8_t in1)
{
    return in0 | in1;
}

static int
or_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return two_port_process(node, data, port, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_OR__OUT__OUT, packet, or_func);
}

static int
xor_func(uint8_t in0, uint8_t in1)
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
    uint8_t in;
    int r;

    r = sol_flow_packet_get_byte(packet, &in);
    SOL_INT_CHECK(r, < 0, r);
    if (in > (CHAR_BIT - 1))
        return -EINVAL;
    return 0;
}

static int
shift_left_func(uint8_t in0, uint8_t in1)
{
    return in0 << in1;
}

static int
shift_left_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    if (port == SOL_FLOW_NODE_TYPE_BYTE_SHIFT_LEFT__IN__SHIFT) {
        int r = validate_shift(packet);

        if (r < 0) {
            int err;
            uint8_t in;

            err = sol_flow_packet_get_byte(packet, &in);
            SOL_INT_CHECK(err, < 0, err);

            sol_flow_send_error_packet(node, -r,
                "Invalid values for a shift left operation: operation: %d. "
                "Maximum is %d", in, (CHAR_BIT - 1));
            return 0;
        }
    }

    return two_port_process(node, data, port,
        SOL_FLOW_NODE_TYPE_BYTE_SHIFT_LEFT__OUT__OUT,
        packet, shift_left_func);
}

static int
shift_right_func(uint8_t in0, uint8_t in1)
{
    return in0 >> in1;
}

static int
shift_right_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    if (port == SOL_FLOW_NODE_TYPE_BYTE_SHIFT_RIGHT__IN__SHIFT) {
        int r = validate_shift(packet);

        if (r < 0) {
            int err;
            uint8_t in;

            err = sol_flow_packet_get_byte(packet, &in);
            SOL_INT_CHECK(err, < 0, err);

            sol_flow_send_error_packet(node, -r,
                "Invalid values for a shift left operation: operation: %d. "
                "Maximum is %d", in, (CHAR_BIT - 1));
            return 0;
        }
    }

    return two_port_process(node, data, port,
        SOL_FLOW_NODE_TYPE_BYTE_SHIFT_RIGHT__OUT__OUT,
        packet, shift_right_func);
}

static int
not_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    uint8_t in_value;
    uint8_t out_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = ~in_value;
    return sol_flow_send_byte_packet(node, SOL_FLOW_NODE_TYPE_BYTE_BITWISE_NOT__OUT__OUT, out_value);
}

// =============================================================================
// BYTE FILTER
// =============================================================================

struct byte_filter_data {
    uint8_t max;
    uint8_t min;
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
    uint8_t value;
    int r;
    struct byte_filter_data *mdata = data;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value >= mdata->min && value <= mdata->max ) {
        return sol_flow_send_byte_packet(node, SOL_FLOW_NODE_TYPE_BYTE_FILTER__OUT__OUT, value);
    }
    return 0;
}

// =============================================================================
// BYTE COMPARISON
// =============================================================================

struct byte_comparison_node_type {
    struct sol_flow_node_type base;
    bool (*func) (uint8_t var0, uint8_t var1);
};

struct byte_comparison_data {
    uint8_t val[2];
    bool val_initialized[2];
};

static bool
byte_val_eq(uint8_t var0, uint8_t var1)
{
    return var0 == var1;
}

static bool
byte_val_less(uint8_t var0, uint8_t var1)
{
    return var0 < var1;
}

static bool
byte_val_less_or_eq(uint8_t var0, uint8_t var1)
{
    return var0 <= var1;
}

static bool
byte_val_greater(uint8_t var0, uint8_t var1)
{
    return var0 > var1;
}

static bool
byte_val_greater_or_eq(uint8_t var0, uint8_t var1)
{
    return var0 >= var1;
}

static bool
byte_val_not_eq(uint8_t var0, uint8_t var1)
{
    return var0 != var1;
}

static int
comparison_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct byte_comparison_data *mdata = data;
    bool output;
    const struct byte_comparison_node_type *type;
    int r;

    r = sol_flow_packet_get_byte(packet, &mdata->val[port]);
    SOL_INT_CHECK(r, < 0, r);

    mdata->val_initialized[port] = true;
    if (!(mdata->val_initialized[0] && mdata->val_initialized[1]))
        return 0;

    type = (const struct byte_comparison_node_type *)
        sol_flow_node_get_type(node);

    output = type->func(mdata->val[0], mdata->val[1]);
    return sol_flow_send_bool_packet(node, 0, output);
}

#include "byte-gen.c"
