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
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "si114x-gen.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-i2c.h"
#include "sol-types.h"
#include "sol-flow-internal.h"

/*
 * SI114X registers
 */
enum SI114X_REG {
    REG_PART_ID = 0x00,
    REG_REV_ID = 0x01,
    REG_SEQ_ID = 0x02,
    REG_INT_CFG = 0x03,
    REG_IRQ_ENABLE = 0x04,

    // these two are not documented in the datasheet,
    // but are mentioned there, as well as in the
    // Adafruit example
    REG_IRQ_MODE1 = 0x05,
    REG_IRQ_MODE2 = 0x06,

    REG_HW_KEY = 0x07,
    REG_MEAS_RATE0 = 0x08,
    REG_MEAS_RATE1 = 0x09,

    REG_PS_LED21 = 0x0f,
    REG_PS_LED3 = 0x10,

    REG_UCOEF0 = 0x13,
    REG_UCOEF1 = 0x14,
    REG_UCOEF2 = 0x15,
    REG_UCOEF3 = 0x16,
    REG_PARAM_WR = 0x17,
    REG_COMMAND = 0x18,

    REG_RESPONSE = 0x20,
    REG_IRQ_STATUS = 0x21,
    REG_ALS_VIS_DATA0 = 0x22,
    REG_ALS_VIS_DATA1 = 0x23,
    REG_ALS_IR_DATA0 = 0x24,
    REG_ALS_IR_DATA1 = 0x25,
    REG_PS1_DATA0 = 0x26,
    REG_PS1_DATA1 = 0x27,
    REG_PS2_DATA0 = 0x28,
    REG_PS2_DATA1 = 0x29,
    REG_PS3_DATA0 = 0x2a,
    REG_PS3_DATA1 = 0x2b,
    REG_AUX_UVINDEX0 = 0x2c,
    REG_AUX_UVINDEX1 = 0x2d,
    REG_PARAM_READ = 0x2e,

    REG_CHIP_STAT = 0x30,

    REG_ANA_IN_KEY0 = 0x3b,
    REG_ANA_IN_KEY1 = 0x3c,
    REG_ANA_IN_KEY2 = 0x3d,
    REG_ANA_IN_KEY3 = 0x3e
};

/*
 * Parameter memory (PARAM)
 */
enum SI114X_PARAM {
    PARAM_I2C_ADDDR = 0x00,
    PARAM_CHLIST = 0x01,
    PARAM_PSLED12_SEL = 0x02,
    PARAM_PSLED3_SEL = 0x03,

    PARAM_PS_ENCODING = 0x05,
    PARAM_ALS_ENCODING = 0x06,
    PARAM_PS1_ADCMUX = 0x07,
    PARAM_PS2_ADCMUX = 0x08,
    PARAM_PS3_ADCMUX = 0x09,
    PARAM_PS_ADC_COUNT = 0x0a,
    PARAM_PS_ADC_GAIN = 0x0b,
    PARAM_PS_ADC_MISC = 0x0c,

    PARAM_ALS_IR_ADCMUX = 0x0e,
    PARAM_AUX_ADCMUX = 0x0f,
    PARAM_ALS_VIS_ADC_COUNT = 0x10,
    PARAM_ALS_VIS_ADC_GAIN = 0x11,
    PARAM_ALS_VIS_ADC_MISC = 0x12,

    PARAM_LED_REC = 0x1c,
    PARAM_ALS_IR_ADC_COUNT = 0x1d,
    PARAM_ALS_IR_ADX_GAIN = 0x1e,
    PARAM_ALS_IR_ADC_MISC = 0x1f
};

/*
 * Commands (written to the REG_COMMAND register)
 */
enum SI114X_CMD {
    CMD_NOOP = 0x00, // clear RESPONSE reg
    CMD_RESET = 0x01,
    CMD_BUSADDR = 0x02,

    CMD_PS_FORCE = 0x05,
    CMD_GET_CAL = 0x12,
    CMD_ALS_FORCE = 0x06,
    CMD_PSALS_FORCE = 0x07,

    CMD_PS_PAUSE = 0x09,
    CMD_ALS_PAUSE = 0x0a,
    CMD_PSALS_PAUSE = 0x0b,

    CMD_PS_AUTO = 0x0d,
    CMD_ALS_AUTO = 0x0e,
    CMD_PSALS_AUTO = 0x0f,

    CMD_PARAM_QUERY = 0x80, // or'd with PARAM_T value
    CMD_PARAM_SET = 0xa0  // or'd with PARAM_T value
};

/*
 * Channel List enable bits
 */
enum SI114X_CHLIST_BITS {
    CHLIST_EN_PS1 = 0x01, // proximity sense 1-3
    CHLIST_EN_PS2 = 0x02,
    CHLIST_EN_PS3 = 0x04,

    CHLIST_EN_ALS_VIS = 0x10, // ambient light sense
    CHLIST_EN_ALS_IR = 0x20,
    CHLIST_EN_AUX = 0x40, // AUX sense
    CHLIST_EN_UV = 0x80  // UV sense
};

struct si114x_data {
    struct sol_i2c *context;
    struct sol_timeout *timer;
    bool fully_initialized;
    unsigned int init_step;
    int pendent_calls;
    uint16_t read_data;
    struct sol_flow_node *node;
};

#define SI114X_I2C_BUS 0
#define SI114X_DEFAULT_I2C_ADDR 0x60
#define SI114X_HW_KEY 0x17

struct i2c_initialization_data {
    uint8_t reg;
    uint8_t value;
    const char *error_str;
} initialization_data[] = {
    { .reg = REG_MEAS_RATE0, .value = 0, .error_str = "Couldn't reset the REG_MEAS_RATE0 register" },
    { .reg = REG_MEAS_RATE1, .value = 0, .error_str = "Couldn't reset the REG_MEAS_RATE1 register" },
    { .reg = REG_IRQ_MODE1, .value = 0, .error_str = "Couldn't reset the REG_IRQ_MODE1 register" },
    { .reg = REG_IRQ_MODE2, .value = 0, .error_str = "Couldn't reset the REG_IRQ_MODE2 register" },
    { .reg = REG_INT_CFG, .value = 0, .error_str = "Couldn't reset the REG_INT_CFG register" },
    { .reg = REG_IRQ_STATUS, .value = 0xff, .error_str = "Couldn't reset the REG_IRQ_STATUS register" },
    { .reg = REG_COMMAND, .value = CMD_RESET, .error_str = "Couldn't reset the device" },
    { .reg = REG_HW_KEY, .value = SI114X_HW_KEY, .error_str = "Couldn't set the REG_HW_KEY to SI114X_HW_KEY" },

    /* The uv calibration is a somewhat magic constant on the specs datasheet,
     * since there's no explanation on the datasheet about it, I'm keeping it here
     * as constants and not allowing it to be changed.
     * I have no idea what the constants are, got them from the datasheet and the upm
     * project.
     */
    { .reg = REG_UCOEF0, .value = 0x29, .error_str = "Couldn't setup UV calibration" },
    { .reg = REG_UCOEF1, .value = 0x89, .error_str = "Couldn't setup UV calibration" },
    { .reg = REG_UCOEF2, .value = 0x02, .error_str = "Couldn't setup UV calibration" },
    { .reg = REG_UCOEF3, .value = 0x00, .error_str = "Couldn't setup UV calibration" },
    { .reg = PARAM_CHLIST, .value = CHLIST_EN_UV, .error_str = "Couldn't enable UV sensor" },
    { .reg = REG_MEAS_RATE0, .value = 0xff, .error_str = "Couldn't enable UV sensor" },
    { .reg = REG_COMMAND, .value = CMD_ALS_AUTO, .error_str = "Couldn't enable UV sensor" }
};

static int si114x_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
void setup_device(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status);

static bool
busy_bus_callback(void *data)
{
    struct si114x_data *mdata = data;

    setup_device(mdata, mdata->context, 0, 0, 1);
    return false;
}

void
setup_device(void *cb_data, struct sol_i2c *i2c,  uint8_t reg, uint8_t *data, ssize_t status)
{
    struct si114x_data *mdata = cb_data;

    if (status < 0) {
        SOL_WRN("Couldn't open the si114x hardware for usage, please check the pinage.");
        return;
    }

    data = data; /* silence warning */

    if (mdata->init_step >= ARRAY_SIZE(initialization_data)) {
        while (mdata->pendent_calls) {
            si114x_process(mdata->node, mdata, SOL_FLOW_NODE_TYPE_LIGHT_SENSOR_SI114X__IN__TICK, 0, 0);
            mdata->pendent_calls--;
        }
        return;
    }
    if (!sol_i2c_busy(i2c)) {
        sol_i2c_write_register(i2c, initialization_data[mdata->init_step].reg, &initialization_data[mdata->init_step].value, 1, &setup_device, cb_data);
        mdata->init_step++;
    } else {
        sol_timeout_add(0, busy_bus_callback, mdata);
    }
}

static int
si114x_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct si114x_data *mdata = data;
    struct sol_flow_node_type_light_sensor_si114x_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_LIGHT_SENSOR_SI114X_OPTIONS_API_VERSION, -EINVAL);

    mdata->fully_initialized = false;
    opts = (struct sol_flow_node_type_light_sensor_si114x_options *)options;

    mdata->context = sol_i2c_open(opts->bus.val, opts->speed.val);
    if (!mdata->context) {
        SOL_WRN("Couldn't open the si114x hardware for usage, please check the pinage.");
        return -EIO;
    }
    mdata->pendent_calls = 0;
    mdata->node = node;
    mdata->init_step = 0;

    setup_device(mdata, mdata->context, 0, 0, 1);
    return 0;
}

static void
si114x_close(struct sol_flow_node *node, void *data)
{
    struct si114x_data *mdata = data;

    sol_timeout_del(mdata->timer);
    mdata->timer = NULL;
    sol_i2c_close(mdata->context);
}

static void
read_callback(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct si114x_data *mdata = cb_data;

    if (status < 0) {
        sol_flow_send_error_packet(mdata->node, EIO, "Error reading UV sensor");
        SOL_WRN("Couldn't read from device, check your UV reader (si114x)");
    } else {
        double value = (double)mdata->read_data / 100.0;
        sol_flow_send_drange_value_packet(mdata->node, SOL_FLOW_NODE_TYPE_LIGHT_SENSOR_SI114X__OUT__OUT,  value);
    }
}

static bool
do_processing(void *data)
{
    struct si114x_data *mdata = data;

    if (!sol_i2c_busy(mdata->context)) {
        if (!sol_i2c_read_register(mdata->context, REG_AUX_UVINDEX0, (uint8_t *)&mdata->read_data, 2, &read_callback, mdata)) {
            sol_flow_send_error_packet(mdata->node, EIO, "Couldn't read from device, check your UV reader (si114x)");
            SOL_WRN("Couldn't read from device, check your UV reader (si114x)");
        }
    } else {
        sol_timeout_add(0, &do_processing, mdata);
    }
    return false;
}

static int
si114x_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct si114x_data *mdata = data;

    if (!mdata->fully_initialized) {
        mdata->pendent_calls++;
        return 0;
    }

    do_processing(mdata);
    return 0;
}

#include "si114x-gen.c"
