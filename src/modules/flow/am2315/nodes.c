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

#include "am2315-gen.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

#include "am2315.h"

#define ZERO_K 273.15

struct am2315_data {
    struct am2315 *device;
    struct sol_flow_node *node;
};

// Humidity Sensor node

static struct sol_drange humidity_out = {
    .min = 0.0,
    .max = 100.0,
    .step = 0.1
};

static void
_send_humidity_error_packet(struct am2315_data *mdata)
{
    const char errmsg[] = "Could not read AM2315 humidity samples";

    SOL_WRN(errmsg);
    sol_flow_send_error_packet(mdata->node, EIO, errmsg);
}

static void
_humidity_reading_callback(float humidity, bool success, void *data)
{
    struct am2315_data *mdata = data;

    if (!success)
        _send_humidity_error_packet(mdata);

    humidity_out.val = humidity;

    sol_flow_send_drange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_AM2315_HUMIDITY__OUT__OUT, &humidity_out);
}

static int
humidity_am2315_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct am2315_data *mdata = data;
    const struct sol_flow_node_type_am2315_humidity_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_AM2315_HUMIDITY_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_am2315_humidity_options *)options;

    mdata->device = am2315_open(opts->i2c_bus.val, opts->i2c_slave.val);
    if (!mdata->device) {
        return -EINVAL;
    }

    am2315_humidity_callback_set(mdata->device, _humidity_reading_callback,
        mdata);

    mdata->node = node;

    return 0;
}

static void
humidity_am2315_close(struct sol_flow_node *node, void *data)
{
    struct am2315_data *mdata = data;

    if (mdata->device)
        am2315_close(mdata->device);
}

static int
humidity_am2315_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct am2315_data *mdata = data;

    am2315_read_humidity(mdata->device);

    return 0;
}

// Temperature sensor node

static struct sol_drange temperature_out = {
    .min = -40.0 + ZERO_K,
    .max = 140.0 + ZERO_K,
    .step = 0.1
};

static void
_send_temperature_error_packet(struct am2315_data *mdata)
{
    const char errmsg[] = "Could not read AM2315 temperature samples";

    SOL_WRN(errmsg);
    sol_flow_send_error_packet(mdata->node, EIO, errmsg);
}

static void
_temperature_reading_callback(float temperature, bool success, void *data)
{
    struct am2315_data *mdata = data;

    if (!success)
        _send_temperature_error_packet(mdata);

    temperature_out.val = temperature - ZERO_K;

    sol_flow_send_drange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_AM2315_TEMPERATURE__OUT__KELVIN, &temperature_out);
}

static int
temperature_am2315_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct am2315_data *mdata = data;
    const struct sol_flow_node_type_am2315_temperature_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_AM2315_TEMPERATURE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_am2315_temperature_options *)options;

    mdata->device = am2315_open(opts->i2c_bus.val, opts->i2c_slave.val);
    if (!mdata->device) {
        return -EINVAL;
    }

    am2315_temperature_callback_set(mdata->device, _temperature_reading_callback,
        mdata);

    mdata->node = node;

    return 0;
}

static void
temperature_am2315_close(struct sol_flow_node *node, void *data)
{
    struct am2315_data *mdata = data;

    if (mdata->device)
        am2315_close(mdata->device);
}

static int
temperature_am2315_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct am2315_data *mdata = data;

    am2315_read_temperature(mdata->device);

    return 0;
}

#include "am2315-gen.c"
