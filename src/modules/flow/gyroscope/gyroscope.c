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

#include "sol-flow/gyroscope.h"

#include <errno.h>

#include "sol-i2c.h"
#include "sol-mainloop.h"
#include "sol-util.h"

/* speed only works for riot */
#define I2C_SPEED SOL_I2C_SPEED_10KBIT

/* L3G4200D gyroscope:
 * http://www.st.com/st-web-ui/static/active/en/resource/technical/document/datasheet/CD00265057.pdf
 */
struct gyroscope_l3g4200d_data {
    struct sol_flow_node *node;
    struct sol_i2c *i2c;
    struct sol_i2c_pending *i2c_pending;
    struct sol_timeout *timer;
    double reading[3];
    unsigned init_sampling_cnt;
    unsigned pending_ticks;
    bool use_rad : 1;
    bool ready : 1;
    // I2C buffer
    union {
        struct {
            uint8_t buffer[32];
        } common;
        struct {
            /*
             * int16_t and 3 entries because of x, y and z axis are read,
             * each consisting of L + H byte parts
             */
            int16_t buffer[32][3];
        } gyro_data;
    };
};

#define GYRO_INIT_STEP_TIME 1

#define GYRO_RANGE 2000       // 2000 deg/s

/* Gyro register definitions */
#define GYRO_ADDRESS 0x69
#define GYRO_REG_CTRL_REG1 0x20
#define GYRO_REG_CTRL_REG1_DRBW_800_110 0xf0
#define GYRO_REG_CTRL_REG1_PD 0x08
#define GYRO_REG_CTRL_REG1_XYZ_ENABLE 0x07
#define GYRO_REG_CTRL_REG4 0x23
#define GYRO_REG_CTRL_REG4_FS_2000 0x30
#define GYRO_REG_CTRL_REG5 0x24
#define GYRO_REG_CTRL_REG5_FIFO_EN 0x40

#define GYRO_REG_FIFO_CTL 0x2e
#define GYRO_REG_FIFO_CTL_STREAM 0x40
#define GYRO_REG_FIFO_SRC 0x2f
#define GYRO_REG_FIFO_SRC_EMPTY 0x20
#define GYRO_REG_FIFO_SRC_ENTRIES_MASK 0x1f
#define GYRO_REG_FIFO_SRC_OVERRUN 0x40

#define GYRO_REG_WHO_AM_I 0x0f
#define GYRO_REG_WHO_AM_I_VALUE 0xd3
#define GYRO_REG_XL 0x28

/* this bit is ORd into the register to enable auto-increment mode */
#define GYRO_REG_AUTO_INCREMENT 0x80

#define DEG_TO_RAD 0.017453292519943295769236907684886f

/* running at 2000 degrees per second, at full range, with 16 bit signed
 * data, the datasheet specifies 70 mdps per bit
 */
#define GYRO_SCALE_R_S (70.0f * 0.001f)

static bool gyro_tick_do(void *data);

static int
gyro_timer_resched(struct gyroscope_l3g4200d_data *mdata,
    uint32_t timeout_ms,
    bool (*cb)(void *data))
{
    mdata->timer = sol_timeout_add(timeout_ms, cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
i2c_read_data_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;
    double scale = mdata->use_rad ? GYRO_SCALE_R_S * DEG_TO_RAD : GYRO_SCALE_R_S;
    uint8_t num_samples_available;
    struct sol_direction_vector val =
    {
        .min = -GYRO_RANGE,
        .max = GYRO_RANGE,
        .x = mdata->reading[0],
        .y = mdata->reading[1],
        .z = mdata->reading[2]
    };

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Failed to read L3G4200D gyro fifo status");
        return;
    }

    num_samples_available = status / (sizeof(int16_t) * 3);

    /* raw readings, with only the sensor-provided filtering */
    for (uint8_t i = 0; i < num_samples_available; i++) {
        mdata->reading[0] = mdata->gyro_data.buffer[i][0] * scale;
        mdata->reading[1] = -mdata->gyro_data.buffer[i][1] * scale;
        mdata->reading[2] = -mdata->gyro_data.buffer[i][2] * scale;
    }

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GYROSCOPE_L3G4200D__OUT__OUT, &val);

    mdata->pending_ticks--;
    if (mdata->pending_ticks)
        gyro_tick_do(mdata);
}

static void
i2c_read_fifo_status_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;
    uint8_t num_samples_available;
    uint8_t fifo_status = mdata->common.buffer[0];

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Failed to read L3G4200D gyro fifo status");
        return;
    }

    if (fifo_status & GYRO_REG_FIFO_SRC_OVERRUN) {
        num_samples_available = 32;
    } else if (fifo_status & GYRO_REG_FIFO_SRC_EMPTY) {
        num_samples_available = 0;
    } else {
        num_samples_available = fifo_status
            & GYRO_REG_FIFO_SRC_ENTRIES_MASK;
    }

    if (!num_samples_available) {
        SOL_INF("No samples available");
        return;
    }

    SOL_DBG("%d samples available", num_samples_available);

    /* Read *all* the entries in one go, using AUTO_INCREMENT */
    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c,
        GYRO_REG_XL | GYRO_REG_AUTO_INCREMENT,
        (uint8_t *)&mdata->gyro_data.buffer[0][0],
        sizeof(mdata->gyro_data.buffer), i2c_read_data_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Failed to read L3G4200D gyro samples");
}

static bool
gyro_tick_do(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_tick_do);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, GYRO_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", GYRO_ADDRESS);
        return false;
    }

    mdata->common.buffer[0] = 0;
    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c, GYRO_REG_FIFO_SRC,
        mdata->common.buffer, 1, i2c_read_fifo_status_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Failed to read L3G4200D gyro fifo status");

    return false;
}

static bool
gyro_ready(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->ready = true;
    SOL_DBG("gyro is ready for reading");

    if (mdata->pending_ticks)
        gyro_tick_do(mdata);

    return false;
}

static void
i2c_write_fifo_ctl_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set L3G4200D gyro sensor's stream mode");
        return;
    }

    if (gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_ready) < 0)
        SOL_WRN("error in scheduling a L3G4200D gyro's init command");
}

static bool
gyro_init_stream(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_init_stream);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, GYRO_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", GYRO_ADDRESS);
        return false;
    }

    /* enable FIFO in stream mode */
    mdata->common.buffer[0] = GYRO_REG_FIFO_CTL_STREAM;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, GYRO_REG_FIFO_CTL,
        mdata->common.buffer, 1, i2c_write_fifo_ctl_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set L3G4200D gyro sensor's stream mode");

    return false;
}

static void
i2c_write_ctrl_reg5_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set L3G4200D gyro sensor's fifo mode");
        return;
    }

    if (gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME,
        gyro_init_stream) < 0)
        SOL_WRN("error in scheduling a L3G4200D gyro's init command");
}

static bool
gyro_init_fifo(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_init_fifo);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, GYRO_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", GYRO_ADDRESS);
        return false;
    }

    mdata->common.buffer[0] = GYRO_REG_CTRL_REG5_FIFO_EN;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, GYRO_REG_CTRL_REG5,
        mdata->common.buffer, 1, i2c_write_ctrl_reg5_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set L3G4200D gyro sensor's fifo mode");

    return false;
}

static void
i2c_write_ctrl_reg4_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set L3G4200D gyro sensor's resolution");
        return;
    }

    if (gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME,
        gyro_init_fifo) < 0)
        SOL_WRN("error in scheduling a L3G4200D gyro's init command");
}

static bool
gyro_init_range(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_init_range);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, GYRO_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", GYRO_ADDRESS);
        return false;
    }

    /* setup for 2000 degrees/sec */
    mdata->common.buffer[0] = GYRO_REG_CTRL_REG4_FS_2000;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, GYRO_REG_CTRL_REG4,
        mdata->common.buffer, 1, i2c_write_ctrl_reg4_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set L3G4200D gyro sensor's resolution");

    return false;
}

static bool gyro_init_sampling(void *data);

static void
i2c_write_ctrl_reg1_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("could not set L3G4200D gyro sensor's sampling rate");
        return;
    }

    mdata->init_sampling_cnt--;

    if (gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME,
        mdata->init_sampling_cnt ?
        gyro_init_sampling : gyro_init_range) < 0) {
        SOL_WRN("error in scheduling a L3G4200D gyro's init command");
    }
}

/* meant to run 3 times */
static bool
gyro_init_sampling(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_init_sampling);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, GYRO_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", GYRO_ADDRESS);
        return false;
    }

    /* setup for 800Hz sampling with 110Hz filter */
    mdata->common.buffer[0] = GYRO_REG_CTRL_REG1_DRBW_800_110 |
        GYRO_REG_CTRL_REG1_PD | GYRO_REG_CTRL_REG1_XYZ_ENABLE;
    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, GYRO_REG_CTRL_REG1,
        mdata->common.buffer, 1, i2c_write_ctrl_reg1_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("could not set L3G4200D gyro sensor's sampling rate");

    return false;
}

static void
i2c_read_who_am_i_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct gyroscope_l3g4200d_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Failed to read i2c register");
        return;
    }

    if (mdata->common.buffer[0] != GYRO_REG_WHO_AM_I_VALUE) {
        SOL_WRN("could not find L3G4200D gyro sensor");
        return;
    }

    gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_init_sampling);
}

static bool
gyro_init(void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        gyro_timer_resched(mdata, GYRO_INIT_STEP_TIME, gyro_init);
        return false;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, GYRO_ADDRESS)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            GYRO_ADDRESS);
        return false;
    }

    mdata->i2c_pending = sol_i2c_read_register(mdata->i2c, GYRO_REG_WHO_AM_I,
        mdata->common.buffer, 1, i2c_read_who_am_i_cb, mdata);
    if (!mdata->i2c_pending)
        SOL_WRN("Failed to read i2c register");

    return false;
}

static int
gyroscope_l3g4200d_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct gyroscope_l3g4200d_data *mdata = data;
    const struct sol_flow_node_type_gyroscope_l3g4200d_options *opts =
        (const struct sol_flow_node_type_gyroscope_l3g4200d_options *)options;

    SOL_NULL_CHECK(options, -EINVAL);

    mdata->i2c = sol_i2c_open(opts->i2c_bus, I2C_SPEED);
    SOL_NULL_CHECK_MSG(mdata->i2c, -EIO, "Failed to open i2c bus");

    mdata->use_rad = opts->output_radians;
    mdata->init_sampling_cnt = 3;
    mdata->node = node;

    gyro_init(mdata);

    return 0;
}

static void
gyroscope_l3g4200d_close(struct sol_flow_node *node, void *data)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    if (mdata->i2c_pending)
        sol_i2c_pending_cancel(mdata->i2c, mdata->i2c_pending);
    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);

    if (mdata->timer)
        sol_timeout_del(mdata->timer);
}

static int
gyroscope_l3g4200d_tick(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gyroscope_l3g4200d_data *mdata = data;

    if (!mdata->ready || mdata->pending_ticks) {
        mdata->pending_ticks++;
        return 0;
    }

    gyro_tick_do(mdata);
    return 0;
}

#include "gyroscope-gen.c"
