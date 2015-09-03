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

/* speed only works for riot */
#define I2C_SPEED SOL_I2C_SPEED_10KBIT

#define INIT_POWER_OFF 0x00
#define INIT_POWER_STARTING 0xff
#define INIT_POWER_MEASURING 0x08

/* ADXL345 Accelerometer
 * http://www.analog.com/static/imported-files/data_sheets/ADXL345.pdf
 */
struct accelerometer_adxl345_data {
    struct sol_flow_node *node;
    struct sol_i2c *i2c;
    struct sol_i2c_pending *i2c_pending;
    struct sol_timeout *timer;
    double reading[3];
    unsigned init_sampling_cnt;
    unsigned pending_ticks;
    //I2C buffers
    union {
        struct {
            uint8_t buffer[64];
        } common;
        struct {
            /* int16_t and 3 entries because of x, y and z axis are read,
             * each consisting of L + H byte parts.
             */
            int16_t buffer[64][3];
        } accel_data;
    };
    bool ready : 1;
};

#define ACCEL_STEP_TIME 1
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

/* ADXL345 accelerometer scaling (result will be scaled to 1m/s/s)
 * ACCEL in full resolution mode (any g scaling) is 256 counts/g, so
 * scale by 9.81/256 = 0.038320312
 */
#define ACCEL_SCALE_M_S (GRAVITY_MSS / 256.0f)

static bool accel_tick_do(void *data);

static int
accel_timer_resched(struct accelerometer_adxl345_data *mdata,
    unsigned int timeout_ms,
    bool (*cb)(void *data))
{
    mdata->timer = sol_timeout_add(timeout_ms, cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
i2c_read_multiple_data_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;
    uint8_t num_samples_available;
    struct sol_direction_vector val =
    {
        .min = -ACCEL_RANGE,
        .max = ACCEL_RANGE,
        .x = mdata->reading[0],
        .y = mdata->reading[1],
        .z = mdata->reading[2]
    };

    mdata->i2c_pending = NULL;
    if (status < 0)
        return;

    num_samples_available = status / (sizeof(int16_t) * 3);

    /* At least with the current i2c driver implementation at the
     * time of testing, if too much time passes between two
     * consecutive readings, the buffer will be reported full, but
     * the last readings will contain trash values -- this will
     * guard against that (one will have to read again to get
     * newer values)*/
#define MAX_EPSILON (10.0f)
#define EPSILON_CHECK(_axis) \
    if (i > 0 && isgreater(fabs((mdata->accel_data.buffer[i][_axis] * ACCEL_SCALE_M_S) \
        - mdata->reading[_axis]), MAX_EPSILON)) \
        break

    /* raw readings, with only the sensor-provided filtering */
    for (int i = 0; i < num_samples_available; i++) {
        EPSILON_CHECK(0);
        EPSILON_CHECK(1);
        EPSILON_CHECK(2);

        mdata->reading[0] = mdata->accel_data.buffer[i][0] * ACCEL_SCALE_M_S;
        mdata->reading[1] = -mdata->accel_data.buffer[i][1] * ACCEL_SCALE_M_S;
        mdata->reading[2] = -mdata->accel_data.buffer[i][2] * ACCEL_SCALE_M_S;
    }
#undef MAX_EPSILON
#undef EPSILON_CHECK

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_ACCELEROMETER_ADXL345__OUT__OUT, &val);

    mdata->pending_ticks--;
    if (mdata->pending_ticks)
        accel_tick_do(mdata);
}

static void
i2c_read_fifo_status_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;
    uint8_t num_samples_available;

    mdata->i2c_pending = NULL;
    if (status < 0)
        return;

    num_samples_available = mdata->common.buffer[0] & 0x3F;

    if (!num_samples_available) {
        SOL_INF("No samples available");
        return;
    }

    SOL_DBG("%d samples available", num_samples_available);

    mdata->i2c_pending = sol_i2c_read_register_multiple(mdata->i2c,
        ACCEL_REG_DATAX0, (uint8_t *)&mdata->accel_data.buffer[0][0],
        sizeof(mdata->accel_data.buffer[0]), num_samples_available,
        i2c_read_multiple_data_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Failed to read ADXL345 accel samples");
}

static bool
accel_tick_do(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        accel_timer_resched(mdata, ACCEL_STEP_TIME, accel_tick_do);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    mdata->common.buffer[0] = 0;
    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c,
        ACCEL_REG_FIFO_STATUS, mdata->common.buffer, 1,
        i2c_read_fifo_status_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Failed to read ADXL345 accel fifo status");

    return false;
}

static void
i2c_write_fifo_ctl_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set ADXL345 accel sensor's stream mode");
        return;
    }

    mdata->ready = true;
    SOL_DBG("accel is ready for reading");

    if (mdata->pending_ticks)
        accel_tick_do(mdata);
}

static bool
accel_init_stream(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        accel_timer_resched(mdata, ACCEL_STEP_TIME, accel_init_stream);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    /* enable FIFO in stream mode */
    mdata->common.buffer[0] = ACCEL_REG_FIFO_CTL_STREAM;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, ACCEL_REG_FIFO_CTL,
        mdata->common.buffer, 1, i2c_write_fifo_ctl_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set ADXL345 accel sensor's stream mode");

    return false;
}

static void
i2c_write_bw_rate_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set ADXL345 accel sensor's sampling rate");
        return;
    }

    if (accel_timer_resched(mdata, ACCEL_STEP_TIME,
        accel_init_stream) < 0)
        SOL_WRN("error in scheduling a ADXL345 accel's init command");
}

static bool
accel_init_rate(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        accel_timer_resched(mdata, ACCEL_STEP_TIME, accel_init_rate);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    mdata->common.buffer[0] = 0x0d;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, ACCEL_REG_BW_RATE,
        mdata->common.buffer, 1, i2c_write_bw_rate_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set ADXL345 accel sensor's sampling rate");

    return false;
}

static void
i2c_write_data_format_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set ADXL345 accel sensor's resolution");
        return;
    }

    if (accel_timer_resched(mdata, ACCEL_STEP_TIME,
        accel_init_rate) < 0)
        SOL_WRN("error in scheduling a ADXL345 accel's init command");
}

static bool
accel_init_format(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        accel_timer_resched(mdata, ACCEL_STEP_TIME, accel_init_format);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    /* Full resolution, 8g:
     * Caution, this must agree with ACCEL_SCALE_1G
     * In full resolution mode, the scale factor need not change
     */
    mdata->common.buffer[0] = 0x08;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c,
        ACCEL_REG_DATA_FORMAT, mdata->common.buffer, 1,
        i2c_write_data_format_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set ADXL345 accel sensor's resolution");

    return false;
}

static bool accel_init_power(void *data);

/* meant to run 3 times */
static void
i2c_write_power_ctl_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;
    bool power_done = false;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set ADXL345 accel sensor's power mode");
        return;
    }

    if (mdata->common.buffer[0] == INIT_POWER_OFF)
        mdata->common.buffer[0] = INIT_POWER_STARTING;
    else if (mdata->common.buffer[0] == INIT_POWER_STARTING)
        mdata->common.buffer[0] = INIT_POWER_MEASURING;
    else
        power_done = true;

    if (accel_timer_resched(mdata, ACCEL_STEP_TIME,
        power_done ?
        accel_init_format : accel_init_power) < 0) {
        SOL_WRN("error in scheduling a ADXL345 accel's init command");
    }
}

static bool
accel_init_power(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        accel_timer_resched(mdata, ACCEL_STEP_TIME, accel_init_power);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", ACCEL_ADDRESS);
        return false;
    }

    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, ACCEL_REG_POWER_CTL,
        mdata->common.buffer, 1, i2c_write_power_ctl_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set ADXL345 accel sensor's power mode");

    return false;
}

static void
i2c_read_dev_id_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct accelerometer_adxl345_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0 || mdata->common.buffer[0] != ACCEL_DEV_ID) {
        SOL_WRN("could not find ADXL345 accel sensor");
        return;
    }

    mdata->common.buffer[0] = INIT_POWER_OFF;
    accel_init_power(mdata);
}

static bool
accel_init(void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        accel_timer_resched(mdata, ACCEL_STEP_TIME, accel_init);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, ACCEL_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            ACCEL_ADDRESS);
        return false;
    }

    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c, ACCEL_REG_DEV_ID,
        mdata->common.buffer, 1, i2c_read_dev_id_cb, mdata);
    if (!mdata->i2c_pending) {
        SOL_WRN("Failed to read i2c register");
    }

    return false;
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

    mdata->node = node;
    accel_init(mdata);

    return 0;
}

static void
accelerometer_adxl345_close(struct sol_flow_node *node, void *data)
{
    struct accelerometer_adxl345_data *mdata = data;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    if (mdata->i2c_pending)
        sol_i2c_pending_cancel(mdata->i2c, mdata->i2c_pending);
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

    if (!mdata->ready || mdata->pending_ticks) {
        mdata->pending_ticks++;
        return 0;
    }

    accel_tick_do(mdata);
    return 0;
}
