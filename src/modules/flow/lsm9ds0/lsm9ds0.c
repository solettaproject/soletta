/*
 * This file is part of the Soletta Project
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

#include "sol-flow/lsm9ds0.h"

#include "sol-flow-internal.h"
#include "sol-flow.h"
#include "sol-i2c.h"
#include "sol-mainloop.h"
#include "sol-types.h"
#include "sol-util.h"

// https://www.adafruit.com/datasheets/LSM9DS0.pdf

// ============================================================================
// I2C Dispatcher
// ============================================================================

#define MAX_RETRIES 3

enum i2c_op_dir {
    I2C_READ,
    I2C_WRITE
};

struct i2c_op {
    enum i2c_op_dir dir;
    uint8_t reg;
    uint8_t value;
};

struct i2c_blob {
    void (*cb)(void *cb_data, ssize_t status);
    void *cb_data;
    struct sol_i2c *i2c;
    struct sol_i2c_pending *pending;
    struct sol_timeout *timer;
    struct i2c_op *queue;
    size_t queue_len;
    size_t queue_idx;
    uint8_t retry;
    uint8_t addr;
};

static bool i2c_op_exec(void *data);

static void
i2c_op_done(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct i2c_blob *blob = cb_data;

    blob->pending = NULL;

    if (status <= 0) {
        SOL_ERR("[addr=0x%02x reg=0x%02x] I2C operation failed!", blob->addr, reg);
        goto exit;
    }

    if (++blob->queue_idx < blob->queue_len) {
        blob->retry = 0;
        blob->timer = sol_timeout_add(0, i2c_op_exec, blob);
        if (!blob->timer) {
            SOL_ERR("Failed to schedule I2C operation.");
            status = -1;
            goto exit;
        }

        return;
    }

exit:
    if (blob->cb)
        blob->cb(blob->cb_data, status);
}

static bool
i2c_op_exec(void *data)
{
    struct i2c_op *op;
    struct i2c_blob *blob = data;

    if (sol_i2c_busy(blob->i2c) || blob->pending) {
        if (++blob->retry >= MAX_RETRIES) {
            SOL_ERR("Failed to schedule I2C operation.");
            goto exit;
        }

        return true;
    }

    if (!sol_i2c_set_slave_address(blob->i2c, blob->addr)) {
        SOL_ERR("Failed to set slave at address 0x%02x on I2C bus.", blob->addr);
        goto exit;
    }

    op = &blob->queue[blob->queue_idx];

    //TODO: Check buf size and decide between r/w multiple/single byte
    blob->pending = (op->dir == I2C_WRITE) ?
        sol_i2c_write_register(blob->i2c, op->reg, &op->value, 1, i2c_op_done, blob) :
        sol_i2c_read_register(blob->i2c, op->reg, &op->value, 1, i2c_op_done, blob);

    if (!blob->pending) {
        blob->retry++;
        return true;
    }

exit:
    blob->timer = NULL;
    return false;
}

static bool
i2c_blob_start(struct i2c_blob *blob)
{
    blob->queue_idx = 0;
    blob->retry = 0;
    blob->timer = sol_timeout_add(0, i2c_op_exec, blob);
    SOL_NULL_CHECK(blob->timer, false);

    return true;
}

static void
i2c_blob_close(struct i2c_blob *blob)
{
    if (blob->timer)
        sol_timeout_del(blob->timer);

    if (blob->i2c) {
        if (blob->pending)
            sol_i2c_pending_cancel(blob->i2c, blob->pending);

        sol_i2c_close(blob->i2c);
    }
}

static void
_parse_raw_data(struct sol_direction_vector *dir, struct i2c_op *read_q, double constant)
{
    dir->x = ((int16_t)((read_q[1].value << 8) | read_q[0].value)) * constant;
    dir->y = ((int16_t)((read_q[3].value << 8) | read_q[2].value)) * constant;
    dir->z = ((int16_t)((read_q[5].value << 8) | read_q[4].value)) * constant;
}

// ============================================================================
// Gyroscope
// ============================================================================

/*
 * I2C Registers
 * Names extracted from the sensor datasheet
 */

#define WHO_AM_I 0x0F
#define WHO_AM_I_RET 0x0F

// Gyroscope control registers
#define CTRL_REG1_G 0x20
#define CTRL_REG2_G 0x21
#define CTRL_REG4_G 0x23
#define CTRL_REG5_G 0x24
#define FIFO_CTRL_REG 0x2E

// Gyroscope Axes
#define OUT_X_L_G 0x28
#define OUT_X_H_G 0x29
#define OUT_Y_L_G 0x2A
#define OUT_Y_H_G 0x2B
#define OUT_Z_L_G 0x2C
#define OUT_Z_H_G 0x2D

struct lsm9ds0_gyro_data {
    struct sol_flow_node *node;
    struct i2c_blob curr_queue;
    struct i2c_op init_q[5];
    struct i2c_op read_q[6];
    uint32_t scale;
    bool init;
};

static void
_read_gyro_done(void *cb_data, ssize_t status)
{
    struct sol_direction_vector gyro;
    struct lsm9ds0_gyro_data *mdata = cb_data;

    if (status <= 0) {
        SOL_ERR("Couldn't read LSM9DS0 Gyroscope.");
        return;
    }

    _parse_raw_data(&gyro, mdata->read_q, (mdata->scale / 32768.0));

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_GYROSCOPE__OUT__OUT, &gyro);
}

static int
lsm9ds0_gyro_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lsm9ds0_gyro_data *mdata = data;

    if (!mdata->init)
        return -1;

    if (!i2c_blob_start(&mdata->curr_queue))
        SOL_ERR("Couldn't schedule I2C read");

    return 0;
}

static void
_init_gyro_done(void *data, ssize_t status)
{
    struct lsm9ds0_gyro_data *mdata = data;

    if (status <= 0) {
        mdata->init = false;
        SOL_ERR("Couldn't initialize LSM9DS0 Gyroscope.");
        return;
    }

    mdata->curr_queue.cb = _read_gyro_done;
    mdata->curr_queue.cb_data = data;
    mdata->curr_queue.queue = mdata->read_q;
    mdata->curr_queue.queue_len = SOL_UTIL_ARRAY_SIZE(mdata->read_q);

    mdata->init = true;
}

static inline void
_init_gyro_queues(struct lsm9ds0_gyro_data *mdata, uint8_t scale)
{
    // Gyro X Y Z
    mdata->read_q[0].dir = I2C_READ;
    mdata->read_q[0].reg = OUT_X_L_G;
    mdata->read_q[1].dir = I2C_READ;
    mdata->read_q[1].reg = OUT_X_H_G;

    mdata->read_q[2].dir = I2C_READ;
    mdata->read_q[2].reg = OUT_Y_L_G;
    mdata->read_q[3].dir = I2C_READ;
    mdata->read_q[3].reg = OUT_Y_H_G;

    mdata->read_q[4].dir = I2C_READ;
    mdata->read_q[4].reg = OUT_Z_L_G;
    mdata->read_q[5].dir = I2C_READ;
    mdata->read_q[5].reg = OUT_Z_H_G;

    // Init
    mdata->init_q[0].dir = I2C_WRITE;
    mdata->init_q[0].reg = CTRL_REG1_G;
    mdata->init_q[0].value = 0x0F;

    mdata->init_q[1].dir = I2C_WRITE;
    mdata->init_q[1].reg = CTRL_REG2_G;
    mdata->init_q[1].value = 0x00;

    mdata->init_q[2].dir = I2C_WRITE;
    mdata->init_q[2].reg = CTRL_REG4_G;
    mdata->init_q[2].value = scale;

    mdata->init_q[3].dir = I2C_WRITE;
    mdata->init_q[3].reg = CTRL_REG5_G;
    mdata->init_q[3].value = 0x10;

    mdata->init_q[4].dir = I2C_WRITE;
    mdata->init_q[4].reg = FIFO_CTRL_REG;
    mdata->init_q[4].value = 0x00;

    mdata->curr_queue.cb = _init_gyro_done;
    mdata->curr_queue.cb_data = mdata;
    mdata->curr_queue.queue = mdata->init_q;
    mdata->curr_queue.queue_len = SOL_UTIL_ARRAY_SIZE(mdata->init_q);
}

static int
lsm9ds0_gyro_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    uint8_t scale_reg = 0x00;
    struct lsm9ds0_gyro_data *mdata = data;
    const struct sol_flow_node_type_lsm9ds0_gyroscope_options *opts =
        (const struct sol_flow_node_type_lsm9ds0_gyroscope_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(opts,
        SOL_FLOW_NODE_TYPE_LSM9DS0_GYROSCOPE_OPTIONS_API_VERSION, -EINVAL);

    mdata->node = node;
    mdata->init = false;

    mdata->curr_queue.i2c = sol_i2c_open(opts->i2c_bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->curr_queue.i2c, -EINVAL, "Failed to open i2c bus: %u", opts->i2c_bus);

    mdata->curr_queue.addr = opts->i2c_addr;
    mdata->scale = opts->scale;

    if (mdata->scale == 2000) {
        scale_reg = 0x0C;
    } else if (mdata->scale == 500) {
        scale_reg = 0x04;
    } else if (mdata->scale != 245) {
        SOL_WRN("Invalid Scale option. Using 245 dps. Valid options are: 245, 500, 2000.");
        mdata->scale = 245;
    }

    _init_gyro_queues(mdata, scale_reg);

    if (!i2c_blob_start(&mdata->curr_queue)) {
        SOL_ERR("Couldn't initialize LSM9DS0 Gyroscope.");
        return -1;
    }

    return 0;
}

static void
lsm9ds0_gyro_close(struct sol_flow_node *node, void *data)
{
    struct lsm9ds0_gyro_data *mdata = data;

    i2c_blob_close(&mdata->curr_queue);
}

// ============================================================================
// Accelerometer/Magnetometer/Temperature Sensor
// ============================================================================

/*
 * I2C Registers
 * Names extracted from the sensor datasheet
 */

// Control registers
#define CTRL_REG0_XM 0x1F
#define CTRL_REG1_XM 0x20
#define CTRL_REG2_XM 0x21
#define CTRL_REG5_XM 0x24
#define CTRL_REG6_XM 0x25

// Temperature
#define OUT_TEMP_L_XM 0x05
#define OUT_TEMP_H_XM 0x06

// Magnetometer Axes
#define OUT_X_L_M 0x08
#define OUT_X_H_M 0x09
#define OUT_Y_L_M 0x0A
#define OUT_Y_H_M 0x0B
#define OUT_Z_L_M 0x0C
#define OUT_Z_H_M 0x0D

// Accelerometer Axes
#define OUT_X_L_A 0x28
#define OUT_X_H_A 0x29
#define OUT_Y_L_A 0x2A
#define OUT_Y_H_A 0x2B
#define OUT_Z_L_A 0x2C
#define OUT_Z_H_A 0x2D

struct lsm9ds0_xmt_data {
    struct sol_flow_node *node;
    struct i2c_blob curr_queue;
    struct i2c_op init_q[6];
    struct i2c_op read_q[14];
    uint32_t accel_scale;
    uint32_t mag_scale;

    bool init;
};

static void
_read_xmt_done(void *cb_data, ssize_t status)
{
    uint32_t t;
    struct sol_direction_vector xmt;
    struct lsm9ds0_xmt_data *mdata = cb_data;

    if (status <= 0) {
        SOL_ERR("Couldn't read LSM9DS0 XMT device.");
        return;
    }

    _parse_raw_data(&xmt, mdata->read_q, (mdata->accel_scale / 32768.0));
    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_XMT__OUT__ACCEL, &xmt);

    _parse_raw_data(&xmt, &mdata->read_q[6], (mdata->mag_scale / 32768.0));
    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_XMT__OUT__MAG, &xmt);

    t = ((int16_t)((mdata->read_q[13].value << 8) | mdata->read_q[12].value));
    sol_flow_send_irange_value_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_XMT__OUT__TEMP, t);
}

static int
lsm9ds0_xmt_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lsm9ds0_xmt_data *mdata = data;

    if (!mdata->init)
        return -1;

    if (!i2c_blob_start(&mdata->curr_queue))
        SOL_ERR("Couldn't schedule I2C read");

    return 0;
}

static void
_init_xmt_done(void *data, ssize_t status)
{
    struct lsm9ds0_xmt_data *mdata = data;

    if (status <= 0) {
        mdata->init = false;
        SOL_ERR("Couldn't initialize LSM9DS0 XMT device.");
        return;
    }

    mdata->curr_queue.cb = _read_xmt_done;
    mdata->curr_queue.cb_data = data;
    mdata->curr_queue.queue = mdata->read_q;
    mdata->curr_queue.queue_len = SOL_UTIL_ARRAY_SIZE(mdata->read_q);

    mdata->init = true;
}

static inline void
_init_xmt_queues(struct lsm9ds0_xmt_data *mdata, uint8_t accel_reg, uint8_t mag_reg)
{
    // Accel X Y Z
    mdata->read_q[0].dir = I2C_READ;
    mdata->read_q[0].reg = OUT_X_L_A;
    mdata->read_q[1].dir = I2C_READ;
    mdata->read_q[1].reg = OUT_X_H_A;

    mdata->read_q[2].dir = I2C_READ;
    mdata->read_q[2].reg = OUT_Y_L_A;
    mdata->read_q[3].dir = I2C_READ;
    mdata->read_q[3].reg = OUT_Y_H_A;

    mdata->read_q[4].dir = I2C_READ;
    mdata->read_q[4].reg = OUT_Z_L_A;
    mdata->read_q[5].dir = I2C_READ;
    mdata->read_q[5].reg = OUT_Z_H_A;

    // Mag X Y Z
    mdata->read_q[6].dir = I2C_READ;
    mdata->read_q[6].reg = OUT_X_L_M;
    mdata->read_q[7].dir = I2C_READ;
    mdata->read_q[7].reg = OUT_X_H_M;

    mdata->read_q[8].dir = I2C_READ;
    mdata->read_q[8].reg = OUT_Y_L_M;
    mdata->read_q[9].dir = I2C_READ;
    mdata->read_q[9].reg = OUT_Y_H_M;

    mdata->read_q[10].dir = I2C_READ;
    mdata->read_q[10].reg = OUT_Z_L_M;
    mdata->read_q[11].dir = I2C_READ;
    mdata->read_q[11].reg = OUT_Z_H_M;

    // Temperature
    mdata->read_q[12].dir = I2C_READ;
    mdata->read_q[12].reg = OUT_TEMP_L_XM;
    mdata->read_q[13].dir = I2C_READ;
    mdata->read_q[13].reg = OUT_TEMP_H_XM;

    // Init
    mdata->init_q[0].dir = I2C_WRITE;
    mdata->init_q[0].reg = CTRL_REG0_XM;
    mdata->init_q[0].value = 0x00;

    mdata->init_q[1].dir = I2C_WRITE;
    mdata->init_q[1].reg = CTRL_REG1_XM;
    mdata->init_q[1].value = 0x87;

    mdata->init_q[2].dir = I2C_WRITE;
    mdata->init_q[2].reg = CTRL_REG2_XM;
    mdata->init_q[2].value = accel_reg;

    mdata->init_q[3].dir = I2C_WRITE;
    mdata->init_q[3].reg = CTRL_REG5_XM;
    mdata->init_q[3].value = 0xF4;

    mdata->init_q[4].dir = I2C_WRITE;
    mdata->init_q[4].reg = CTRL_REG6_XM;
    mdata->init_q[4].value = mag_reg;

    mdata->init_q[5].dir = I2C_WRITE;
    mdata->init_q[5].reg = FIFO_CTRL_REG;
    mdata->init_q[5].value = 0x00;

    mdata->curr_queue.cb = _init_xmt_done;
    mdata->curr_queue.cb_data = mdata;
    mdata->curr_queue.queue = mdata->init_q;
    mdata->curr_queue.queue_len = SOL_UTIL_ARRAY_SIZE(mdata->init_q);
}

static int
lsm9ds0_xmt_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    uint8_t accel_reg = 0x00;
    uint8_t mag_reg = 0x20;
    struct lsm9ds0_xmt_data *mdata = data;
    const struct sol_flow_node_type_lsm9ds0_xmt_options *opts =
        (const struct sol_flow_node_type_lsm9ds0_xmt_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(opts,
        SOL_FLOW_NODE_TYPE_LSM9DS0_XMT_OPTIONS_API_VERSION, -EINVAL);

    mdata->node = node;
    mdata->init = false;

    mdata->curr_queue.i2c = sol_i2c_open(opts->i2c_bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->curr_queue.i2c, -EINVAL, "Failed to open i2c bus: %u", opts->i2c_bus);

    mdata->curr_queue.addr = opts->i2c_addr;

    mdata->accel_scale = opts->accel_scale;
    mdata->mag_scale = opts->mag_scale;

    if (mdata->accel_scale == 4) {
        accel_reg = 0x08;
    } else if (mdata->accel_scale == 6 ) {
        accel_reg = 0x10;
    } else if (mdata->accel_scale == 8 ) {
        accel_reg = 0x18;
    } else if (mdata->accel_scale == 16 ) {
        accel_reg = 0x20;
    } else if (mdata->accel_scale != 2 ) {
        SOL_WRN("Invalid Accelerometer scale option. Using 2g. Valid options are: 2, 4, 6, 8 or 16.");
        mdata->accel_scale = 2;
    }

    if (mdata->mag_scale == 2) {
        mag_reg = 0x00;
    } else if (mdata->mag_scale == 8 ) {
        mag_reg = 0x40;
    } else if (mdata->mag_scale == 12 ) {
        mag_reg = 0x60;
    } else if (mdata->mag_scale != 4 ) {
        SOL_WRN("Invalid Magnetic scale option. Using 4 gauss. Valid options are: 2, 4, 8 or 12.");
        mdata->mag_scale = 4;
    }
    _init_xmt_queues(mdata, accel_reg, mag_reg);

    if (!i2c_blob_start(&mdata->curr_queue)) {
        SOL_ERR("Couldn't initialize LSM9DS0 XMT device.");
        return -1;
    }

    return 0;
}

static void
lsm9ds0_xmt_close(struct sol_flow_node *node, void *data)
{
    struct lsm9ds0_xmt_data *mdata = data;

    i2c_blob_close(&mdata->curr_queue);
}

#include "lsm9ds0-gen.c"
