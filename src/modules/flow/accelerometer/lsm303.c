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

#include "accelerometer-gen.h"

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

struct accelerometer_lsm303_data {
    struct sol_i2c *i2c;
    double reading[3];
    double sensitivity;
    uint8_t slave;
    uint8_t scale;
};

static int
accelerometer_lsm303_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct accelerometer_lsm303_data *mdata = data;
    const struct sol_flow_node_type_accelerometer_lsm303_options *opts;
    static const uint8_t mode = LSM303_ACCEL_DEFAULT_MODE;
    uint8_t scale_bit;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_ACCELEROMETER_LSM303_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_accelerometer_lsm303_options *)options;

    switch (opts->scale.val) {
    case 2:
        scale_bit = 0x00;
        mdata->sensitivity = 1.0 / 1000;
        break;
    case 4:
        scale_bit = 0x01;
        mdata->sensitivity = 2.0 / 1000;
        break;
    case 8:
        scale_bit = 0x02;
        mdata->sensitivity = 4.0 / 1000;
        break;
    case 16:
        scale_bit = 0x03;
        mdata->sensitivity = 12.0 / 1000;
        break;
    default:
        SOL_WRN("Invalid scale. Expected one of 2, 4, 8 or 16");
        return -EINVAL;
    }
    mdata->scale = opts->scale.val;

    mdata->i2c = sol_i2c_open(opts->i2c_bus.val, SOL_I2C_SPEED_10KBIT);
    if (!mdata->i2c) {
        SOL_WRN("Failed to open i2c bus");
        return -EINVAL;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, opts->i2c_slave.val)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            opts->i2c_slave.val);
        goto fail;
    }
    mdata->slave = opts->i2c_slave.val;

    if (!sol_i2c_write_register(mdata->i2c, LSM303_ACCEL_REG_CTRL_REG1_A, &mode, 1)) {
        SOL_WRN("Could not enable LSM303 accelerometer");
        goto fail;
    }

    if (!sol_i2c_write_register(mdata->i2c, LSM303_ACCEL_REG_CTRL_REG4_A, &scale_bit, 1)) {
        SOL_WRN("Could not set scale to LSM303 accelerometer");
        goto fail;
    }

    return 0;

fail:
    sol_i2c_close(mdata->i2c);
    return -EIO;
}

static void
accelerometer_lsm303_close(struct sol_flow_node *node, void *data)
{
    struct accelerometer_lsm303_data *mdata = data;

    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);
}

static void
_lsm303_send_output_packets(struct sol_flow_node *node, struct accelerometer_lsm303_data *mdata)
{
    struct sol_direction_vector val =
    {
        .min = -mdata->scale,
        .max = -mdata->scale,
        .x = mdata->reading[0],
        .y = mdata->reading[1],
        .z = mdata->reading[2]
    };

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_ACCELEROMETER_LSM303__OUT__RAW, &val);

    val.x = val.x * GRAVITY_MSS;
    val.y = val.y * GRAVITY_MSS;
    val.z = val.z * GRAVITY_MSS;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_ACCELEROMETER_LSM303__OUT__OUT, &val);
}

static int
accelerometer_lsm303_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct accelerometer_lsm303_data *mdata = data;
    int8_t buffer[LSM303_ACCEL_BYTES_NUMBER];
    int r;

    if (!sol_i2c_set_slave_address(mdata->i2c, mdata->slave)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            mdata->slave);
        return -EIO;
    }

    /* ORing with 0x80 to read all bytes in a row */
    r = sol_i2c_read_register(mdata->i2c, LSM303_ACCEL_REG_OUT_X_H_A | 0x80,
        (uint8_t *)buffer, sizeof(buffer));
    if (r <= 0) {
        SOL_WRN("Failed to read LSM303 accel samples");
        return -EIO;
    }

    /* http://stackoverflow.com/a/19164062 says that it's necessary to >> 4 buffer result.
     * https://github.com/adafruit/Adafruit_LSM303/blob/master/Adafruit_LSM303.cpp does the shift
     * Doing it here, but it's interesting to check it. Datasheet says nothing about it, though.
     */
    mdata->reading[0] = ((buffer[0] | (buffer[1] << 8)) >> 4) * mdata->sensitivity;
    mdata->reading[1] = ((buffer[2] | (buffer[3] << 8)) >> 4) * mdata->sensitivity;
    mdata->reading[2] = ((buffer[4] | (buffer[5] << 8)) >> 4) * mdata->sensitivity;

    _lsm303_send_output_packets(node, mdata);

    return 0;
}

