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

#include "sol-flow-internal.h"
#include "sol-i2c.h"
#include "sol-mainloop.h"
#include "sol-util.h"

/* speed only works for riot */
#define I2C_SPEED SOL_I2C_SPEED_10KBIT

/* ADXL345 Accelerometer
 * http://www.analog.com/static/imported-files/data_sheets/ADXL345.pdf
 */
struct accelerometer_adxl345_data {
    struct sol_flow_node *node;
    struct sol_i2c *i2c;
    struct sol_timeout *timer;
    double reading[3];
    uint8_t init_power;
    unsigned init_sampling_cnt;
    unsigned pending_ticks;
    bool ready : 1;
};

#define ACCEL_INIT_STEP_TIME 1
#define ACCEL_RANGE 8         // 8 g

/* Accelerometer register definitions */
#define ACCEL_ADDRESS 0x53
#define ACCEL_DEV_ID 0xe5
#define ACCEL_REG_BW_RATE 0x2c
#define ACCEL_REG_DATAX0 0x32
#define ACCEL_REG_DATA_FORMAT 0x31
#define ACCEL_REG_DEV_ID 0x00
#define ACCEL_REG_FIFO_CTL 0x38
#define ACCEL_REG_FIFO_CTL_STREAM 0x9F
#define ACCEL_REG_FIFO_STATUS 0x39
#define ACCEL_REG_POWER_CTL 0x2d

/* acceleration due to gravity in m/s/s */
#define GRAVITY_MSS 9.80665f

/* ADXL345 accelerometer scaling (result will be scaled to 1m/s/s)
 * ACCEL in full resolution mode (any g scaling) is 256 counts/g, so
 * scale by 9.81/256 = 0.038320312
 */
#define ACCEL_SCALE_M_S (GRAVITY_MSS / 256.0f)

static int
accel_timer_resched(struct accelerometer_adxl345_data *mdata,
    unsigned int timeout_ms,
    bool (*cb)(void *data),
    const void *cb_data)
{
    SOL_NULL_CHECK(cb, -EINVAL);

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    mdata->timer = sol_timeout_add(timeout_ms, cb, cb_data);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
accel_read(struct accelerometer_adxl345_data *mdata)
{
    uint8_t num_samples_available;
    uint8_t fifo_status = 0;
    int r;

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return;
    }

    fifo_status = 0;
    r = sol_i2c_read_register(mdata->i2c, ACCEL_REG_FIFO_STATUS, &fifo_status,
        1);
    if (r <= 0) {
        SOL_WRN("Failed to read ADXL345 accel fifo status");
        return;
    }

    num_samples_available = fifo_status & 0x3F;

    if (!num_samples_available) {
        SOL_INF("No samples available");
        return;
    }

    SOL_DBG("%d samples available", num_samples_available);

    {
        /* int16_t and 3 entries because of x, y and z axis are read,
         * each consisting of L + H byte parts
         */
        int16_t buffer[num_samples_available][3];

        r = sol_i2c_read_register_multiple(mdata->i2c,
            ACCEL_REG_DATAX0, (uint8_t *)&buffer[0][0], sizeof(buffer[0]),
            num_samples_available);
        if (r <= 0) {
            SOL_WRN("Failed to read ADXL345 accel samples");
            return;
        }

        /* At least with the current i2c driver implementation at the
         * time of testing, if too much time passes between two
         * consecutive readings, the buffer will be reported full, but
         * the last readings will contain trash values -- this will
         * guard against that (one will have to read again to get
         * newer values)*/
#define MAX_EPSILON (10.0f)
#define EPSILON_CHECK(_axis) \
    if (i > 0 && isgreater(fabs((buffer[i][_axis] * ACCEL_SCALE_M_S) \
        - mdata->reading[_axis]), MAX_EPSILON)) \
        break

        /* raw readings, with only the sensor-provided filtering */
        for (int i = 0; i < num_samples_available; i++) {
            EPSILON_CHECK(0);
            EPSILON_CHECK(1);
            EPSILON_CHECK(2);

            mdata->reading[0] = buffer[i][0] * ACCEL_SCALE_M_S;
            mdata->reading[1] = -buffer[i][1] * ACCEL_SCALE_M_S;
            mdata->reading[2] = -buffer[i][2] * ACCEL_SCALE_M_S;
        }
#undef MAX_EPSILON
#undef EPSILON_CHECK
    }
}

static int
accel_tick_do(struct accelerometer_adxl345_data *mdata)
{
    struct sol_direction_vector val =
    {
        .min = -ACCEL_RANGE,
        .max = ACCEL_RANGE,
        .x = mdata->reading[0],
        .y = mdata->reading[1],
        .z = mdata->reading[2]
    };
    int r;

    accel_read(mdata);

    r = sol_flow_send_direction_vector_packet
            (mdata->node, SOL_FLOW_NODE_TYPE_ACCELEROMETER_ADXL345__OUT__OUT,
            &val);

    return r;
}

static void
accel_ready(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    mdata->ready = true;

    while (mdata->pending_ticks) {
        accel_tick_do(mdata);
        mdata->pending_ticks--;
    }

    SOL_DBG("accel is ready for reading");
}

static bool
accel_init_stream(void *data)
{
    bool r;
    struct accelerometer_adxl345_data *mdata = data;
    static const uint8_t value = ACCEL_REG_FIFO_CTL_STREAM;

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    /* enable FIFO in stream mode */
    r = sol_i2c_write_register(mdata->i2c, ACCEL_REG_FIFO_CTL, &value, 1);
    if (!r) {
        SOL_WRN("could not set ADXL345 accel sensor's stream mode");
        return false;
    }

    accel_ready(mdata);

    return false;
}

static bool
accel_init_rate(void *data)
{
    bool r;
    struct accelerometer_adxl345_data *mdata = data;
    static const uint8_t value = 0x0d;

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    r = sol_i2c_write_register(mdata->i2c, ACCEL_REG_BW_RATE, &value, 1);
    if (!r) {
        SOL_WRN("could not set ADXL345 accel sensor's sampling rate");
        return false;
    }

    if (accel_timer_resched(mdata, ACCEL_INIT_STEP_TIME,
        accel_init_stream, mdata) < 0)
        SOL_WRN("error in scheduling a ADXL345 accel's init command");

    return false;
}

static bool
accel_init_format(void *data)
{
    bool r;
    struct accelerometer_adxl345_data *mdata = data;
    /* Full resolution, 8g:
     * Caution, this must agree with ACCEL_SCALE_1G
     * In full resoution mode, the scale factor need not change
     */
    static const uint8_t value = 0x08;

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    r = sol_i2c_write_register(mdata->i2c, ACCEL_REG_DATA_FORMAT, &value, 1);
    if (!r) {
        SOL_WRN("could not set ADXL345 accel sensor's resolution");
        return false;
    }

    if (accel_timer_resched(mdata, ACCEL_INIT_STEP_TIME,
        accel_init_rate, mdata) < 0)
        SOL_WRN("error in scheduling a ADXL345 accel's init command");

    return false;
}

/* meant to run 3 times */
static bool
accel_init_power(void *data)
{
    bool r, power_done = false;
    static bool first_run = true;
    struct accelerometer_adxl345_data *mdata = data;

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    r = sol_i2c_write_register(mdata->i2c, ACCEL_REG_POWER_CTL,
        &mdata->init_power, 1);
    if (!r) {
        SOL_WRN("could not set ADXL345 accel sensor's power mode");
        return false;
    }

    if (mdata->init_power == 0x00)
        mdata->init_power = 0xff;
    else if (mdata->init_power == 0xff)
        mdata->init_power = 0x08;
    else
        power_done = true;

    if (accel_timer_resched(mdata, ACCEL_INIT_STEP_TIME,
        power_done ?
        accel_init_format : accel_init_power,
        mdata) < 0) {
        SOL_WRN("error in scheduling a ADXL345 accel's init command");
    }

    if (first_run) {
        first_run = false;
        return true;
    }

    return false;
}

static int
accel_init(struct accelerometer_adxl345_data *mdata)
{
    ssize_t r;
    uint8_t data = 0;

    r = sol_i2c_read_register(mdata->i2c, ACCEL_REG_DEV_ID, &data, 1);
    if (r < 0) {
        SOL_WRN("Failed to read i2c register");
        return r;
    }
    if (data != ACCEL_DEV_ID) {
        SOL_WRN("could not find ADXL345 accel sensor");
        return -EIO;
    }

    return accel_init_power(mdata) ? 0 : -EIO;
}

static int
accelerometer_adxl345_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct accelerometer_adxl345_data *mdata = data;
    const struct sol_flow_node_type_accelerometer_adxl345_options *opts =
        (const struct sol_flow_node_type_accelerometer_adxl345_options *)options;

    SOL_NULL_CHECK(options, -EINVAL);

    mdata->i2c = sol_i2c_open(opts->i2c_bus.val, I2C_SPEED);
    if (!mdata->i2c) {
        SOL_WRN("Failed to open i2c bus");
        return -EIO;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            ACCEL_ADDRESS);
        return -EIO;
    }

    mdata->init_power = 0x00;
    mdata->ready = false;
    mdata->node = node;

    return accel_init(mdata);
}

static void
accelerometer_adxl345_close(struct sol_flow_node *node, void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);
}

static int
accelerometer_adxl345_tick(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct accelerometer_adxl345_data *mdata = data;

    if (!mdata->ready) {
        mdata->pending_ticks++;
        return 0;
    }

    return accel_tick_do(mdata);
}

// ############################################# LSM303DHLC accelerometer
// http://www.adafruit.com/datasheets/LSM303DLHC.PDF

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
    default:
        SOL_WRN("Invalid scale. Expected one of 2, 4, 8 or 16");
        return -EINVAL;
    }
    mdata->scale = opts->scale.val;

    mdata->i2c = sol_i2c_open(opts->i2c_bus.val, I2C_SPEED);
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
        const char errmsg[] = "Failed to set slave at address 0x%02x";
        SOL_WRN(errmsg, mdata->slave);
        sol_flow_send_error_packet(node, EIO, errmsg, mdata->slave);
        return -EIO;
    }

    // ORing with 0x80 to read all bytes in a row
    r = sol_i2c_read_register(mdata->i2c, LSM303_ACCEL_REG_OUT_X_H_A | 0x80,
        (uint8_t *)buffer, sizeof(buffer));
    if (r <= 0) {
        const char errmsg[] = "Failed to read LSM303 accel samples";
        SOL_WRN(errmsg);
        sol_flow_send_error_packet(node, EIO, errmsg);
        return -EIO;
    }

    // http://stackoverflow.com/a/19164062 says that it's necessary to >> 4 buffer result.
    // https://github.com/adafruit/Adafruit_LSM303/blob/master/Adafruit_LSM303.cpp does the shift
    // Doing it here, but it's interesting to check it. Datasheet says nothing about it, though.
    mdata->reading[0] = ((buffer[0] | (buffer[1] << 8)) >> 4) * mdata->sensitivity;
    mdata->reading[1] = ((buffer[2] | (buffer[3] << 8)) >> 4) * mdata->sensitivity;
    mdata->reading[2] = ((buffer[4] | (buffer[5] << 8)) >> 4) * mdata->sensitivity;

    _lsm303_send_output_packets(node, mdata);

    return 0;
}

#include "accelerometer-gen.c"
