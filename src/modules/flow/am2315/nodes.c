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

#include "sol-flow/am2315.h"
#include "sol-flow-internal.h"

#include <sol-util-internal.h>
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

    mdata->device = am2315_open(opts->i2c_bus, opts->i2c_slave);
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
        SOL_FLOW_NODE_TYPE_AM2315_THERMOMETER__OUT__KELVIN, &temperature_out);
}

static int
temperature_am2315_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct am2315_data *mdata = data;
    const struct sol_flow_node_type_am2315_thermometer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_AM2315_THERMOMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_am2315_thermometer_options *)options;

    mdata->device = am2315_open(opts->i2c_bus, opts->i2c_slave);
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
