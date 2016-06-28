/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
#include "sol-types.h"
#include "sol-util.h"
#include "sol-vector.h"

// https://www.adafruit.com/datasheets/LSM9DS0.pdf

#define SAMPLE_RES 32768.0 // 16 bit signed float resolution

static void
_parse_raw_data(struct sol_direction_vector *dir, struct sol_i2c_op *read_q, double constant)
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
    struct sol_i2c_op_set_pending *pending;
    struct sol_i2c *i2c;
    struct sol_vector queue;
    uint32_t scale;
    uint8_t addr;
    bool init;
};

static inline int
_gyro_init_queue(struct lsm9ds0_gyro_data *mdata, uint8_t scale)
{
    struct sol_i2c_op *op;

    sol_vector_init(&mdata->queue, sizeof(struct sol_i2c_op));
    op = sol_vector_append_n(&mdata->queue, 5);
    SOL_NULL_CHECK(op, -ENOMEM);

    op[0].type = SOL_I2C_WRITE;
    op[0].reg = CTRL_REG1_G;
    op[0].value = 0x0F;

    op[1].type = SOL_I2C_WRITE;
    op[1].reg = CTRL_REG2_G;
    op[1].value = 0x00;

    op[2].type = SOL_I2C_WRITE;
    op[2].reg = CTRL_REG4_G;
    op[2].value = scale;

    op[3].type = SOL_I2C_WRITE;
    op[3].reg = CTRL_REG5_G;
    op[3].value = 0x10;

    op[4].type = SOL_I2C_WRITE;
    op[4].reg = FIFO_CTRL_REG;
    op[4].value = 0x00;

    return 0;
}

static inline int
_gyro_read_queue(struct lsm9ds0_gyro_data *mdata)
{
    struct sol_i2c_op *op;

    sol_vector_init(&mdata->queue, sizeof(struct sol_i2c_op));
    op = sol_vector_append_n(&mdata->queue, 6);
    SOL_NULL_CHECK(op, -ENOMEM);

    op[0].type = SOL_I2C_READ;
    op[0].reg = OUT_X_L_G;
    op[1].type = SOL_I2C_READ;
    op[1].reg = OUT_X_H_G;

    op[2].type = SOL_I2C_READ;
    op[2].reg = OUT_Y_L_G;
    op[3].type = SOL_I2C_READ;
    op[3].reg = OUT_Y_H_G;

    op[4].type = SOL_I2C_READ;
    op[4].reg = OUT_Z_L_G;
    op[5].type = SOL_I2C_READ;
    op[5].reg = OUT_Z_H_G;

    return 0;
}

static void
_read_gyro_done(void *cb_data, ssize_t status)
{
    struct sol_direction_vector gyro;
    struct lsm9ds0_gyro_data *mdata = cb_data;

    mdata->pending = NULL;

    if (status <= 0) {
        SOL_ERR("Couldn't read LSM9DS0 Gyroscope.");
        sol_flow_send_error_packet_errno(mdata->node, EIO);
        return;
    }

    _parse_raw_data(&gyro, sol_vector_get_no_check(&mdata->queue, 0), (mdata->scale / SAMPLE_RES));

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_GYROSCOPE__OUT__OUT, &gyro);
}

static void
_init_gyro_done(void *data, ssize_t status)
{
    struct lsm9ds0_gyro_data *mdata = data;

    mdata->pending = NULL;

    if (status <= 0)
        goto error;

    sol_vector_clear(&mdata->queue);
    if (_gyro_read_queue(mdata))
        goto error;

    mdata->init = true;
    return;

error:
    SOL_ERR("Couldn't initialize LSM9DS0 Gyroscope.");
    sol_flow_send_error_packet_str(mdata->node, EINVAL,
        "Couldn't initialize LSM9DS0 Gyroscope.");
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

    mdata->i2c = sol_i2c_open(opts->i2c_bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->i2c, -EINVAL,
        "Failed to open i2c bus: %" PRId32 "", opts->i2c_bus);

    mdata->addr = opts->i2c_addr;
    mdata->scale = opts->scale;

    if (mdata->scale == 2000) {
        scale_reg = 0x0C;
    } else if (mdata->scale == 500) {
        scale_reg = 0x04;
    } else if (mdata->scale != 245) {
        SOL_WRN("Invalid Scale option. Using 245 dps. Valid options are: 245, 500, 2000.");
        mdata->scale = 245;
    }

    if (_gyro_init_queue(mdata, scale_reg))
        goto error;

    mdata->pending = sol_i2c_dispatcher_add_op_set(mdata->i2c, mdata->addr, &mdata->queue,
        _init_gyro_done, mdata, 0);

    if (!mdata->pending)
        goto error;

    return 0;

error:
    sol_i2c_close(mdata->i2c);
    SOL_ERR("Couldn't initialize LSM9DS0 Gyroscope.");
    return -EINVAL;
}

static void
lsm9ds0_gyro_close(struct sol_flow_node *node, void *data)
{
    struct lsm9ds0_gyro_data *mdata = data;

    if (mdata->i2c) {
        if (mdata->pending)
            sol_i2c_dispatcher_remove_op_set(mdata->i2c, mdata->pending);
        sol_i2c_close(mdata->i2c);
    }

    sol_vector_clear(&mdata->queue);
}

static int
lsm9ds0_gyro_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lsm9ds0_gyro_data *mdata = data;

    if (!mdata->init || mdata->pending)
        return -EAGAIN;

    mdata->pending = sol_i2c_dispatcher_add_op_set(mdata->i2c, mdata->addr, &mdata->queue,
        _read_gyro_done, mdata, 0);

    if (!mdata->pending) {
        SOL_ERR("Couldn't schedule I2C reads");
        return -1;
    }

    return 0;
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
    struct sol_i2c_op_set_pending *pending;
    struct sol_i2c *i2c;
    struct sol_vector queue;
    uint32_t accel_scale;
    uint32_t mag_scale;
    uint8_t addr;
    bool init;
};

static inline int
_xmt_init_queue(struct lsm9ds0_xmt_data *mdata, uint8_t accel_reg, uint8_t mag_reg)
{
    struct sol_i2c_op *op;

    sol_vector_init(&mdata->queue, sizeof(struct sol_i2c_op));
    op = sol_vector_append_n(&mdata->queue, 6);
    SOL_NULL_CHECK(op, -ENOMEM);

    op[0].type = SOL_I2C_WRITE;
    op[0].reg = CTRL_REG0_XM;
    op[0].value = 0x00;

    op[1].type = SOL_I2C_WRITE;
    op[1].reg = CTRL_REG1_XM;
    op[1].value = 0x87;

    op[2].type = SOL_I2C_WRITE;
    op[2].reg = CTRL_REG2_XM;
    op[2].value = accel_reg;

    op[3].type = SOL_I2C_WRITE;
    op[3].reg = CTRL_REG5_XM;
    op[3].value = 0xF4;

    op[4].type = SOL_I2C_WRITE;
    op[4].reg = CTRL_REG6_XM;
    op[4].value = mag_reg;

    op[5].type = SOL_I2C_WRITE;
    op[5].reg = FIFO_CTRL_REG;
    op[5].value = 0x00;

    return 0;
}

static inline int
_xmt_read_queue(struct lsm9ds0_xmt_data *mdata)
{
    struct sol_i2c_op *op;

    sol_vector_init(&mdata->queue, sizeof(struct sol_i2c_op));
    op = sol_vector_append_n(&mdata->queue, 14);
    SOL_NULL_CHECK(op, -ENOMEM);

    // Accel X Y Z
    op[0].type = SOL_I2C_READ;
    op[0].reg = OUT_X_L_A;
    op[1].type = SOL_I2C_READ;
    op[1].reg = OUT_X_H_A;

    op[2].type = SOL_I2C_READ;
    op[2].reg = OUT_Y_L_A;
    op[3].type = SOL_I2C_READ;
    op[3].reg = OUT_Y_H_A;

    op[4].type = SOL_I2C_READ;
    op[4].reg = OUT_Z_L_A;
    op[5].type = SOL_I2C_READ;
    op[5].reg = OUT_Z_H_A;

    // Mag X Y Z
    op[6].type = SOL_I2C_READ;
    op[6].reg = OUT_X_L_M;
    op[7].type = SOL_I2C_READ;
    op[7].reg = OUT_X_H_M;

    op[8].type = SOL_I2C_READ;
    op[8].reg = OUT_Y_L_M;
    op[9].type = SOL_I2C_READ;
    op[9].reg = OUT_Y_H_M;

    op[10].type = SOL_I2C_READ;
    op[10].reg = OUT_Z_L_M;
    op[11].type = SOL_I2C_READ;
    op[11].reg = OUT_Z_H_M;

    // Temperature
    op[12].type = SOL_I2C_READ;
    op[12].reg = OUT_TEMP_L_XM;
    op[13].type = SOL_I2C_READ;
    op[13].reg = OUT_TEMP_H_XM;

    return 0;
}

static void
_read_xmt_done(void *cb_data, ssize_t status)
{
    uint32_t t;
    struct sol_i2c_op *t_op;
    struct sol_direction_vector xmt;
    struct lsm9ds0_xmt_data *mdata = cb_data;

    mdata->pending = NULL;

    if (status <= 0) {
        SOL_ERR("Couldn't read LSM9DS0 XMT device.");
        sol_flow_send_error_packet_errno(mdata->node, EIO);
        return;
    }

    _parse_raw_data(&xmt, sol_vector_get_no_check(&mdata->queue, 0), (mdata->accel_scale / SAMPLE_RES));
    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_XMT__OUT__ACCEL, &xmt);

    _parse_raw_data(&xmt, sol_vector_get_no_check(&mdata->queue, 6), (mdata->mag_scale / SAMPLE_RES));
    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_LSM9DS0_XMT__OUT__MAG, &xmt);

    t_op = sol_vector_get_no_check(&mdata->queue, 13);
    t = ((int16_t)((t_op[1].value << 8) | t_op[0].value));
    sol_flow_send_irange_value_packet(mdata->node, SOL_FLOW_NODE_TYPE_LSM9DS0_XMT__OUT__TEMP, t);
}

static void
_init_xmt_done(void *data, ssize_t status)
{
    struct lsm9ds0_xmt_data *mdata = data;

    mdata->pending = NULL;

    if (status <= 0)
        goto error;

    sol_vector_clear(&mdata->queue);
    if (_xmt_read_queue(mdata))
        goto error;

    mdata->init = true;
    return;

error:
    SOL_ERR("Couldn't initialize LSM9DS0 XMT device.");
    sol_flow_send_error_packet_str(mdata->node, EINVAL,
        "Couldn't initialize LSM9DS0 XMT device.");
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

    mdata->i2c = sol_i2c_open(opts->i2c_bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(mdata->i2c, -EINVAL,
        "Failed to open i2c bus: %" PRId32 "", opts->i2c_bus);

    mdata->addr = opts->i2c_addr;

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

    if (_xmt_init_queue(mdata, accel_reg, mag_reg))
        goto error;

    mdata->pending = sol_i2c_dispatcher_add_op_set(mdata->i2c, mdata->addr, &mdata->queue,
        _init_xmt_done, mdata, 0);

    if (!mdata->pending)
        goto error;

    return 0;

error:
    sol_i2c_close(mdata->i2c);
    SOL_ERR("Couldn't initialize LSM9DS0 XMT device.");
    return -EINVAL;
}

static void
lsm9ds0_xmt_close(struct sol_flow_node *node, void *data)
{
    struct lsm9ds0_xmt_data *mdata = data;

    if (mdata->i2c) {
        if (mdata->pending)
            sol_i2c_dispatcher_remove_op_set(mdata->i2c, mdata->pending);
        sol_i2c_close(mdata->i2c);
    }

    sol_vector_clear(&mdata->queue);
}

static int
lsm9ds0_xmt_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lsm9ds0_xmt_data *mdata = data;

    if (!mdata->init || mdata->pending)
        return -EAGAIN;

    mdata->pending = sol_i2c_dispatcher_add_op_set(mdata->i2c, mdata->addr, &mdata->queue,
        _read_xmt_done, mdata, 0);

    if (!mdata->pending) {
        SOL_ERR("Couldn't schedule I2C reads");
        return -1;
    }

    return 0;
}

#include "lsm9ds0-gen.c"
