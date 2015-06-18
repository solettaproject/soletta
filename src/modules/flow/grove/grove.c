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

#include <sol-util.h>
#include <errno.h>
#include <math.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-grove");

#include "sol-flow-internal.h"

#include "aio-gen.h"

#include "grove-gen.h"

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

    mdata->thermistor_constant = opts->thermistor_constant.val;
    mdata->input_range = 1 << opts->input_range_mask.val;
    mdata->resistance = opts->resistance.val;
    mdata->reference_temperature = opts->reference_temperature.val;
    mdata->thermistor_resistance = opts->thermistor_resistance.val;

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
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__CELSIUS,
        temperature_kelvin - 273.15);
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT,
        temperature_kelvin * 9 / 5 - 459.67);
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__KELVIN,
        temperature_kelvin);

    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__RAW,
        in_value.val);

    return 0;
}

static void
temperature_child_opts_set(uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_temperature_sensor_options *container_opts = (struct sol_flow_node_type_grove_temperature_sensor_options *)opts;

    if (child_index == 0) {
        // 0 is the temperature-converter index
        struct sol_flow_node_type_grove_temperature_converter_options *converter_opts =
            (struct sol_flow_node_type_grove_temperature_converter_options *)child_opts;
        converter_opts->thermistor_constant = container_opts->thermistor_constant;
        converter_opts->input_range_mask = container_opts->mask;
        converter_opts->resistance = container_opts->resistance;
        converter_opts->reference_temperature = container_opts->reference_temperature;
        converter_opts->thermistor_resistance = container_opts->thermistor_resistance;
    } else if (child_index == 1) {
        // 1 is the aio-reader index
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        reader_opts->pin = container_opts->pin;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }
}

static void
grove_temperature_sensor_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;

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
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__CELSIUS },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__KELVIN },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    nodes[0].type = SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER;
    nodes[1].type = SOL_FLOW_NODE_TYPE_AIO_READER;

    type = sol_flow_static_new_type(nodes, conns, NULL, exported_out, &temperature_child_opts_set);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->new_options = (*current)->new_options;
    *current = type;
}

static void
temperature_init_type(void)
{
    grove_temperature_sensor_new_type(&SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_SENSOR);
}

#include "grove-gen.c"
