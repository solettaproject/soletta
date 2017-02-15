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

#include "sol-flow/converter.h"
#include "sol-mainloop.h"
#include "sol-flow-internal.h"

#include <sol-json.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct string_converter {
    struct sol_flow_node *node;
    char *format;
    char *format_prefix;
    char *format_suffix;
};

struct sol_converter_byte {
    unsigned char min;
    unsigned char max;
};

struct sol_converter_boolean {
    bool output_value;
};

struct sol_converter_bits {
    unsigned char last;
    unsigned char output_initialized;
    unsigned char connected_ports;
};

struct sol_converter_string {
    char *string;
};

struct sol_converter_rgb {
    struct sol_rgb output_value;
    bool output_initialized[3];
};

struct sol_converter_direction_vector {
    struct sol_direction_vector output_value;
    bool output_initialized[3];
};

struct sol_converter_location {
    struct sol_location output_value;
    bool output_initialized[3];
};

struct sol_converter_irange_compose {
    unsigned int output_value;
    unsigned char connected_ports : 4;
    unsigned char port_has_value : 4;
};

struct sol_converter_boolean_string {
    char *false_value;
    char *true_value;
};

struct sol_converter_string_blob {
    bool include_null_terminator;
};

static int
irange_min_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);
    mdata->min = value;

    return 0;
}

static int
irange_max_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);
    mdata->max = value;

    return 0;
}

static int
irange_true_range_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange_spec *mdata = data;
    struct sol_irange value;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->min = value.min;
    mdata->max = value.max;

    return 0;
}

static int
drange_min_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange_spec *mdata = data;
    double value;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);
    mdata->min = value;

    return 0;
}

static int
drange_max_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange_spec *mdata = data;
    double value;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);
    mdata->max = value;

    return 0;
}

static int
drange_true_range_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange *mdata = data;
    int r = sol_flow_packet_get_drange(packet, mdata);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
byte_min_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    int r;
    unsigned char value;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);
    mdata->min = value;

    return 0;
}

static int
byte_max_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    int r;
    unsigned char value;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);
    mdata->max = value;

    return 0;
}

static int
boolean_to_irange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_irange *mdata = data;
    const struct sol_flow_node_type_converter_boolean_to_int_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_INT_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_boolean_to_int_options *)options;

    mdata->min = opts->false_value;
    mdata->max = opts->true_value;

    return 0;
}

static int
boolean_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_INT__OUT__OUT,
        in_value ? mdata->max : mdata->min);
}

static int
irange_to_boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_irange_spec *mdata = data;
    const struct sol_flow_node_type_converter_int_to_boolean_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_int_to_boolean_options *)options;

    if (opts->true_range.max >= opts->true_range.min) {
        mdata->min = opts->true_range.min;
        mdata->max = opts->true_range.max;
    } else {
        SOL_WRN("min (%" PRId32 ") should be smaller than max (%" PRId32 ").",
            opts->true_range.min, opts->true_range.max);
        mdata->min = opts->true_range.max;
        mdata->max = opts->true_range.min;
    }

    return 0;
}

static int
irange_to_boolean_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange_spec *mdata = data;
    int32_t in_value;
    int r;
    bool out_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if ((in_value < mdata->min) || (in_value > mdata->max))
        out_value = false;
    else
        out_value = true;

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_BOOLEAN__OUT__OUT,
        out_value);
}

static int
boolean_to_drange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_drange_spec *mdata = data;
    const struct sol_flow_node_type_converter_boolean_to_float_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_FLOAT_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_boolean_to_float_options *)options;

    mdata->min = opts->false_value;
    mdata->max = opts->true_value;

    return 0;
}

static int
boolean_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange_spec *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_FLOAT__OUT__OUT,
        in_value ? mdata->max : mdata->min);
}

static int
drange_to_boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_drange *mdata = data;
    const struct sol_flow_node_type_converter_float_to_boolean_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_float_to_boolean_options *)options;

    if (opts->true_range.max >= opts->true_range.min) {
        mdata->min = opts->true_range.min;
        mdata->max = opts->true_range.max;
    } else {
        SOL_WRN("min %f should be smaller than max %f.",
            opts->true_range.min, opts->true_range.max);
        mdata->min = opts->true_range.max;
        mdata->max = opts->true_range.min;
    }

    return 0;
}

static int
drange_to_boolean_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    double in_value;
    struct sol_drange *mdata = data;
    int r;
    bool out_value;

    r = sol_flow_packet_get_drange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = !((in_value < mdata->min) || (in_value > mdata->max));

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_BOOLEAN__OUT__OUT,
        out_value);
}

static int
boolean_to_byte_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_byte *mdata = data;
    const struct sol_flow_node_type_converter_boolean_to_byte_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_BYTE_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_boolean_to_byte_options *)options;

    mdata->min = opts->false_value;
    mdata->max = opts->true_value;

    return 0;
}

static int
boolean_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_BYTE__OUT__OUT,
        in_value ? mdata->max : mdata->min);
}

static int
byte_to_boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_byte *mdata = data;
    const struct sol_flow_node_type_converter_byte_to_boolean_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_byte_to_boolean_options *)options;

    if (opts->true_max >= opts->true_min) {
        mdata->min = opts->true_min;
        mdata->max = opts->true_max;
    } else {
        SOL_WRN("min %02x should be smaller than max %02x.",
            opts->true_min, opts->true_max);
        mdata->min = opts->true_max;
        mdata->max = opts->true_min;
    }

    return 0;
}

static int
byte_to_boolean_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    int r;
    unsigned char in_value;
    bool out_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = !((in_value < mdata->min) || (in_value > mdata->max));

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_BOOLEAN__OUT__OUT,
        out_value);
}

static int
byte_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange out_value = { 0, 0, 255, 1 };
    int r;
    unsigned char in_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value.val = (int32_t)in_value;

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_INT__OUT__OUT,
        &out_value);
}

static int
byte_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange out_value = { 0, 0, 255, 0 };
    int r;
    unsigned char in_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value.val = (double)in_value;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_FLOAT__OUT__OUT,
        &out_value);
}

static int
irange_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange in_value;
    int r;
    unsigned char out_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < 0)
        out_value = 0;
    else if (in_value.val > 255)
        out_value = 255;
    else
        out_value = (char)in_value.val;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_BYTE__OUT__OUT,
        out_value);
}

static int
drange_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange in_value;
    int r;
    unsigned char out_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < 0)
        out_value = 0;
    else if (in_value.val > 255)
        out_value = 255;
    else
        out_value = (char)in_value.val;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_BYTE__OUT__OUT,
        out_value);
}

static int
irange_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange in_value;
    struct sol_drange out_value;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value.val = (double)in_value.val;
    out_value.min = (double)in_value.min;
    out_value.max = (double)in_value.max;
    out_value.step = (double)in_value.step;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_FLOAT__OUT__OUT,
        &out_value);
}

static int
drange_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange in_value;
    struct sol_irange out_value;
    int r;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value.val = (int32_t)in_value.val;
    out_value.min = (int32_t)in_value.min;
    out_value.max = (int32_t)in_value.max;
    out_value.step = (int32_t)in_value.step;

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_INT__OUT__OUT,
        &out_value);
}

static int
empty_to_boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_boolean *mdata = data;
    const struct sol_flow_node_type_converter_empty_to_boolean_options *opts =
        (const struct sol_flow_node_type_converter_empty_to_boolean_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->output_value = opts->output_value;
    return 0;
}

static int
empty_to_rgb_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_rgb *mdata = data;
    const struct sol_flow_node_type_converter_empty_to_rgb_options *opts =
        (const struct sol_flow_node_type_converter_empty_to_rgb_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_RGB_OPTIONS_API_VERSION,
        -EINVAL);

    *mdata = opts->output_value;
    return 0;
}

static int
empty_to_byte_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_byte *mdata = data;
    const struct sol_flow_node_type_converter_empty_to_byte_options *opts =
        (const struct sol_flow_node_type_converter_empty_to_byte_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_BYTE_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->min = opts->output_value;
    return 0;
}

static int
empty_to_drange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_drange *mdata = data;
    const struct sol_flow_node_type_converter_empty_to_float_options *opts =
        (const struct sol_flow_node_type_converter_empty_to_float_options *)
        options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_FLOAT_OPTIONS_API_VERSION,
        -EINVAL);

    r = sol_drange_compose(&opts->output_value_spec, opts->output_value, mdata);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
empty_to_irange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_irange *mdata = data;
    const struct sol_flow_node_type_converter_empty_to_int_options *opts =
        (const struct sol_flow_node_type_converter_empty_to_int_options *)
        options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_INT_OPTIONS_API_VERSION,
        -EINVAL);

    r = sol_irange_compose(&opts->output_value_spec, opts->output_value, mdata);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
byte_to_empty_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_byte *mdata = data;
    const struct sol_flow_node_type_converter_byte_to_empty_options *opts =
        (const struct sol_flow_node_type_converter_byte_to_empty_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_EMPTY_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->min = opts->range_min;
    mdata->max = opts->range_max;
    return 0;
}

static int
drange_to_empty_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_drange_spec *mdata = data;
    const struct sol_flow_node_type_converter_float_to_empty_options *opts =
        (const struct sol_flow_node_type_converter_float_to_empty_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_EMPTY_OPTIONS_API_VERSION,
        -EINVAL);

    *mdata = opts->range;
    return 0;
}

static int
irange_to_empty_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_irange_spec *mdata = data;
    const struct sol_flow_node_type_converter_int_to_empty_options *opts =
        (const struct sol_flow_node_type_converter_int_to_empty_options *)
        options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_EMPTY_OPTIONS_API_VERSION,
        -EINVAL);

    *mdata = opts->range;
    return 0;
}

static int
empty_to_boolean_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_boolean *mdata = data;

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_BOOLEAN__OUT__OUT,
        mdata->output_value);
}

static int
empty_to_rgb_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_rgb *mdata = data;

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_RGB__OUT__OUT,
        mdata);
}

static int
empty_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_BYTE__OUT__OUT,
        mdata->min);
}

static int
empty_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange *mdata = data;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_FLOAT__OUT__OUT,
        mdata);
}

static int
empty_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange *mdata = data;

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_INT__OUT__OUT,
        mdata);
}

static int
send_empty_packet(struct sol_flow_node *node, uint16_t port)
{
    return sol_flow_send_empty_packet(node, port);
}

static int
pulse_if_true(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (!in_value)
        return 0;

    return send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_EMPTY__OUT__OUT);
}

static int
pulse_if_false(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        return 0;

    return send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_EMPTY__OUT__OUT);
}

static int
byte_to_empty_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    unsigned char in_value;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if ((in_value < mdata->min) || (in_value > mdata->max))
        return 0;

    return send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_EMPTY__OUT__OUT);
}

static int
drange_to_empty_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange_spec *mdata = data;
    double in_value;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if ((in_value < mdata->min) || (in_value > mdata->max))
        return 0;

    return send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_EMPTY__OUT__OUT);
}

static int
irange_to_empty_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange_spec *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if ((in_value < mdata->min) || (in_value > mdata->max))
        return 0;

    return send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_EMPTY__OUT__OUT);
}

static int
drange_to_string_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value;
    struct string_converter *mdata = data;
    struct sol_buffer out = SOL_BUFFER_INIT_EMPTY;

    mdata->node = node;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_slice(&out,
        sol_str_slice_from_str(mdata->format_prefix));
    SOL_INT_CHECK_GOTO(r, < 0, end);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    r = sol_buffer_append_printf(&out, mdata->format, in_value.val);
    SOL_INT_CHECK_GOTO(r, < 0, end);
#pragma GCC diagnostic pop

    r = sol_buffer_append_slice(&out,
        sol_str_slice_from_str(mdata->format_suffix));
    SOL_INT_CHECK_GOTO(r, < 0, end);

    r = sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_STRING__OUT__OUT,
        sol_buffer_get_slice(&out));

end:
    sol_buffer_fini(&out);
    return r;
}

static bool
validate_flags(const char *s)
{
    const char *ptr = s;

    while (*ptr) {
        switch (*ptr) {
        case '#':
        case '0':
        case '-':
        case ' ':
        case '+':
            ptr++;
            continue;
        default:
            return false;
        }
    }

    return true;
}

static bool
validate_drange_conversion_specifier(const char *s)
{
    if ((s[0] == 'e' || s[0] == 'f' || s[0] == 'F' || s[0] == 'g' ||
        s[0] == 'G' || s[0] == 'a' || s[0] == 'A') && s[1] == '\0')
        return true;

    return false;
}

static bool
validate_irange_conversion_specifier(const char *s)
{
    if ((s[0] == 'd' || s[0] == 'i' || s[0] == 'o' || s[0] == 'u' ||
        s[0] == 'x' || s[0] == 'X' || s[0] == 'c') && s[1] == '\0')
        return true;

    return false;
}

static int
drange_to_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_converter *mdata = data;
    const struct sol_flow_node_type_converter_float_to_string_options *opts;
    bool flags = false, valid_conv = false;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_float_to_string_options *)options;

    if (opts->format_prefix)
        mdata->format_prefix = strdup(opts->format_prefix);
    if (opts->format_suffix)
        mdata->format_suffix = strdup(opts->format_suffix);
    if (opts->format_flags) {
        if (!validate_flags(opts->format_flags))
            SOL_WRN("Invalid format flags provided: %s,"
                " proceeding with no flags", opts->format_flags);
        else
            flags = true;
    }
    if (opts->format_conversion_specifier) {
        if (!validate_drange_conversion_specifier
                (opts->format_conversion_specifier))
            SOL_WRN("Invalid format conversion specifier provided: %s,"
                " proceeding with default one (f)",
                opts->format_conversion_specifier);
        else
            valid_conv = true;
    }

    if (opts->format_precision > 0)
        return asprintf(&mdata->format, "%%%s%" PRId32 ".%" PRId32 "%s",
            flags ? opts->format_flags : "",
            opts->format_field_width,
            opts->format_precision,
            valid_conv ? opts->format_conversion_specifier : "f");
    else
        return asprintf(&mdata->format, "%%%s%" PRId32 "%s",
            flags ? opts->format_flags : "",
            opts->format_field_width,
            valid_conv ? opts->format_conversion_specifier : "f");
}

static void
irange_drange_to_string_close(struct sol_flow_node *node, void *data)
{
    struct string_converter *mdata = data;

    free(mdata->format);
    free(mdata->format_prefix);
    free(mdata->format_suffix);
}

static int
irange_to_string_convert(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    struct string_converter *mdata = data;
    struct sol_buffer out = SOL_BUFFER_INIT_EMPTY;

    mdata->node = node;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_slice(&out,
        sol_str_slice_from_str(mdata->format_prefix));
    SOL_INT_CHECK_GOTO(r, < 0, end);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    r = sol_buffer_append_printf(&out, mdata->format, in_value.val);
    SOL_INT_CHECK_GOTO(r, < 0, end);
#pragma GCC diagnostic pop

    r = sol_buffer_append_slice(&out,
        sol_str_slice_from_str(mdata->format_suffix));
    SOL_INT_CHECK_GOTO(r, < 0, end);

    r = sol_flow_send_string_slice_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_STRING__OUT__OUT,
        sol_buffer_get_slice(&out));

end:
    sol_buffer_fini(&out);
    return r;
}

static int
irange_to_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_converter *mdata = data;
    const struct sol_flow_node_type_converter_int_to_string_options *opts;
    bool flags = false, valid_conv = false;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_int_to_string_options *)options;

    if (opts->format_prefix)
        mdata->format_prefix = strdup(opts->format_prefix);
    if (opts->format_suffix)
        mdata->format_suffix = strdup(opts->format_suffix);
    if (opts->format_flags) {
        if (!validate_flags(opts->format_flags))
            SOL_WRN("Invalid format flags provided: %s,"
                " proceeding with no flags", opts->format_flags);
        else
            flags = true;
    }
    if (opts->format_conversion_specifier) {
        if (!validate_irange_conversion_specifier
                (opts->format_conversion_specifier))
            SOL_WRN("Invalid format conversion specifier provided: %s,"
                " proceeding with default one (d)",
                opts->format_conversion_specifier);
        else
            valid_conv = true;
    }

    if (opts->format_precision > 0)
        return asprintf(&mdata->format, "%%%s%" PRId32 ".%" PRId32 "%s",
            flags ? opts->format_flags : "",
            opts->format_field_width,
            opts->format_precision,
            valid_conv ? opts->format_conversion_specifier : "f");
    else
        return asprintf(&mdata->format, "%%%s%" PRId32 "%s",
            flags ? opts->format_flags : "",
            opts->format_field_width,
            valid_conv ? opts->format_conversion_specifier : "f");
}

static int
boolean_to_string_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_boolean_string *mdata = data;
    const struct sol_flow_node_type_converter_boolean_to_string_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_STRING_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_boolean_to_string_options *)options;

    SOL_NULL_CHECK_MSG(opts->false_value, -EINVAL, "A valid string is required as 'false_value'");
    SOL_NULL_CHECK_MSG(opts->true_value, -EINVAL, "A valid string is required as 'true_value'");

    mdata->false_value = strdup(opts->false_value);
    SOL_NULL_CHECK(mdata->false_value, -ENOMEM);

    mdata->true_value = strdup(opts->true_value);
    SOL_NULL_CHECK_GOTO(mdata->true_value, string_error);

    return 0;

string_error:
    free(mdata->false_value);
    return -ENOMEM;
}

static void
boolean_to_string_close(struct sol_flow_node *node, void *data)
{
    struct sol_converter_boolean_string *mdata = data;

    free(mdata->false_value);
    free(mdata->true_value);
}

static int
set_string(const struct sol_flow_packet *packet, char **string)
{
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(*string);
    *string = strdup(in_value);

    if (!(*string))
        return -ENOMEM;

    return 0;
}

static int
string_false_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_boolean_string *mdata = data;

    return set_string(packet, &mdata->false_value);
}

static int
string_true_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_boolean_string *mdata = data;

    return set_string(packet, &mdata->true_value);
}

static int
boolean_to_string_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_boolean_string *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BOOLEAN_TO_STRING__OUT__OUT,
        in_value ? mdata->true_value : mdata->false_value);
}

#define BYTE_STR_SIZE (5)

static int
byte_to_string_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    unsigned char in_value;
    int r;
    char out_value[BYTE_STR_SIZE];

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    snprintf(out_value, BYTE_STR_SIZE, "0x%02x", in_value);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_STRING__OUT__OUT,
        out_value);
}

#undef BYTE_STR_SIZE

static int
empty_to_string_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_string *mdata = data;
    const struct sol_flow_node_type_converter_empty_to_string_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_STRING_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_converter_empty_to_string_options *)
        options;

    mdata->string = strdup(opts->output_value);
    SOL_NULL_CHECK(mdata->string, -ENOMEM);

    return 0;
}

static void
empty_to_string_close(struct sol_flow_node *node, void *data)
{
    struct sol_converter_string *mdata = data;

    free(mdata->string);
}

static int
empty_string_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string *mdata = data;

    return set_string(packet, &mdata->string);
}

static int
empty_to_string_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string *mdata = data;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_EMPTY_TO_STRING__OUT__OUT,
        mdata->string);
}

static int
empty_boolean_output_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_boolean *mdata = data;
    int r = sol_flow_packet_get_bool(packet, &mdata->output_value);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
empty_rgb_output_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_rgb *mdata = data;
    int r = sol_flow_packet_get_rgb(packet, mdata);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
empty_byte_min_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    int r = sol_flow_packet_get_byte(packet, &mdata->min);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
empty_byte_max_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_byte *mdata = data;
    int r = sol_flow_packet_get_byte(packet, &mdata->max);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
empty_drange_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange *mdata = data;
    int r = sol_flow_packet_get_drange(packet, mdata);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
irange_empty_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange_spec *mdata = data;
    struct sol_irange value;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->min = value.min;
    mdata->max = value.max;

    return 0;
}

static int
empty_irange_value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange *mdata = data;
    int r = sol_flow_packet_get_irange(packet, mdata);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
byte_to_bits_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_bits *mdata = data;
    int i;
    unsigned char in_val, last_bit, next_bit;

    int r = sol_flow_packet_get_byte(packet, &in_val);

    SOL_INT_CHECK(r, < 0, r);

    for (i = 0; i <= 7; i++) {
        last_bit = (mdata->last >> i) & 1;
        next_bit = (in_val >> i) & 1;

        if (mdata->output_initialized && last_bit == next_bit)
            continue;

        sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_CONVERTER_BYTE_TO_BITS__OUT__OUT_0 + i, next_bit);
    }

    mdata->last = in_val;
    mdata->output_initialized = 1;

    return 0;
}

static int
string_to_boolean_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *in_value;
    int r;
    bool out_value = false;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (!strcasecmp(in_value, "true"))
        out_value = true;
    else if (strcasecmp(in_value, "false")) {
        return sol_flow_send_error_packet
                   (node, EINVAL, "String %s isn't a valid boolean", in_value);
    }

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_BOOLEAN__OUT__OUT,
        out_value);
}

static int
string_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *in_value;
    char *endptr;
    int r;
    unsigned char out_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    errno = 0;
    out_value = strtol(in_value, &endptr, 16);
    if (errno) {
        SOL_WRN("Failed to convert string to byte %s: %d", in_value, errno);
        return -errno;
    }

    // no error, but no conversion done
    if (in_value == endptr) {
        SOL_WRN("Failed to convert string to byte %s", in_value);
        return -EINVAL;
    }

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_BYTE__OUT__OUT,
        out_value);
}

static int
string_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    double out_value;
    const char *in_value;
    char *endptr;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = sol_util_strtod_n(in_value, &endptr, -1, false);
    if (errno) {
        SOL_WRN("Failed to convert string to float %s: %d", in_value, errno);
        return -errno;
    }

    if (in_value == endptr) {
        SOL_WRN("Failed to convert string to float %s", in_value);
        return -EINVAL;
    }

    return sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_FLOAT__OUT__OUT,
        out_value);
}

static int
string_to_empty_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_EMPTY__OUT__OUT);
}

static int
string_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *in_value;
    char *endptr;
    uint32_t out_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    errno = 0;
    out_value = strtol(in_value, &endptr, 10);
    if (errno) {
        SOL_WRN("Failed to convert string to int %s: %d", in_value, errno);
        return -errno;
    }

    if (in_value == endptr) {
        SOL_WRN("Failed to convert string to int %s", in_value);
        return -EINVAL;
    }

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_INT__OUT__OUT,
        out_value);
}

static int
byte_to_rgb_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_rgb *mdata = data;

    mdata->output_value.red_max = 255;
    mdata->output_value.green_max = 255;
    mdata->output_value.blue_max = 255;

    return 0;
}

static int
irange_to_rgb_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_rgb *mdata = data;
    const struct sol_flow_node_type_converter_int_to_rgb_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_RGB_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_int_to_rgb_options *)options;

    mdata->output_value.red_max = opts->red_max;
    mdata->output_value.green_max = opts->green_max;
    mdata->output_value.blue_max = opts->blue_max;

    return 0;
}

static int
drange_to_rgb_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_rgb *mdata = data;
    const struct sol_flow_node_type_converter_float_to_rgb_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_RGB_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_float_to_rgb_options *)options;

    mdata->output_value.red_max = opts->red_max;
    mdata->output_value.green_max = opts->green_max;
    mdata->output_value.blue_max = opts->blue_max;

    return 0;
}

static int
rgb_convert(struct sol_flow_node *node, void *data, uint16_t port, uint32_t val)
{
    struct sol_converter_rgb *mdata = data;
    int i;

    mdata->output_initialized[port] = true;
    if (port == 0)
        mdata->output_value.red = val > mdata->output_value.red_max ?
            mdata->output_value.red_max : val;
    else if (port == 1)
        mdata->output_value.green = val > mdata->output_value.green_max ?
            mdata->output_value.green_max : val;
    else
        mdata->output_value.blue = val > mdata->output_value.blue_max ?
            mdata->output_value.blue_max : val;

    for (i = 0; i < 3; i++) {
        if (!mdata->output_initialized[i])
            return 0;
    }

    return sol_flow_send_rgb_packet(node, 0, &mdata->output_value);
}

static int
byte_to_rgb_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    unsigned char in_val;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    return rgb_convert(node, data, port, in_val);
}

static inline uint32_t
rgb_get_port_max(void *data, uint16_t port)
{
    struct sol_converter_rgb *mdata = data;

    if (port == 0)
        return mdata->output_value.red_max;
    if (port == 1)
        return mdata->output_value.green_max;
    return mdata->output_value.blue_max;
}

static int
irange_to_rgb_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange in_val;
    uint32_t val;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    if (in_val.val < 0) {
        SOL_WRN("Color component must to be a not negative value");
        return -EINVAL;
    }

    if (in_val.max <= 0) {
        SOL_WRN("Max value for color component must to be a positive value");
        return -EINVAL;
    }

    val = in_val.val * rgb_get_port_max(data, port) / in_val.max;

    return rgb_convert(node, data, port, val);
}

static int
drange_to_rgb_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange in_val;
    uint32_t val;
    int r;

    r = sol_flow_packet_get_drange(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    if (isless(in_val.val, 0)) {
        SOL_WRN("Color component must to be a not negative value");
        return -EINVAL;
    }

    if (islessequal(in_val.max, 0)) {
        SOL_WRN("Max value for color component must to be a positive value");
        return -EINVAL;
    }

    val = in_val.val * rgb_get_port_max(data, port) / in_val.max;

    return rgb_convert(node, data, port, val);
}

static int
rgb_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_rgb rgb;
    int r;

    r = sol_flow_packet_get_rgb(packet, &rgb);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_RGB_TO_BYTE__OUT__RED, rgb.red);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_RGB_TO_BYTE__OUT__GREEN, rgb.green);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_RGB_TO_BYTE__OUT__BLUE, rgb.blue);
}

#define RGB_SEND(_type, _type_m, _component, _component_m) \
    do { \
        out.val = rgb._component; \
        out.max = rgb._component ## _max; \
        r = sol_flow_send_ ## _type ## _packet(node, \
            SOL_FLOW_NODE_TYPE_CONVERTER_RGB_TO_ ## _type_m ## __OUT__ ## _component_m, \
            &out); \
        SOL_INT_CHECK(r, < 0, r); \
    } while (0)

static int
rgb_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_rgb rgb;
    struct sol_irange out;
    int r;

    r = sol_flow_packet_get_rgb(packet, &rgb);
    SOL_INT_CHECK(r, < 0, r);

    out.min = 0;
    out.step = 1;

    RGB_SEND(irange, INT, red, RED);
    RGB_SEND(irange, INT, green, GREEN);
    RGB_SEND(irange, INT, blue, BLUE);

    return r;
}

static int
rgb_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_rgb rgb;
    struct sol_drange out;
    int r;

    r = sol_flow_packet_get_rgb(packet, &rgb);
    SOL_INT_CHECK(r, < 0, r);

    out.min = 0;
    out.step = 1;

    RGB_SEND(drange, FLOAT, red, RED);
    RGB_SEND(drange, FLOAT, green, GREEN);
    RGB_SEND(drange, FLOAT, blue, BLUE);

    return r;
}

#undef RGB_SEND

static int
byte_to_direction_vector_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_direction_vector *mdata = data;

    mdata->output_value.max = 255;
    mdata->output_value.min = 0;

    return 0;
}

static int
irange_to_direction_vector_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_direction_vector *mdata = data;
    const struct sol_flow_node_type_converter_int_to_direction_vector_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_INT_TO_DIRECTION_VECTOR_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_int_to_direction_vector_options *)options;

    mdata->output_value.max = opts->out_range.max;
    mdata->output_value.min = opts->out_range.min;

    return 0;
}

static int
drange_to_direction_vector_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_direction_vector *mdata = data;
    const struct sol_flow_node_type_converter_float_to_direction_vector_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_DIRECTION_VECTOR_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_converter_float_to_direction_vector_options *)options;

    mdata->output_value.max = opts->out_range.max;
    mdata->output_value.min = opts->out_range.min;

    return 0;
}

static int
direction_vector_convert(struct sol_flow_node *node, void *data, uint16_t port, double val)
{
    struct sol_converter_direction_vector *mdata = data;
    unsigned i;

    if (val > mdata->output_value.max)
        val = mdata->output_value.max;
    if (val < mdata->output_value.min)
        val = mdata->output_value.min;

    mdata->output_initialized[port] = true;
    if (port == 0) {
        mdata->output_value.x = val;
    } else if (port == 1) {
        mdata->output_value.y = val;
    } else {
        mdata->output_value.z = val;
    }

    for (i = 0; i < sol_util_array_size(mdata->output_initialized); i++) {
        if (!mdata->output_initialized[i])
            return 0;
    }

    return sol_flow_send_direction_vector_packet(node, 0, &mdata->output_value);
}

static int
byte_to_direction_vector_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    unsigned char in_val;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    return direction_vector_convert(node, data, port, in_val);
}

static int
irange_to_direction_vector_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_direction_vector *mdata = data;
    struct sol_irange in_val;
    uint32_t val;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    val = in_val.val *
        (mdata->output_value.max - mdata->output_value.min) /
        (in_val.max - in_val.min);

    return direction_vector_convert(node, data, port, val);
}

static int
drange_to_direction_vector_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_direction_vector *mdata = data;
    struct sol_drange in_val;
    uint32_t val;
    int r;

    r = sol_flow_packet_get_drange(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    val = in_val.val *
        (mdata->output_value.max - mdata->output_value.min) /
        (in_val.max - in_val.min);

    return direction_vector_convert(node, data, port, val);
}

static int
direction_vector_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_direction_vector direction_vector;
    unsigned char out_x, out_y, out_z;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &direction_vector);
    SOL_INT_CHECK(r, < 0, r);

#define VALUE_CONSTRAIN(_val, _out_val) \
    do { \
        if (_val < 0) \
            _out_val = 0; \
        else if (_val > 255) \
            _out_val = 255; \
        else \
            _out_val = (unsigned char)_val; \
    } while (0)

    VALUE_CONSTRAIN(direction_vector.x, out_x);
    VALUE_CONSTRAIN(direction_vector.y, out_y);
    VALUE_CONSTRAIN(direction_vector.z, out_z);
#undef VALUE_CONSTRAIN

    r = sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_DIRECTION_VECTOR_TO_BYTE__OUT__X, out_x);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_DIRECTION_VECTOR_TO_BYTE__OUT__Y, out_y);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_DIRECTION_VECTOR_TO_BYTE__OUT__Z, out_z);
}

#define DIRECTION_VECTOR_SEND(_type, _type_m, _component, _component_m) \
    do { \
        out.val = direction_vector._component; \
        r = sol_flow_send_ ## _type ## _packet(node, \
            SOL_FLOW_NODE_TYPE_CONVERTER_DIRECTION_VECTOR_TO_ ## _type_m ## __OUT__ ## _component_m, \
            &out); \
        SOL_INT_CHECK(r, < 0, r); \
    } while (0)

static int
direction_vector_to_irange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_direction_vector direction_vector;
    struct sol_irange out;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &direction_vector);
    SOL_INT_CHECK(r, < 0, r);

    out.step = 1;
    out.min = direction_vector.min < INT32_MIN ? INT32_MIN : direction_vector.min;
    out.max = direction_vector.max > INT32_MAX ? INT32_MAX : direction_vector.max;

    DIRECTION_VECTOR_SEND(irange, INT, x, X);
    DIRECTION_VECTOR_SEND(irange, INT, y, Y);
    DIRECTION_VECTOR_SEND(irange, INT, z, Z);

    return r;
}

static int
direction_vector_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_direction_vector direction_vector;
    struct sol_drange out;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &direction_vector);
    SOL_INT_CHECK(r, < 0, r);

    out.min = direction_vector.min;
    out.max = direction_vector.max;
    out.step = DBL_MIN;

    DIRECTION_VECTOR_SEND(drange, FLOAT, x, X);
    DIRECTION_VECTOR_SEND(drange, FLOAT, y, Y);
    DIRECTION_VECTOR_SEND(drange, FLOAT, z, Z);

    return r;
}

#undef DIRECTION_VECTOR_SEND

static int
irange_compose_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct sol_converter_irange_compose *mdata = data;
    int idx = port - SOL_FLOW_NODE_TYPE_CONVERTER_INT_COMPOSE__IN__IN_0;

    mdata->connected_ports |= 1 << idx;
    return 0;
}

static int
irange_compose(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_irange_compose *mdata = data;
    struct sol_irange out_val = { .min = INT32_MIN, .max = INT32_MAX, .step = 1 };
    int idx = port - SOL_FLOW_NODE_TYPE_CONVERTER_INT_COMPOSE__IN__IN_0;
    unsigned int tmp;
    unsigned char in_val;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    mdata->port_has_value |= 1 << idx;

    tmp = mdata->output_value & ~(0xffu << (idx * CHAR_BIT));
    tmp |= in_val << (idx * CHAR_BIT);
    mdata->output_value = tmp;

    if (mdata->port_has_value != mdata->connected_ports)
        return 0;

    out_val.val = (int)mdata->output_value;
    return sol_flow_send_irange_packet(node, SOL_FLOW_NODE_TYPE_CONVERTER_INT_COMPOSE__OUT__OUT, &out_val);
}

static int
irange_decompose(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange in;
    unsigned int value;
    int r, i;

    r = sol_flow_packet_get_irange(packet, &in);
    SOL_INT_CHECK(r, < 0, r);

    value = (unsigned)in.val;
    for (i = 0; i < 4; i++) {
        uint16_t out_port[] = {
            SOL_FLOW_NODE_TYPE_CONVERTER_INT_DECOMPOSE__OUT__OUT_0,
            SOL_FLOW_NODE_TYPE_CONVERTER_INT_DECOMPOSE__OUT__OUT_1,
            SOL_FLOW_NODE_TYPE_CONVERTER_INT_DECOMPOSE__OUT__OUT_2,
            SOL_FLOW_NODE_TYPE_CONVERTER_INT_DECOMPOSE__OUT__OUT_3
        };
        unsigned char byte;
        byte = value & 0xff;
        value >>= CHAR_BIT;
        r = sol_flow_send_byte_packet(node, out_port[i], byte);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
error_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *msg;
    int code, r;

    r = sol_flow_packet_get_error(packet, &code, &msg);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_ERROR__OUT__CODE, code);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_ERROR__OUT__MESSAGE, msg);
}

static int
bits_to_byte_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct sol_converter_bits *mdata = data;
    int idx = port - SOL_FLOW_NODE_TYPE_CONVERTER_BITS_TO_BYTE__IN__IN_0;

    mdata->connected_ports |= 1 << idx;
    return 0;
}

#define SET_BIT(_byte, _bit_index, _bit_value) \
    do { \
        _byte = (~(1 << _bit_index) & _byte) | (_bit_value << _bit_index); \
    } while (0)

static int
bits_to_byte_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_bits *mdata = data;
    int idx = port - SOL_FLOW_NODE_TYPE_CONVERTER_BITS_TO_BYTE__IN__IN_0;
    bool in_val;

    int r = sol_flow_packet_get_bool(packet, &in_val);

    SOL_INT_CHECK(r, < 0, r);

    if ((mdata->output_initialized >> idx) & 1) {
        if ((mdata->last >> idx) == in_val)
            return 0;
    } else
        mdata->output_initialized |= 1 << idx;

    SET_BIT(mdata->last, idx, in_val);

    if (mdata->output_initialized != mdata->connected_ports)
        return 0;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BITS_TO_BYTE__OUT__OUT, mdata->last);
}

#undef SET_BIT

static struct sol_blob *
get_string_convert_blob(struct sol_converter_string_blob *mdata, const struct sol_flow_packet *packet)
{
    struct sol_blob *blob;
    const char *str;
    char *mem;
    int ret;
    size_t len;

    ret = sol_flow_packet_get_string(packet, &str);
    SOL_INT_CHECK(ret, < 0, NULL);

    len = strlen(str);
    if (mdata->include_null_terminator)
        len++;

    if (len == 0)
        mem = NULL;
    else {
        mem = malloc(len);
        SOL_NULL_CHECK(mem, NULL);
        memcpy(mem, str, len);
    }

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, mem, len);
    if (!blob) {
        free(mem);
        return NULL;
    }

    return blob;
}

static int
string_to_blob_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string_blob *mdata = data;
    struct sol_blob *blob;
    int ret;

    blob = get_string_convert_blob(mdata, packet);
    SOL_NULL_CHECK(blob, -ENOMEM);

    ret = sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_BLOB__OUT__OUT, blob);

    sol_blob_unref(blob);

    return ret;
}

static int
string_to_blob_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_string_blob *mdata = data;
    const struct sol_flow_node_type_converter_string_to_blob_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_BLOB_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_converter_string_to_blob_options *)
        options;
    mdata->include_null_terminator = opts->include_null_terminator;
    return 0;
}

static int
blob_to_string_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    char *mem;
    struct sol_blob *blob;
    size_t len;
    int ret;

    ret = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(ret, < 0, -EINVAL);

    if (blob->size == 0)
        return sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_CONVERTER_BLOB_TO_STRING__OUT__OUT, "");

    mem = memchr(blob->mem, '\0', blob->size);
    if (mem)
        len = mem - (char *)blob->mem;
    else
        len = blob->size;

    mem = malloc(len + 1);
    SOL_NULL_CHECK(mem, -ENOMEM);
    memcpy(mem, blob->mem, len);
    mem[len] = '\0';

    return sol_flow_send_string_take_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BLOB_TO_STRING__OUT__OUT, mem);
}

static int
timestamp_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_converter_string *mdata = data;
    const struct sol_flow_node_type_converter_string_to_timestamp_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_TIMESTAMP_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_converter_string_to_timestamp_options *)
        options;

    mdata->string = strdup(opts->format);
    if (!mdata->string)
        return -ENOMEM;

    return 0;
}

static void
timestamp_close(struct sol_flow_node *node, void *data)
{
    struct sol_converter_string *mdata = data;

    free(mdata->string);
}

static int
timestamp_to_string_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string *mdata = data;
    struct timespec in_value;
    struct tm time_tm;
    int r;
    char out_value[128];

    r = sol_flow_packet_get_timestamp(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    tzset();
    if (!localtime_r(&in_value.tv_sec, &time_tm))
        goto timestamp_error;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

    r = strftime(out_value, sizeof(out_value), mdata->string,
        &time_tm);

#pragma GCC diagnostic pop

    if (!r)
        goto timestamp_error;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_TIMESTAMP_TO_STRING__OUT__OUT,
        out_value);

timestamp_error:
    sol_flow_send_error_packet(node, EINVAL, "Failed to convert timestamp");
    return 0;
}

static int
string_to_timestamp_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string *mdata = data;
    struct timespec out_value;
    const char *in_value;
    char *timestamp_str;
    struct tm time_tm = { 0 };
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    timestamp_str = strptime(in_value, mdata->string, &time_tm);
    SOL_NULL_CHECK_GOTO(timestamp_str, timestamp_error);

    /* Let mktime() figure out if we are under DST or not (don't
     * assume we're not, as the variable initialization does) */
    time_tm.tm_isdst = -1;

    out_value.tv_sec = mktime(&time_tm);
    if (out_value.tv_sec < 0)
        goto timestamp_error;
    out_value.tv_nsec = 0;

    return sol_flow_send_timestamp_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_TIMESTAMP__OUT__OUT,
        &out_value);

timestamp_error:
    sol_flow_send_error_packet(node, EINVAL,
        "Failed to convert string to timestamp");
    return 0;
}

static bool
json_validate(struct sol_blob *blob, enum sol_json_type type)
{
    struct sol_json_scanner scanner;
    struct sol_str_slice trimmed_str;

    trimmed_str = sol_str_slice_trim(sol_str_slice_from_blob(blob));

    sol_json_scanner_init(&scanner, trimmed_str.data, trimmed_str.len);
    return sol_json_is_valid_type(&scanner, type);
}

static int
blob_to_json_object_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_blob *blob;
    int ret;

    ret = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(ret, < 0, -EINVAL);

    if (!json_validate(blob, SOL_JSON_TYPE_OBJECT_START))
        return sol_flow_send_error_packet(node, EINVAL,
            "Blob isn't a valid JSON Object");

    return sol_flow_send_json_object_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BLOB_TO_JSON_OBJECT__OUT__OUT, blob);
}

static int
json_object_to_blob_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_blob *blob;
    int ret;

    ret = sol_flow_packet_get_json_object(packet, &blob);
    SOL_INT_CHECK(ret, < 0, -EINVAL);

    ret = sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_JSON_OBJECT_TO_BLOB__OUT__OUT, blob);

    return ret;
}

static int
blob_to_json_array_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_blob *blob;
    int ret;

    ret = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(ret, < 0, -EINVAL);

    if (!json_validate(blob, SOL_JSON_TYPE_ARRAY_START))
        return sol_flow_send_error_packet(node, EINVAL,
            "Blob isn't a valid JSON Array");

    return sol_flow_send_json_array_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_BLOB_TO_JSON_ARRAY__OUT__OUT, blob);
}

static int
json_array_to_blob_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_blob *blob;
    int ret;

    ret = sol_flow_packet_get_json_array(packet, &blob);
    SOL_INT_CHECK(ret, < 0, -EINVAL);

    ret = sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_JSON_ARRAY_TO_BLOB__OUT__OUT, blob);

    return ret;
}

static int
string_to_json_object_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string_blob *mdata = data;
    struct sol_blob *blob;
    int ret;

    blob = get_string_convert_blob(mdata, packet);
    SOL_NULL_CHECK(blob, -ENOMEM);

    if (!json_validate(blob, SOL_JSON_TYPE_OBJECT_START)) {
        sol_blob_unref(blob);
        return sol_flow_send_error_packet(node, EINVAL,
            "Blob isn't a valid JSON Object");
    }

    ret = sol_flow_send_json_object_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_JSON_OBJECT__OUT__OUT, blob);

    sol_blob_unref(blob);

    return ret;
}

static int
string_to_json_array_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_string_blob *mdata = data;
    struct sol_blob *blob;
    int ret;

    blob = get_string_convert_blob(mdata, packet);
    SOL_NULL_CHECK(blob, -ENOMEM);

    if (!json_validate(blob, SOL_JSON_TYPE_ARRAY_START)) {
        sol_blob_unref(blob);
        return sol_flow_send_error_packet(node, EINVAL,
            "Blob isn't a valid JSON Array");
    }

    ret = sol_flow_send_json_array_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_STRING_TO_JSON_ARRAY__OUT__OUT, blob);

    sol_blob_unref(blob);

    return ret;
}

static int
drange_to_location_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_converter_location *mdata = data;
    double in_val;
    unsigned int i;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &in_val);
    SOL_INT_CHECK(r, < 0, r);

    mdata->output_initialized[port] = true;
    if (port == 0) {
        mdata->output_value.lat = in_val;
    } else if (port == 1) {
        mdata->output_value.lon = in_val;
    } else {
        mdata->output_value.alt = in_val;
    }

    for (i = 0; i < sol_util_array_size(mdata->output_initialized); i++) {
        if (!mdata->output_initialized[i])
            return 0;
    }

    return sol_flow_send_location_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_FLOAT_TO_LOCATION__OUT__OUT,
        &mdata->output_value);
}

static int
location_to_drange_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_location location;
    int r;

    r = sol_flow_packet_get_location(packet, &location);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_LOCATION_TO_FLOAT__OUT__LATITUDE,
        location.lat);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_LOCATION_TO_FLOAT__OUT__LONGITUDE,
        location.lon);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_CONVERTER_LOCATION_TO_FLOAT__OUT__ALTITUDE,
        location.alt);
}

#include "converter-gen.c"
