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
enum _SI114X_REG {
    REG_PART_ID       = 0x00,
    REG_REV_ID        = 0x01,
    REG_SEQ_ID        = 0x02,
    REG_INT_CFG       = 0x03,
    REG_IRQ_ENABLE    = 0x04,

    // these two are not documented in the datasheet,
    // but are mentioned there, as well as in the
    // Adafruit example
    REG_IRQ_MODE1     = 0x05,
    REG_IRQ_MODE2     = 0x06,

    REG_HW_KEY        = 0x07,
    REG_MEAS_RATE0    = 0x08,
    REG_MEAS_RATE1    = 0x09,

    REG_PS_LED21      = 0x0f,
    REG_PS_LED3       = 0x10,

    REG_UCOEF0        = 0x13,
    REG_UCOEF1        = 0x14,
    REG_UCOEF2        = 0x15,
    REG_UCOEF3        = 0x16,
    REG_PARAM_WR      = 0x17,
    REG_COMMAND       = 0x18,

    REG_RESPONSE      = 0x20,
    REG_IRQ_STATUS    = 0x21,
    REG_ALS_VIS_DATA0 = 0x22,
    REG_ALS_VIS_DATA1 = 0x23,
    REG_ALS_IR_DATA0  = 0x24,
    REG_ALS_IR_DATA1  = 0x25,
    REG_PS1_DATA0     = 0x26,
    REG_PS1_DATA1     = 0x27,
    REG_PS2_DATA0     = 0x28,
    REG_PS2_DATA1     = 0x29,
    REG_PS3_DATA0     = 0x2a,
    REG_PS3_DATA1     = 0x2b,
    REG_AUX_UVINDEX0  = 0x2c,
    REG_AUX_UVINDEX1  = 0x2d,
    REG_PARAM_READ    = 0x2e,

    REG_CHIP_STAT     = 0x30,

    REG_ANA_IN_KEY0   = 0x3b,
    REG_ANA_IN_KEY1   = 0x3c,
    REG_ANA_IN_KEY2   = 0x3d,
    REG_ANA_IN_KEY3   = 0x3e
};
typedef enum _SI114X_REG SI114X_REG_T;

/*
 * Parameter memory (PARAM)
 */
enum _SI114X_PARAM {
    PARAM_I2C_ADDDR         = 0x00,
    PARAM_CHLIST            = 0x01,
    PARAM_PSLED12_SEL       = 0x02,
    PARAM_PSLED3_SEL        = 0x03,

    PARAM_PS_ENCODING       = 0x05,
    PARAM_ALS_ENCODING      = 0x06,
    PARAM_PS1_ADCMUX        = 0x07,
    PARAM_PS2_ADCMUX        = 0x08,
    PARAM_PS3_ADCMUX        = 0x09,
    PARAM_PS_ADC_COUNT      = 0x0a,
    PARAM_PS_ADC_GAIN       = 0x0b,
    PARAM_PS_ADC_MISC       = 0x0c,

    PARAM_ALS_IR_ADCMUX     = 0x0e,
    PARAM_AUX_ADCMUX        = 0x0f,
    PARAM_ALS_VIS_ADC_COUNT = 0x10,
    PARAM_ALS_VIS_ADC_GAIN  = 0x11,
    PARAM_ALS_VIS_ADC_MISC  = 0x12,

    PARAM_LED_REC           = 0x1c,
    PARAM_ALS_IR_ADC_COUNT  = 0x1d,
    PARAM_ALS_IR_ADX_GAIN   = 0x1e,
    PARAM_ALS_IR_ADC_MISC   = 0x1f
};
typedef enum _SI114X_PARAM SI114X_PARAM_T;

/*
 * Commands (written to the REG_COMMAND register)
 */
enum _SI114X_CMD {
    CMD_NOOP          = 0x00, // clear RESPONSE reg
    CMD_RESET         = 0x01,
    CMD_BUSADDR       = 0x02,

    CMD_PS_FORCE      = 0x05,
    CMD_GET_CAL       = 0x12,
    CMD_ALS_FORCE     = 0x06,
    CMD_PSALS_FORCE   = 0x07,

    CMD_PS_PAUSE      = 0x09,
    CMD_ALS_PAUSE     = 0x0a,
    CMD_PSALS_PAUSE   = 0x0b,

    CMD_PS_AUTO       = 0x0d,
    CMD_ALS_AUTO      = 0x0e,
    CMD_PSALS_AUTO    = 0x0f,

    CMD_PARAM_QUERY   = 0x80, // or'd with PARAM_T value
    CMD_PARAM_SET     = 0xa0  // or'd with PARAM_T value
};
typedef enum _SI114X_CMD SI114X_CMD_T;

/*
 * Channel List enable bits
 */
enum _SI114X_CHLIST_BITS {
    CHLIST_EN_PS1     = 0x01, // proximity sense 1-3
    CHLIST_EN_PS2     = 0x02,
    CHLIST_EN_PS3     = 0x04,

    CHLIST_EN_ALS_VIS = 0x10, // ambient light sense
    CHLIST_EN_ALS_IR  = 0x20,
    CHLIST_EN_AUX     = 0x40, // AUX sense
    CHLIST_EN_UV      = 0x80  // UV sense
};
typedef enum _SI114X_CHLIST_BITS SI114X_CHLIST_BITS_T;

union calibration {
    uint32_t data_raw;
    uint8_t data[4];
};

struct si114x_data {
    struct sol_i2c *context;
    struct sol_flow_node_type_light_sensor_si114x_options *opts;
    struct sol_timeout *timer;
    union calibration uv_calibration;
    bool fully_initialized;
};

#define SI114X_I2C_BUS 0
#define SI114X_DEFAULT_I2C_ADDR 0x60
#define SI114X_HW_KEY 0x17

static int
timer_reschedule(struct si114x_data *mdata,
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

static bool
setup_device_pt3(void *d)
{
    struct si114x_data *mdata = (struct si114x_data *)d;
    static uint8_t data;

    if (!sol_i2c_write_register(mdata->context, REG_UCOEF0, &mdata->uv_calibration.data[0], 1) ) {
        SOL_WRN("Couldn't setup UV calibration");
        return false;
    }

    if (!sol_i2c_write_register(mdata->context, REG_UCOEF1, &mdata->uv_calibration.data[1], 1)) {
        SOL_WRN("Couldn't setup UV calibration");
        return false;
    }

    if (!sol_i2c_write_register(mdata->context, REG_UCOEF2, &mdata->uv_calibration.data[2], 1)) {
        SOL_WRN("Couldn't setup UV calibration");
        return false;
    }

    if (!sol_i2c_write_register(mdata->context, REG_UCOEF3, &mdata->uv_calibration.data[3], 1)) {
        SOL_WRN("Couldn't setup UV calibration");
        return false;
    }

    // enable UV sensor only for now
    data = CHLIST_EN_UV;
    if (!sol_i2c_write_register(mdata->context, PARAM_CHLIST, &data, 1)) {
        SOL_WRN("Couldn't enable UV sensor");
        return false;
    }

    // auto-measure speed - slowest - (rate * 31.25us)
    data = 0xff;
    if (!sol_i2c_write_register(mdata->context, REG_MEAS_RATE0, &data, 1)) {
        SOL_WRN("Couldn't set measure speed");
        return false;
    }

    // set autorun
    data = CMD_ALS_AUTO;
    if (!sol_i2c_write_register(mdata->context, REG_COMMAND, &data, 1)) {
        SOL_WRN("Couldn't set mode to auto-run");
        return false;
    }
    mdata->fully_initialized = true;
    return false;
}

static bool
setup_device_pt2(void *d)
{
    struct si114x_data *mdata = (struct si114x_data *)d;
    uint8_t data = SI114X_HW_KEY;

    if (!sol_i2c_write_register(mdata->context, REG_HW_KEY, &data, 1)) {
        SOL_WRN("Couldn't set the REG_HW_KEY to SI114X_HW_KEY");
        return false;
    }
    timer_reschedule(mdata, 100, &setup_device_pt3, mdata);
    return false;
}

static bool
setup_device(void *d)
{
    struct si114x_data *mdata = (struct si114x_data *)d;
    uint8_t data;

    // Initialize the device to an empty state
    // zero out measuring rate
    data = 0;
    if (!sol_i2c_write_register(mdata->context, REG_MEAS_RATE0, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_MEAS_RATE0 register");
        return false;
    }

    if (!sol_i2c_write_register(mdata->context, REG_MEAS_RATE1, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_MEAS_RATE1 register");
        return false;
    }

    // disable IRQ MODES
    // these are undocumented in the datasheet, but mentioned in Adafruit's code
    if (!sol_i2c_write_register(mdata->context, REG_IRQ_MODE1, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_IRQ_MODE1 register");
        return false;
    }

    if (!sol_i2c_write_register(mdata->context, REG_IRQ_MODE2, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_IRQ_MODE2 register");
        return false;
    }

    // turn off interrupts
    if (!sol_i2c_write_register(mdata->context, REG_INT_CFG, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_INT_CFG register");
        return false;
    }

    data = 0xff;
    if (!sol_i2c_write_register(mdata->context, REG_IRQ_STATUS, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_IRQ_STATUS register");
        return false;
    }

    // send a reset
    data = CMD_RESET;
    if (!sol_i2c_write_register(mdata->context, REG_COMMAND, &data, 1)) {
        SOL_WRN("Couldn't reset the REG_COMMAND register");
        return false;
    }

    timer_reschedule(mdata, 100, &setup_device_pt2, mdata);
    return false;
}

static int
si114x_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct si114x_data *mdata = data;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_LIGHT_SENSOR_SI114X_OPTIONS_API_VERSION, -EINVAL);

    mdata->fully_initialized = false;
    mdata->opts = (struct sol_flow_node_type_light_sensor_si114x_options *)options;
    mdata->uv_calibration.data_raw = mdata->opts->calibration.val;
    mdata->context = sol_i2c_open(mdata->opts->bus.val, mdata->opts->speed.val);

    setup_device(mdata);
    return 0;
}

static void
si114x_close(struct sol_flow_node *node, void *data)
{
    struct si114x_data *mdata = data;

    sol_timeout_del(mdata->timer);
    sol_i2c_close(mdata->context);
}

static int
si114x_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct si114x_data *mdata = data;
    uint16_t word;
    double value;

    if (sol_i2c_read_register(mdata->context, REG_AUX_UVINDEX0, (uint8_t *)&word, 2) < 0) {
        sol_flow_send_error_packet(node, EIO, "Error reading UV sensor");
        return -EIO;
    } else {
        value = (double)word / 100.0;
        sol_flow_send_drange_value_packet(node, SOL_FLOW_NODE_TYPE_LIGHT_SENSOR_SI114X__OUT__OUT,  value);
    }
    return 0;
}

#include "si114x-gen.c"
