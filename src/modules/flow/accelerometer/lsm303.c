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


#include <errno.h>
#include <math.h>

#include "sol-flow/accelerometer.h"

#include "sol-i2c.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-flow-internal.h"

/* LSM303DHLC accelerometer
 * http://www.adafruit.com/datasheets/LSM303DLHC.PDF
 */

#define LSM303_ACCEL_DEFAULT_MODE 0x27
#define LSM303_ACCEL_BYTES_NUMBER 6
#define LSM303_ACCEL_REG_OUT_X_H_A 0x28
#define LSM303_ACCEL_REG_CTRL_REG1_A 0x20
#define LSM303_ACCEL_REG_CTRL_REG4_A 0x23

#define ACCEL_STEP_TIME 1

struct accelerometer_lsm303_data {
    struct sol_flow_node *node;
    struct sol_i2c *i2c;
    struct sol_i2c_pending *i2c_pending;
    struct sol_timeout *timer;
    double reading[3];
    double sensitivity;
    uint8_t slave;
    uint8_t scale;
    uint8_t i2c_buffer[LSM303_ACCEL_BYTES_NUMBER];
    uint8_t ready : 1;
    unsigned pending_ticks;
};

static bool lsm303_read_data(void *data);

static int
lsm303_timer_resched(struct accelerometer_lsm303_data *mdata, uint32_t timeout_ms, bool (*cb)(void *data))
{
    mdata->timer = sol_timeout_add(timeout_ms, cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
lsm303_i2c_write_scale_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_lsm303_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Could not set scale to LSM303 accelerometer");
        return;
    }

    mdata->ready = true;
    if (mdata->pending_ticks)
        lsm303_read_data(mdata);
}

static void
lsm303_scale_bit_set(struct accelerometer_lsm303_data *mdata)
{
    if (!sol_i2c_set_slave_address(mdata->i2c, mdata->slave)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", mdata->slave);
        return;
    }

    switch (mdata->scale) {
    case 2:
        mdata->i2c_buffer[0] = 0x00;
        mdata->sensitivity = 1.0 / 1000;
        break;
    case 4:
        mdata->i2c_buffer[0] = 0x01;
        mdata->sensitivity = 2.0 / 1000;
        break;
    case 8:
        mdata->i2c_buffer[0] = 0x02;
        mdata->sensitivity = 4.0 / 1000;
        break;
    case 16:
        mdata->i2c_buffer[0] = 0x03;
        mdata->sensitivity = 12.0 / 1000;
        break;
    default:
        SOL_WRN("Invalid scale. Expected one of 2, 4, 8 or 16");
        return;
    }

    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c,
        LSM303_ACCEL_REG_CTRL_REG4_A, mdata->i2c_buffer, 1,
        lsm303_i2c_write_scale_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Could not set scale to LSM303 accelerometer");
}

static void
lsm303_i2c_write_mode_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_lsm303_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Could not enable LSM303 accelerometer");
        return;
    }

    lsm303_scale_bit_set(mdata);
}

static bool
lsm303_accel_init(void *data)
{
    struct accelerometer_lsm303_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        lsm303_timer_resched(mdata, ACCEL_STEP_TIME, lsm303_accel_init);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, mdata->slave)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            mdata->slave);
        return false;
    }

    mdata->i2c_buffer[0] = LSM303_ACCEL_DEFAULT_MODE;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c,
        LSM303_ACCEL_REG_CTRL_REG1_A, mdata->i2c_buffer, 1,
        lsm303_i2c_write_mode_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Could not enable LSM303 accelerometer");

    return false;
}

static int
accelerometer_lsm303_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct accelerometer_lsm303_data *mdata = data;
    const struct sol_flow_node_type_accelerometer_lsm303_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_ACCELEROMETER_LSM303_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_accelerometer_lsm303_options *)options;

    mdata->i2c = sol_i2c_open(opts->i2c_bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->i2c, -EINVAL, "Failed to open i2c bus");

    mdata->node = node;
    mdata->scale = opts->scale;
    mdata->slave = opts->i2c_slave;
    lsm303_accel_init(mdata);

    return 0;
}

static void
accelerometer_lsm303_close(struct sol_flow_node *node, void *data)
{
    struct accelerometer_lsm303_data *mdata = data;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    if (mdata->i2c_pending)
        sol_i2c_pending_cancel(mdata->i2c, mdata->i2c_pending);
    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);
}

static void
_lsm303_send_output_packets(struct accelerometer_lsm303_data *mdata)
{
    struct sol_direction_vector val =
    {
        .min = -mdata->scale,
        .max = mdata->scale,
        .x = mdata->reading[0],
        .y = mdata->reading[1],
        .z = mdata->reading[2]
    };

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_ACCELEROMETER_LSM303__OUT__RAW, &val);

    val.x = val.x * GRAVITY_MSS;
    val.y = val.y * GRAVITY_MSS;
    val.z = val.z * GRAVITY_MSS;

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_ACCELEROMETER_LSM303__OUT__OUT, &val);

    mdata->pending_ticks--;
    if (mdata->pending_ticks)
        lsm303_read_data(mdata);
}

static void
i2c_read_data_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *buffer, ssize_t status)
{
    struct accelerometer_lsm303_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Could not enable LSM303 accelerometer");
        return;
    }

    /* http://stackoverflow.com/a/19164062 says that it's necessary to >> 4 buffer result.
     * https://github.com/adafruit/Adafruit_LSM303/blob/master/Adafruit_LSM303.cpp does the shift
     * Doing it here, but it's interesting to check it. Datasheet says nothing about it, though.
     */
    mdata->reading[0] = ((buffer[0] | (buffer[1] << 8)) >> 4) * mdata->sensitivity;
    mdata->reading[1] = ((buffer[2] | (buffer[3] << 8)) >> 4) * mdata->sensitivity;
    mdata->reading[2] = ((buffer[4] | (buffer[5] << 8)) >> 4) * mdata->sensitivity;

    _lsm303_send_output_packets(mdata);
}

static bool
lsm303_read_data(void *data)
{
    struct accelerometer_lsm303_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        lsm303_timer_resched(mdata, ACCEL_STEP_TIME, lsm303_read_data);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, mdata->slave)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            mdata->slave);
        return false;
    }

    /* ORing with 0x80 to read all bytes in a row */
    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c,
        LSM303_ACCEL_REG_OUT_X_H_A | 0x80, mdata->i2c_buffer,
        sizeof(mdata->i2c_buffer), i2c_read_data_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Failed to read LSM303 accel samples");

    return false;
}

static int
accelerometer_lsm303_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct accelerometer_lsm303_data *mdata = data;

    if (!mdata->ready || mdata->pending_ticks) {
        mdata->pending_ticks++;
        return 0;
    }

    lsm303_read_data(mdata);

    return 0;
}
