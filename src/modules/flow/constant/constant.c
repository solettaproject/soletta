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

#include "sol-flow/constant.h"
#include "sol-flow-internal.h"

#include <sol-util-internal.h>
#include <errno.h>

static int
constant_irange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_int_options *opts;
    struct sol_irange value;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_INT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_int_options *)options;

    r = sol_irange_compose(&opts->value_spec, opts->value, &value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_INT__OUT__OUT, &value);
}

static int
constant_drange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_float_options *opts;
    struct sol_drange value;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_FLOAT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_float_options *)options;

    r = sol_drange_compose(&opts->value_spec, opts->value, &value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_FLOAT__OUT__OUT, &value);
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

    return sol_flow_send_bool_packet(node,
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

static int
constant_location_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_constant_location_options *opts;
    struct sol_location location;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_LOCATION_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_location_options *)options;

    location.lat = opts->latitude;
    location.lon = opts->longitude;
    location.alt = opts->altitude;

    return sol_flow_send_location_packet(node,
        SOL_FLOW_NODE_TYPE_CONSTANT_LOCATION__OUT__OUT, &location);
}

#include "constant-gen.c"
