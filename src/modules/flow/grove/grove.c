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

#include <errno.h>
#include <math.h>

#include "sol-flow/grove.h"

#include "sol-flow-internal.h"
#include "sol-flow-static.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "sol-flow/aio.h"

// ################################ Rotary sensor nodes

#define ROTARY_CONVERTER_NODE_IDX 0
#define ROTARY_AIO_READER_NODE_IDX 1

struct rotary_converter_data {
    int angular_range;
    int input_range;
};

static int
rotary_child_opts_set(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_rotary_sensor_options *container_opts = (struct sol_flow_node_type_grove_rotary_sensor_options *)opts;

    if (child_index == ROTARY_CONVERTER_NODE_IDX) {
        struct sol_flow_node_type_grove_rotary_converter_options *converter_opts =
            (struct sol_flow_node_type_grove_rotary_converter_options *)child_opts;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
            SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER_OPTIONS_API_VERSION,
            -EINVAL);
        converter_opts->angular_range = container_opts->angular_range;
        converter_opts->input_range_mask = container_opts->mask;
    } else if (child_index == ROTARY_AIO_READER_NODE_IDX) {
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
            SOL_FLOW_NODE_TYPE_AIO_READER_OPTIONS_API_VERSION,
            -EINVAL);
        reader_opts->raw = container_opts->raw;
        reader_opts->pin = container_opts->pin ? strdup(container_opts->pin) : NULL;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }

    return 0;
}

static void
grove_rotary_sensor_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;
    const struct sol_flow_node_type **aio_reader, **ctl;

    static struct sol_flow_static_node_spec nodes[] = {
        { NULL, "rotary-converter", NULL },
        { NULL, "aio-reader", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { 1, SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__DEG },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAD },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_out = exported_out,
        .child_opts_set = rotary_child_opts_set,
    };

    if (sol_flow_get_node_type("aio", SOL_FLOW_NODE_TYPE_AIO_READER, &aio_reader) < 0) {
        *current = NULL;
        return;
    }

    ctl = &SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER;
    if ((*ctl)->init_type)
        (*ctl)->init_type();

    nodes[0].type = *ctl;
    nodes[1].type = *aio_reader;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

static void
rotary_sensor_init_type(void)
{
    grove_rotary_sensor_new_type(&SOL_FLOW_NODE_TYPE_GROVE_ROTARY_SENSOR);
}

static int
rotary_converter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct rotary_converter_data *mdata = data;
    const struct sol_flow_node_type_grove_rotary_converter_options *opts =
        (const struct sol_flow_node_type_grove_rotary_converter_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER_OPTIONS_API_VERSION, -EINVAL);

    mdata->angular_range = opts->angular_range;
    mdata->input_range = 1 << opts->input_range_mask;

    return 0;
}

static int
rotary_converter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    struct sol_drange degrees, radians;
    struct rotary_converter_data *mdata = data;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    degrees.step = DBL_MIN;
    degrees.min = 0;
    degrees.max = mdata->angular_range;
    degrees.val = (float)in_value.val * (float)mdata->angular_range /
        mdata->input_range;

    radians = degrees;
    radians.val *= M_PI / 180.0;
    radians.max *= M_PI / 180.0;

    sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__DEG,
        &degrees);
    sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAD,
        &radians);
    sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAW,
        &in_value);

    return 0;
}

// ################################ Light sensor nodes

#define LIGHT_CONVERTER_NODE_IDX 0
#define LIGHT_AIO_READER_NODE_IDX 1

struct light_converter_data {
    int input_range;
};

static int
light_child_opts_set(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_light_sensor_options *container_opts = (struct sol_flow_node_type_grove_light_sensor_options *)opts;

    if (child_index == LIGHT_CONVERTER_NODE_IDX) {
        struct sol_flow_node_type_grove_light_converter_options *converter_opts = (struct sol_flow_node_type_grove_light_converter_options *)child_opts;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
            SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER_OPTIONS_API_VERSION,
            -EINVAL);
        converter_opts->input_range_mask = container_opts->mask;
    } else if (child_index == LIGHT_AIO_READER_NODE_IDX) {
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
            SOL_FLOW_NODE_TYPE_AIO_READER_OPTIONS_API_VERSION,
            -EINVAL);
        reader_opts->raw = container_opts->raw;
        reader_opts->pin = container_opts->pin ? strdup(container_opts->pin) : NULL;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }

    return 0;
}

static void
grove_light_sensor_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;
    const struct sol_flow_node_type **aio_reader, **ctl;

    static struct sol_flow_static_node_spec nodes[] = {
        { NULL, "light-converter", NULL },
        { NULL, "aio-reader", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { 1, SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, 0, SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__LUX },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_out = exported_out,
        .child_opts_set = light_child_opts_set,
    };

    if (sol_flow_get_node_type("aio", SOL_FLOW_NODE_TYPE_AIO_READER, &aio_reader) < 0) {
        *current = NULL;
        return;
    }

    ctl = &SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER;
    if ((*ctl)->init_type)
        (*ctl)->init_type();

    nodes[0].type = *ctl;
    nodes[1].type = *aio_reader;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

static void
light_sensor_init_type(void)
{
    grove_light_sensor_new_type(&SOL_FLOW_NODE_TYPE_GROVE_LIGHT_SENSOR);
}

static int
light_converter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct light_converter_data *mdata = data;
    const struct sol_flow_node_type_grove_light_converter_options *opts =
        (const struct sol_flow_node_type_grove_light_converter_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER_OPTIONS_API_VERSION, -EINVAL);

    mdata->input_range = 1 << opts->input_range_mask;

    return 0;
}

static int
light_converter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    float a;
    struct light_converter_data *mdata = data;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    // The following calculations follow the exponential best fit
    // (found using least squares) for the values suggested for LUX
    // on the table found on Grove Starter Kit for Arduino booklet
    // Least squares best fit: 0.152262 e^(0.00782118 x)
    // First row below maps input_range to 0-1023 range, used on booklet table.
    a = (float)in_value.val * 1023 / mdata->input_range;
    a = 0.152262 * exp(0.00782118 * (a));
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__LUX,
        a);
    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__RAW,
        in_value.val);

    return 0;
}

// ################################ Temperature sensor nodes

#define TEMPERATURE_CONVERTER_NODE_IDX 0
#define TEMPERATURE_AIO_READER_NODE_IDX 1

struct temperature_converter_data {
    int thermistor_constant;
    int input_range;
    int resistance;
    int thermistor_resistance;
    float reference_temperature;
};

static int
temperature_converter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct temperature_converter_data *mdata = data;
    const struct sol_flow_node_type_grove_temperature_converter_options *opts =
        (const struct sol_flow_node_type_grove_temperature_converter_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER_OPTIONS_API_VERSION, -EINVAL);

    mdata->thermistor_constant = opts->thermistor_constant;
    mdata->input_range = 1 << opts->input_range_mask;
    mdata->resistance = opts->resistance;
    mdata->reference_temperature = opts->reference_temperature;
    mdata->thermistor_resistance = opts->thermistor_resistance;

    return 0;
}

static int
temperature_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    float resistance, temperature_kelvin;
    struct temperature_converter_data *mdata = data;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    resistance = (float)(mdata->input_range - in_value.val) * mdata->resistance / in_value.val;
    temperature_kelvin = 1 / (log(resistance / mdata->thermistor_resistance) / mdata->thermistor_constant + 1 / mdata->reference_temperature);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__KELVIN,
        temperature_kelvin);
    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__RAW,
        in_value.val);

    return 0;
}

static int
temperature_child_opts_set(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_thermometer_options *container_opts = (struct sol_flow_node_type_grove_thermometer_options *)opts;

    if (child_index == TEMPERATURE_CONVERTER_NODE_IDX) {
        struct sol_flow_node_type_grove_temperature_converter_options *converter_opts =
            (struct sol_flow_node_type_grove_temperature_converter_options *)child_opts;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
            SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER_OPTIONS_API_VERSION,
            -EINVAL);
        converter_opts->thermistor_constant = container_opts->thermistor_constant;
        converter_opts->input_range_mask = container_opts->mask;
        converter_opts->resistance = container_opts->resistance;
        converter_opts->reference_temperature = container_opts->reference_temperature;
        converter_opts->thermistor_resistance = container_opts->thermistor_resistance;
    } else if (child_index == TEMPERATURE_AIO_READER_NODE_IDX) {
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
            SOL_FLOW_NODE_TYPE_AIO_READER_OPTIONS_API_VERSION,
            -EINVAL);
        reader_opts->raw = container_opts->raw;
        reader_opts->pin = container_opts->pin ? strdup(container_opts->pin) : NULL;
        reader_opts->pin = container_opts->pin;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }

    return 0;
}

static void
grove_thermometer_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;
    const struct sol_flow_node_type **aio_reader, **ctl;

    static struct sol_flow_static_node_spec nodes[] = {
        { NULL, "temperature-converter", NULL },
        { NULL, "aio-reader", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { 1, SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__KELVIN },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_out = exported_out,
        .child_opts_set = temperature_child_opts_set,
    };

    if (sol_flow_get_node_type("aio", SOL_FLOW_NODE_TYPE_AIO_READER, &aio_reader) < 0) {
        *current = NULL;
        return;
    }

    ctl = &SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER;
    if ((*ctl)->init_type)
        (*ctl)->init_type();

    nodes[0].type = *ctl;
    nodes[1].type = *aio_reader;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

static void
temperature_init_type(void)
{
    grove_thermometer_new_type(&SOL_FLOW_NODE_TYPE_GROVE_THERMOMETER);
}

#include "grove-gen.c"
