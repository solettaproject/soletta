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

#include "sol-flow/stts751.h"
#include "sol-flow-internal.h"

#include <sol-i2c.h>
#include <sol-mainloop.h>

#include <errno.h>

#define STEP_TIME 1
#define BYTES_NUMBER 3
#define STATUS_REGISTER 0x01
#define CONFIGURATION_REGISTER 0x03
#define TEMPERATURE_REGISTER_H 0x00
#define TEMPERATURE_REGISTER_L 0x02

enum reading_step {
    READING_NONE,
    READING_STATUS,
    READING_TEMP_H,
    READING_TEMP_L
};

struct stts751_data {
    struct sol_flow_node *node;
    struct sol_i2c *i2c;
    struct sol_i2c_pending *i2c_pending;
    struct sol_timeout *timer;
    enum reading_step reading_step;
    uint8_t i2c_buffer;
    uint8_t slave;
    uint8_t resolution;
    uint8_t status;
    int8_t temp_h;
    uint8_t temp_l;
};

static bool stts751_read(void *data);

static int
timer_sched(struct stts751_data *mdata, uint32_t timeout_ms, bool (*cb)(void *data))
{
    mdata->timer = sol_timeout_add(timeout_ms, cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
i2c_write_configuration_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg,
    uint8_t *data, ssize_t status)
{
    struct stts751_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0)
        SOL_WRN("Could not enable STTS751 temperature sensor");
}

static bool
set_slave(struct stts751_data *mdata, bool (*cb)(void *data))
{
    int r;

    r = sol_i2c_set_slave_address(mdata->i2c, mdata->slave);

    if (r < 0) {
        if (r == -EBUSY)
            timer_sched(mdata, STEP_TIME, cb);
        else {
            const char errmsg[] = "Failed to set slave at address 0x%02x";
            SOL_WRN(errmsg, mdata->slave);
            sol_flow_send_error_packet(mdata->node, r, errmsg, mdata->slave);
        }
        return false;
    }

    return true;
}

static bool
stts751_init(void *data)
{
    struct stts751_data *mdata = data;
    static const uint8_t resolutions[] = { 0x2, 0x0, 0x1, 0x3 };

    mdata->timer = NULL;

    if (!set_slave(mdata, stts751_init))
        return false;

    /*TODO: It might be a good idea to use the stand-by mode and one-shot reading,
     * as it saves energy */

    /*Resolution bits are form 9 to 12, their setup values are on resolutions
     * from 0 to 3, thus the '- 9' below */
    mdata->i2c_buffer = resolutions[mdata->resolution - 9] << 2;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c,
        CONFIGURATION_REGISTER, &mdata->i2c_buffer, 1, i2c_write_configuration_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Could not set STTS751 temperature reading resolution");

    return false;
}

static int
temperature_stts751_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct stts751_data *mdata = data;
    const struct sol_flow_node_type_stts751_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_STTS751_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_stts751_options *)options;

    mdata->node = node;

    mdata->i2c = sol_i2c_open(opts->i2c_bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->i2c, -EINVAL, "Failed to open i2c bus");

    mdata->slave = opts->i2c_slave;
    mdata->resolution = opts->temperature_resolution;
    if (mdata->resolution < 9 || mdata->resolution > 12) {
        SOL_WRN("Invalid temperature resolution bits for STTS751 %d. "
            "Must be between 9 and 12. Falling back to 10.", mdata->resolution);
        mdata->resolution = 10;
    }


    stts751_init(mdata);

    return 0;
}

static void
temperature_stts751_close(struct sol_flow_node *node, void *data)
{
    struct stts751_data *mdata = data;

    if (mdata->i2c_pending)
        sol_i2c_pending_cancel(mdata->i2c, mdata->i2c_pending);
    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);

    if (mdata->timer)
        sol_timeout_del(mdata->timer);
}

static void
send_temperature(struct stts751_data *mdata)
{
    double temp;
    static const double steps[] = { 0.5, 0.25, 0.125, 0.0625 };
    struct sol_drange val = {
        .min = -64.0,
        .max = 127.9375,
        .step = steps[mdata->resolution - 9]
    };

    SOL_DBG("Temperature registers H:0x%x, L:0x%x", mdata->temp_h, mdata->temp_l);

    temp = mdata->temp_h;
    /* XXX Check if negative conversion is right */
    temp += ((double)(mdata->temp_l) / (1 << 8));

    /* To Kelvin */
    temp += 273.16;

    val.val = temp;

    sol_flow_send_drange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_STTS751__OUT__KELVIN, &val);
}

static void
read_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg,
    uint8_t *data, ssize_t status)
{
    struct stts751_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        const char errmsg[] = "Failed to read STTS751 temperature status";
        SOL_WRN(errmsg);
        sol_flow_send_error_packet(mdata->node, EIO, errmsg);
        mdata->reading_step = READING_NONE;
        return;
    }

    /* If reading status, let's check it */
    if (mdata->reading_step == READING_STATUS && mdata->status) {
        const char errmsg[] = "Invalid temperature status: 0x%x";
        SOL_WRN(errmsg, mdata->status);
        mdata->reading_step = READING_NONE;
        return;
    }

    /* Last step, send temperature */
    if (mdata->reading_step == READING_TEMP_L) {
        send_temperature(mdata);
        mdata->reading_step = READING_NONE;
        return;
    }

    mdata->reading_step++;
    stts751_read(mdata);
}

static bool
stts751_read(void *data)
{
    struct stts751_data *mdata = data;
    uint8_t reg, *dst;

    mdata->timer = NULL;

    if (!set_slave(mdata, stts751_read))
        return false;

    switch (mdata->reading_step) {
    case READING_STATUS:
        reg = STATUS_REGISTER;
        dst = &mdata->status;
        break;
    case READING_TEMP_H:
        reg = TEMPERATURE_REGISTER_H;
        dst = (uint8_t *)&mdata->temp_h;
        break;
    case READING_TEMP_L:
        reg = TEMPERATURE_REGISTER_L;
        dst = &mdata->temp_l;
        break;
    default:
        SOL_WRN("Invalid reading step");
        return false;
    }

    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c, reg,
        dst, sizeof(*dst), read_cb, mdata);

    if (!mdata->i2c_pending) {
        const char errmsg[] = "Failed to read STTS751 temperature";
        SOL_WRN(errmsg);
        sol_flow_send_error_packet(mdata->node, EIO, errmsg);
        mdata->reading_step = READING_NONE;
    }

    return false;
}

static int
temperature_stts751_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct stts751_data *mdata = data;

    if (mdata->reading_step != READING_NONE) {
        SOL_WRN("Reading operation in progress, discading TICK");
        return 0;
    }
    /* First, read the status, if it's ok, then we read temp high and low */
    mdata->reading_step = READING_STATUS;
    stts751_read(mdata);

    return 0;
}
#include "stts751-gen.c"
