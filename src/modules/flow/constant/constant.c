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

#include "sol-flow/constant.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

static int
constant_irange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_int_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_INT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_int_options *)options;

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_INT__OUT__OUT, &opts->value);
}

static int
constant_drange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_float_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_FLOAT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_float_options *)options;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_FLOAT__OUT__OUT, &opts->value);
}

static int
constant_byte_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_byte_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_BYTE_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_byte_options *)options;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_BYTE__OUT__OUT, opts->value);
}

static int
constant_boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_boolean_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_BOOLEAN_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_boolean_options *)options;

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_BOOLEAN__OUT__OUT, opts->value);
}

static int
constant_string_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_string_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_STRING_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_string_options *)options;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_STRING__OUT__OUT, opts->value);
}

static int
constant_empty_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    return sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_EMPTY__OUT__OUT);
}

static int
constant_rgb_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_rgb_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_RGB_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_rgb_options *)options;

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_RGB__OUT__OUT, &opts->value);
}

static int
constant_direction_vector_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_direction_vector_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_DIRECTION_VECTOR_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_direction_vector_options *)options;

    return sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_DIRECTION_VECTOR__OUT__OUT, &opts->value);

    return 0;
}

#include "constant-gen.c"
