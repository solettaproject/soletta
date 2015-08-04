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

#include <sol-util.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>

#include "grove-gen.h"
#include "aio-gen.h"

#include "sol-flow-static.h"
#include "sol-flow-internal.h"
#include "sol-i2c.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"


// ################################ Rotary sensor nodes

#define ROTARY_CONVERTER_NODE_IDX 0
#define ROTARY_AIO_READER_NODE_IDX 1

struct rotary_converter_data {
    int angular_range;
    int input_range;
};

static int
rotary_child_opts_set(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_rotary_sensor_options *container_opts = (struct sol_flow_node_type_grove_rotary_sensor_options *)opts;

    if (child_index == ROTARY_CONVERTER_NODE_IDX) {
        struct sol_flow_node_type_grove_rotary_converter_options *converter_opts =
            (struct sol_flow_node_type_grove_rotary_converter_options *)child_opts;
        converter_opts->angular_range = container_opts->angular_range;
        converter_opts->input_range_mask = container_opts->mask;
    } else if (child_index == ROTARY_AIO_READER_NODE_IDX) {
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        reader_opts->device = container_opts->device;
        reader_opts->pin = container_opts->pin;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }

    return 0;
}

static void
grove_rotary_sensor_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;

    static struct sol_flow_static_node_spec nodes[] = {
        { NULL, "rotary-converter", NULL },
        { NULL, "aio-reader", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { 1, SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__DEG },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAD },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        .api_version = SOL_FLOW_STATIC_API_VERSION,
        .nodes = nodes,
        .conns = conns,
        .exported_out = exported_out,
        .child_opts_set = rotary_child_opts_set,
    };

    nodes[0].type = SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER;
    nodes[1].type = SOL_FLOW_NODE_TYPE_AIO_READER;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

static void
rotary_sensor_init_type(void)
{
    grove_rotary_sensor_new_type(&SOL_FLOW_NODE_TYPE_GROVE_ROTARY_SENSOR);
}

static int
rotary_converter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct rotary_converter_data *mdata = data;
    const struct sol_flow_node_type_grove_rotary_converter_options *opts =
        (const struct sol_flow_node_type_grove_rotary_converter_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER_OPTIONS_API_VERSION, -EINVAL);

    mdata->angular_range = opts->angular_range.val;
    mdata->input_range = 1 << opts->input_range_mask.val;

    return 0;
}

static int
rotary_converter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    float degrees;
    struct rotary_converter_data *mdata = data;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    degrees = (float)in_value.val * (float)mdata->angular_range / mdata->input_range;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__DEG,
        degrees);
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAD,
        degrees * M_PI / 180.0);
    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_ROTARY_CONVERTER__OUT__RAW,
        in_value.val);

    return 0;
}

// ################################ Light sensor nodes

#define LIGHT_CONVERTER_NODE_IDX 0
#define LIGHT_AIO_READER_NODE_IDX 1

struct light_converter_data {
    int input_range;
};

static int
light_child_opts_set(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_light_sensor_options *container_opts = (struct sol_flow_node_type_grove_light_sensor_options *)opts;

    if (child_index == LIGHT_CONVERTER_NODE_IDX) {
        struct sol_flow_node_type_grove_light_converter_options *converter_opts = (struct sol_flow_node_type_grove_light_converter_options *)child_opts;
        converter_opts->input_range_mask = container_opts->mask;
    } else if (child_index == LIGHT_AIO_READER_NODE_IDX) {
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        reader_opts->device = container_opts->device;
        reader_opts->pin = container_opts->pin;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }

    return 0;
}

static void
grove_light_sensor_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;

    static struct sol_flow_static_node_spec nodes[] = {
        { NULL, "light-converter", NULL },
        { NULL, "aio-reader", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { 1, SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, 0, SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__LUX },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        .api_version = SOL_FLOW_STATIC_API_VERSION,
        .nodes = nodes,
        .conns = conns,
        .exported_out = exported_out,
        .child_opts_set = light_child_opts_set,
    };

    nodes[0].type = SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER;
    nodes[1].type = SOL_FLOW_NODE_TYPE_AIO_READER;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

static void
light_sensor_init_type(void)
{
    grove_light_sensor_new_type(&SOL_FLOW_NODE_TYPE_GROVE_LIGHT_SENSOR);
}

static int
light_converter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct light_converter_data *mdata = data;
    const struct sol_flow_node_type_grove_light_converter_options *opts =
        (const struct sol_flow_node_type_grove_light_converter_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER_OPTIONS_API_VERSION, -EINVAL);

    mdata->input_range = 1 << opts->input_range_mask.val;

    return 0;
}

static int
light_converter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    float a;
    struct light_converter_data *mdata = data;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    // The following calculations follow the exponential best fit
    // (found using least squares) for the values suggested for LUX
    // on the table found on Grove Starter Kit for Arduino booklet
    // Least squares best fit: 0.152262 e^(0.00782118 x)
    // First row below maps input_range to 0-1023 range, used on booklet table.
    a = (float)in_value.val * 1023 / mdata->input_range;
    a = 0.152262 * exp(0.00782118 * (a));
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__LUX,
        a);
    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_LIGHT_CONVERTER__OUT__RAW,
        in_value.val);

    return 0;
}

// ################################ Temperature sensor nodes

#define TEMPERATURE_CONVERTER_NODE_IDX 0
#define TEMPERATURE_AIO_READER_NODE_IDX 1

struct temperature_converter_data {
    int thermistor_constant;
    int input_range;
    int resistance;
    int thermistor_resistance;
    float reference_temperature;
};

static int
temperature_converter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct temperature_converter_data *mdata = data;
    const struct sol_flow_node_type_grove_temperature_converter_options *opts =
        (const struct sol_flow_node_type_grove_temperature_converter_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER_OPTIONS_API_VERSION, -EINVAL);

    mdata->thermistor_constant = opts->thermistor_constant.val;
    mdata->input_range = 1 << opts->input_range_mask.val;
    mdata->resistance = opts->resistance.val;
    mdata->reference_temperature = opts->reference_temperature.val;
    mdata->thermistor_resistance = opts->thermistor_resistance.val;

    return 0;
}

static int
temperature_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;
    float resistance, temperature_kelvin;
    struct temperature_converter_data *mdata = data;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    resistance = (float)(mdata->input_range - in_value.val) * mdata->resistance / in_value.val;
    temperature_kelvin = 1 / (log(resistance / mdata->thermistor_resistance) / mdata->thermistor_constant + 1 / mdata->reference_temperature);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__CELSIUS,
        temperature_kelvin - 273.15);
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT,
        temperature_kelvin * 9 / 5 - 459.67);
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__KELVIN,
        temperature_kelvin);

    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__RAW,
        in_value.val);

    return 0;
}

static int
temperature_child_opts_set(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_node_type_grove_temperature_sensor_options *container_opts = (struct sol_flow_node_type_grove_temperature_sensor_options *)opts;

    if (child_index == TEMPERATURE_CONVERTER_NODE_IDX) {
        struct sol_flow_node_type_grove_temperature_converter_options *converter_opts =
            (struct sol_flow_node_type_grove_temperature_converter_options *)child_opts;
        converter_opts->thermistor_constant = container_opts->thermistor_constant;
        converter_opts->input_range_mask = container_opts->mask;
        converter_opts->resistance = container_opts->resistance;
        converter_opts->reference_temperature = container_opts->reference_temperature;
        converter_opts->thermistor_resistance = container_opts->thermistor_resistance;
    } else if (child_index == TEMPERATURE_AIO_READER_NODE_IDX) {
        struct sol_flow_node_type_aio_reader_options *reader_opts = (struct sol_flow_node_type_aio_reader_options *)child_opts;
        reader_opts->device = container_opts->device;
        reader_opts->pin = container_opts->pin;
        reader_opts->mask = container_opts->mask;
        reader_opts->poll_timeout = container_opts->poll_timeout;
    }

    return 0;
}

static void
grove_temperature_sensor_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;

    static struct sol_flow_static_node_spec nodes[] = {
        { NULL, "temperature-converter", NULL },
        { NULL, "aio-reader", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { 1, SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__CELSIUS },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__KELVIN },
        { 0, SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER__OUT__RAW },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        .api_version = SOL_FLOW_STATIC_API_VERSION,
        .nodes = nodes,
        .conns = conns,
        .exported_out = exported_out,
        .child_opts_set = temperature_child_opts_set,
    };

    nodes[0].type = SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_CONVERTER;
    nodes[1].type = SOL_FLOW_NODE_TYPE_AIO_READER;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

static void
temperature_init_type(void)
{
    grove_temperature_sensor_new_type(&SOL_FLOW_NODE_TYPE_GROVE_TEMPERATURE_SENSOR);
}

// ################################ LCD nodes

/* TODO move me to options - speed only works for riot */
#define I2C_BUS 0
#define I2C_SPEED SOL_I2C_SPEED_10KBIT

#define COL_MIN (0)
#define COL_MAX (15)
#define COL_EXTRA (16) /* when writing RTL, the cursor must be past
                        * the screen to get it right */
#define ROW_MIN (0)
#define ROW_MAX (1)

static const uint8_t RGB_ADDR = 0xc4 >> 1;
static const uint8_t COLOR_ADDR[3] = { 0x04, 0x03, 0x02 };
static const uint8_t DISPLAY_ADDR = 0x7c >> 1;
static const uint8_t ROW_ADDR[2] = { 0x80, 0xc0 };
static const uint8_t ROW_OUT_MASK = 0x3f;

static const uint8_t SEND_DATA = 0x40;
static const uint8_t SEND_COMMAND = 0x80;

enum command_status {
    COMMAND_STATUS_WAITING = 0,
    COMMAND_STATUS_SENDING,
    COMMAND_STATUS_DONE
};

struct command {
    char *string;
    uint8_t chip_addr, data_addr, value;
    enum command_status status;
};

struct lcd_data {
    struct sol_i2c *i2c;
    void *i2c_pending;
    struct sol_timeout *timer;
    struct sol_vector cmd_queue;
    unsigned char row, col;
    uint8_t display_mode, display_control;
    uint8_t error : 1;
};

#define LCD_BLINK_ON (0x01)
#define LCD_CURSOR_ON (0x02)
#define LCD_MODE_SET_LTR (0x02)
#define LCD_MODE_SET_AUTO_SCROLL (0x01)

struct timespec initial, prev;

static bool
command_queue(struct lcd_data *mdata, uint8_t chip_addr, uint8_t data_addr, uint8_t value)
{
    struct command *cmd;

    cmd = sol_vector_append(&mdata->cmd_queue);
    SOL_NULL_CHECK(cmd, false);
    cmd->chip_addr = chip_addr;
    cmd->data_addr = data_addr;
    cmd->value = value;
    cmd->status = COMMAND_STATUS_WAITING;
    cmd->string = NULL;

    return true;
}

static bool
command_string_queue(struct lcd_data *mdata, uint8_t chip_addr, uint8_t data_addr, uint8_t value, char *string)
{
    struct command *cmd;

    if (mdata->error) return false;

    cmd = sol_vector_append(&mdata->cmd_queue);
    SOL_NULL_CHECK(cmd, false);
    cmd->chip_addr = chip_addr;
    cmd->data_addr = data_addr;
    cmd->value = value;
    cmd->status = COMMAND_STATUS_WAITING;
    cmd->string = string;

    return true;
}

static bool
write_position(struct lcd_data *mdata, uint8_t row, uint8_t col)
{
    uint8_t command = col | ROW_ADDR[row];

    return command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND, command);
}

/* Returns the number of chars behind the current cursor position,
 * if in ltr, or after that, if in rtl */
static int
write_char(struct lcd_data *mdata, char value)
{
    bool ret;
    bool rtl;
    bool autoscroll;

    ret = command_queue(mdata, DISPLAY_ADDR, SEND_DATA, value);
    if (!ret)
        return -EIO;

    autoscroll = mdata->display_mode & LCD_MODE_SET_AUTO_SCROLL;
    /* when autoscrolling, don't advance in either way */
    if (autoscroll)
        return ret;

    rtl = !(mdata->display_mode & LCD_MODE_SET_LTR);

    if (rtl) {
        mdata->col--;
        /* going rtl case (checking underflow on unsigned):
         * jump to end of 1st line or keep overriding first
         * col */
        if (mdata->col == UINT8_MAX) {
            if (mdata->row == ROW_MAX) {
                mdata->col = COL_EXTRA;
                if (!write_position(mdata, mdata->row--, mdata->col)) {
                    SOL_WRN("Failed to change cursor position");
                    return false;
                }
            } else
                mdata->col = COL_MIN;
        }
    } else {
        mdata->col++;
        /* going ltr case: jump to start of second line or
         * keep overriding last col */
        if (mdata->col > COL_MAX) {
            if (mdata->row < ROW_MAX) {
                mdata->col = COL_MIN;
                if (!write_position(mdata, mdata->row++, mdata->col)) {
                    SOL_WRN("Failed to change cursor position");
                    return false;
                }
            } else
                mdata->col = COL_MAX;
        }
    }

    if (rtl)
        return mdata->col + (1 + COL_MAX) * mdata->row;
    else
        return (ROW_MAX - mdata->row) * (1 + COL_MAX) + (COL_MAX - mdata->col);
}

static bool
write_string(struct lcd_data *mdata, char *str)
{
    int ret;

    while ((*str) && (*str != '\0')) {
        ret = write_char(mdata, *str++);
        if (!ret)
            return false;
        /* stop if the whole display was used */
        if (ret >= (COL_MAX + 1) * (ROW_MAX + 1) - 1)
            return true;
    }

    return true;
}

#define LCD_CLEAR (0x01)
#define LCD_STRING_WRITE (0xFF) /* not to be sent via i2c, internal use */
#define LCD_ENTRY_MODE_SET (0x04)
#define LCD_DISPLAY_CONTROL (0x08)
#define LCD_FUNCTION_SET (0x20)
#define LCD_DISPLAY_ON (0x04)
#define LCD_FUNCTION_SET_2_LINES (0x08)

#define LCD_CURSOR_SHIFT (0x10)
#define LCD_DISPLAY_MOVE (0x08)
#define LCD_MOVE_RIGHT (0x04)
#define LCD_MOVE_LEFT (0x00)

#define LCD_RGB_MODE1 (0x00)
#define LCD_RGB_MODE2 (0x01)
#define LCD_RGB_OUTPUT (0x08)

#define LCD_COLOR_R (0)
#define LCD_COLOR_G (1)
#define LCD_COLOR_B (2)

#define TIME_TO_CLEAR (2)

#define I2C_STEP_TIME 1

static bool command_queue_process(void *data);

static int
timer_reschedule(struct lcd_data *mdata,
    unsigned int timeout_ms,
    bool (*cb)(void *data))
{
    mdata->timer = sol_timeout_add(timeout_ms, cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    return 0;
}

static void
i2c_write_cb(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status)
{
    struct lcd_data *mdata = cb_data;

    mdata->i2c_pending = NULL;
    if (status < 0) {
        SOL_ERR("Failed to process i2c command, no new commands will be executed.");
        return;
    }

    command_queue_process(cb_data);
}

static bool
command_send(struct lcd_data *mdata, struct command *cmd)
{
    struct timespec now, result;

    if (!sol_i2c_set_slave_address(mdata->i2c, cmd->chip_addr)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n", cmd->chip_addr);
        return false;
    }

    now = sol_util_timespec_get_current();
    sol_util_timespec_sub(&now, &initial, &result);

    printf("sec: %lu microsec: %lu | ChipAddr: 0x%02x, DataAddr: 0x%02x, Value 0x%02x - %c\n",
        result.tv_sec, result.tv_nsec / NSEC_PER_USEC, cmd->chip_addr, cmd->data_addr, cmd->value, cmd->value);

    mdata->i2c_pending = sol_i2c_write_register(mdata->i2c, cmd->data_addr,
        &cmd->value, 1, i2c_write_cb, mdata);
    return mdata->i2c_pending;
}

static void
command_free(struct command *cmd)
{
    if (cmd->string) {
        free(cmd->string);
    }
}

static void
command_copy(struct command *src, struct command *dst)
{
    memcpy(dst, src, sizeof(struct command));
}

static bool
lcd_string_write_process(struct lcd_data *mdata, char *string, uint16_t i)
{
    struct sol_vector old_vector;
    struct sol_vector final_vector;
    struct command *cmd_src, *cmd_dst;
    uint16_t j;

    /*
     * The commands that will be queued by write_string() must be right
     * after the command that trigged this function, to accomplish this
     * with vector:
     * - Copy mdata->cmd_queue vector to old_vector
     * - Initialize mdata->cmd_queue
     * - Call write_string() to queued the commands to mdata->cmd_queue
     * - Initialize the final_vector
     * - Append all processed(0..i) commands on old_vector to final_vector
     * - Append all commands queued by write_string() to final_vector
     * - Append all non-processed(i+1..len) commands on old_vector to final_vector
     * - Set final_vector as mdata->cmd_queue
     */

    old_vector = mdata->cmd_queue;
    sol_vector_init(&mdata->cmd_queue, sizeof(struct command));

    if (!write_string(mdata, string)) {
        sol_vector_clear(&mdata->cmd_queue);
        goto err;
    }

    sol_vector_init(&final_vector, sizeof(struct command));
    SOL_VECTOR_FOREACH_IDX (&old_vector, cmd_src, j) {
        if (j > i) break;
        cmd_dst = sol_vector_append(&final_vector);
        if (!cmd_dst) {
            sol_vector_clear(&mdata->cmd_queue);
            sol_vector_clear(&final_vector);
            goto err;
        }
        command_copy(cmd_src, cmd_dst);
    }

    SOL_VECTOR_FOREACH_IDX (&mdata->cmd_queue, cmd_src, j) {
        cmd_dst = sol_vector_append(&final_vector);
        if (!cmd_dst) {
            sol_vector_clear(&mdata->cmd_queue);
            sol_vector_clear(&final_vector);
            goto err;
        }
        command_copy(cmd_src, cmd_dst);
    }
    sol_vector_clear(&mdata->cmd_queue);

    SOL_VECTOR_FOREACH_IDX (&old_vector, cmd_src, j) {
        if (j <= i) continue;
        cmd_dst = sol_vector_append(&final_vector);
        if (!cmd_dst) {
            sol_vector_clear(&final_vector);
            goto err;
        }
        command_copy(cmd_src, cmd_dst);
    }
    sol_vector_clear(&old_vector);

    mdata->cmd_queue = final_vector;

    return true;
err:
    mdata->cmd_queue = old_vector;
    return false;
}

static void
command_queue_start(struct lcd_data *mdata)
{
    if (mdata->cmd_queue.len)
        return;
    command_queue_process(mdata);
}

/* write buffered changes */
static bool
command_queue_process(void *data)
{
    struct lcd_data *mdata = data;
    struct command *cmd;
    uint16_t i;

    mdata->timer = NULL;
    if (sol_i2c_busy(mdata->i2c)) {
        if (timer_reschedule(mdata, I2C_STEP_TIME, command_queue_process))
            goto reshedule_error;
        return false;
    }

    SOL_VECTOR_FOREACH_IDX (&mdata->cmd_queue, cmd, i) {

        if (cmd->status == COMMAND_STATUS_WAITING) {
            if (cmd->chip_addr == DISPLAY_ADDR &&
                cmd->data_addr == SEND_COMMAND &&
                cmd->value == LCD_STRING_WRITE) {
                /* being a fake cmd, we do it with the due function */
                SOL_EXP_CHECK_GOTO(!lcd_string_write_process(mdata, cmd->string, i), err);
                cmd->status = COMMAND_STATUS_DONE;
                continue;
            }

            SOL_EXP_CHECK_GOTO(!command_send(mdata, cmd), err);
            cmd->status = COMMAND_STATUS_SENDING;
            break;
        }

        cmd->status = COMMAND_STATUS_DONE;
        if (cmd->chip_addr == DISPLAY_ADDR && cmd->data_addr == SEND_COMMAND) {
            if (cmd->value == LCD_CLEAR) {
                mdata->row = ROW_MIN;
                mdata->col = COL_MIN;
                if (timer_reschedule(mdata, TIME_TO_CLEAR, command_queue_process))
                    goto reshedule_error;
                break;
            }

            /* we're left to change cursor, display on/off,
             * set ltr, scroll left/right & autoscroll
             * commands. we detect cursor commands by the
             * value's bit pattern and to the extra actions
             * for them */
            if (cmd->value & ROW_ADDR[ROW_MIN]) {
                mdata->row = ROW_MIN;
                mdata->col = cmd->value & ROW_OUT_MASK;
            } else if (cmd->value & ROW_ADDR[ROW_MAX]) {
                mdata->row = ROW_MAX;
                mdata->col = cmd->value & ROW_OUT_MASK;
            }
        }
    }

    /* Traverse backwards so deletion doesn't impact the indices. */
    SOL_VECTOR_FOREACH_REVERSE_IDX (&mdata->cmd_queue, cmd, i) {
        if (cmd->status == COMMAND_STATUS_DONE) {
            command_free(cmd);
            sol_vector_del(&mdata->cmd_queue, i);
        }
    }

    return 0;

err:
    SOL_ERR("Failed to process LCD command, no new commands will be executed.");
    mdata->error = true;
    return false;

reshedule_error:
    SOL_WRN("Fail to reschedule LCD command queue, no new commands will be executed");
    mdata->error = true;
    return false;
}

static bool
setup(void *data)
{
    struct lcd_data *mdata = data;
    bool r;

    mdata->timer = NULL;

    /* set display to 2 lines */
    r = command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        LCD_FUNCTION_SET | LCD_FUNCTION_SET_2_LINES);
    if (!r)
        goto fail;
    /* turn on display */
    r = command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON);
    if (!r)
        goto fail;
    /* clear display */
    r = command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND, LCD_CLEAR);
    if (!r)
        goto fail;
    r = command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND, mdata->display_mode);
    if (!r)
        goto fail;
    r = command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_control);
    if (!r)
        goto fail;

    r = command_queue(mdata, RGB_ADDR, LCD_RGB_MODE1, 0);
    if (!r)
        goto fail;
    r = command_queue(mdata, RGB_ADDR, LCD_RGB_MODE2, 0);
    if (!r)
        goto fail;
    r = command_queue(mdata, RGB_ADDR, LCD_RGB_OUTPUT, 0xAA);
    if (!r)
        goto fail;

    command_queue_process(mdata);
    return true;

fail:
    SOL_WRN("Unnable to append one of LCD setup commands.");
    return false;
}

#undef LCD_FUNCTION_SET
#undef LCD_FUNCTION_SET_2_LINES
#undef LCD_RGB_MODE1
#undef LCD_RGB_MODE2
#undef LCD_RGB_OUTPUT
#undef LCD_COLOR_R
#undef LCD_COLOR_G
#undef LCD_COLOR_B

static int
lcd_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct lcd_data *mdata = data;
    static const unsigned int TIME_TO_TURN_ON = 55;

    mdata->i2c = sol_i2c_open(I2C_BUS, I2C_SPEED);
    if (!mdata->i2c) {
        SOL_WRN("Failed to open i2c bus");
        return -EIO;
    }

    sol_vector_init(&mdata->cmd_queue, sizeof(struct command));

    mdata->display_mode = LCD_ENTRY_MODE_SET | LCD_MODE_SET_LTR;
    mdata->display_control = (LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON)
        & (~LCD_BLINK_ON | ~LCD_CURSOR_ON);

    return timer_reschedule(mdata, TIME_TO_TURN_ON, setup);
}

static int
lcd_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    return lcd_open(node, data, options);
}

#undef I2C_BUS
#undef I2C_SPEED

static void
lcd_close(struct sol_flow_node *node, void *data)
{
    struct lcd_data *mdata = data;
    struct command *cmd = NULL;
    uint16_t i;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    if (mdata->i2c_pending)
        sol_i2c_pending_cancel(mdata->i2c, mdata->i2c_pending);
    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);

    /* Traverse backwards so deletion doesn't impact the indices. */
    SOL_VECTOR_FOREACH_REVERSE_IDX (&mdata->cmd_queue, cmd, i) {
        command_free(cmd);
        sol_vector_del(&mdata->cmd_queue, i);
    }
}

/* serves both set_row() and set_col() */
static int
cursor_cmd_queue(struct lcd_data *mdata, uint8_t value, bool is_col)
{
    uint8_t v = is_col ? value | ROW_ADDR[mdata->row]
        : mdata->col | ROW_ADDR[value];

    return command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND, v);
}

/* LCD API */
static int
set_row(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    struct sol_irange in_value;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < ROW_MIN || in_value.val > ROW_MAX) {
        SOL_WRN("Row range for this lcd display is %d-%d", ROW_MIN, ROW_MAX);
        return -EINVAL;
    }

    r = cursor_cmd_queue(mdata, in_value.val, false);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

/* LCD API */
static int
set_col(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    struct sol_irange in_value;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val < COL_MIN || in_value.val > COL_EXTRA) {
        SOL_WRN("Column range for this lcd display is %d-%d",
            COL_MIN, COL_EXTRA);
        return -EINVAL;
    }

    r = cursor_cmd_queue(mdata, in_value.val, true);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

/* serves cursor blink/underline and display on/off cmds */
static int
char_display_cmd_queue(struct lcd_data *mdata)
{
    return command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_control);
}

/* LCD API */
static int
set_display_on(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_control |= LCD_DISPLAY_ON;
    else
        mdata->display_control &= ~LCD_DISPLAY_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

/* LCD API */
static int
set_underline_cursor(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_control |= LCD_CURSOR_ON;
    else
        mdata->display_control &= ~LCD_CURSOR_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

/* LCD API */
static int
set_blinking_cursor(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_control |= LCD_BLINK_ON;
    else
        mdata->display_control &= ~LCD_BLINK_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

/* serves both set_ltr() and set_autoscroll() */
static int
char_entry_cmd_queue(struct lcd_data *mdata)
{
    return command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        mdata->display_mode);
}


/* LCD API */
static int
set_ltr(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_mode |= LCD_MODE_SET_LTR;
    else
        mdata->display_mode &= ~LCD_MODE_SET_LTR;

    r = char_entry_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}


/* LCD API */
static int
set_autoscroll(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value)
        mdata->display_mode |= LCD_MODE_SET_AUTO_SCROLL;
    else
        mdata->display_mode &= ~LCD_MODE_SET_AUTO_SCROLL;

    r = char_entry_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

static int
char_cmd_queue(struct lcd_data *mdata, uint8_t value)
{
    return command_queue(mdata, DISPLAY_ADDR, SEND_DATA, value);
}

/* LCD API */
static int
put_char(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    unsigned char in_value;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = char_cmd_queue(mdata, in_value);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

static int
clear_cmd_queue(struct lcd_data *mdata)
{
    return command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND, LCD_CLEAR);
}

#undef LCD_CLEAR

/* LCD API */
static int
display_clear(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    int r;

    r = clear_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

/* LCD API */
/* insert a sequence of chars where the cursor is at */
static int
put_string(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    int r;
    const char *in_value;
    char *string;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    string = strdup(in_value);
    SOL_NULL_CHECK(string, -ENOMEM);

    SOL_EXP_CHECK_GOTO(!command_string_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        LCD_STRING_WRITE,  string), fail);
    command_queue_start(mdata);

    return 0;

fail:
    free(string);
    return r;
}

/* LCD API */
/* clear screen and write sequence of chars from (0, 0) position */
static int
set_string(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    int r;
    const char *in_value;
    char *string;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    string = strdup(in_value);
    SOL_NULL_CHECK(string, -ENOMEM);

    r = clear_cmd_queue(mdata);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    SOL_EXP_CHECK_GOTO(!command_string_queue(mdata, DISPLAY_ADDR, SEND_COMMAND,
        LCD_STRING_WRITE,  string), fail);
    command_queue_start(mdata);

    return 0;

fail:
    free(string);
    return r;
}

static int
color_cmd_queue(struct lcd_data *mdata,
    uint32_t red,
    uint32_t green,
    uint32_t blue)
{
    uint32_t colors[] = { red, green, blue };
    uint32_t *color = colors;
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(colors); i++) {
        if (!command_queue(mdata, RGB_ADDR, COLOR_ADDR[i], *color++))
            return -ENOMEM;
    }

    return 0;
}

/* LCD API */
static int
set_color(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct lcd_data *mdata = data;
    struct sol_rgb in_value;
    int r;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (sol_rgb_set_max(&in_value, 255) < 0) {
        SOL_WRN("Invalid color");
        return -EINVAL;
    }

    r = color_cmd_queue(mdata, in_value.red, in_value.green, in_value.blue);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

static int
display_scroll_cmd_queue(struct lcd_data *mdata, uint8_t value)
{
    return command_queue(mdata, DISPLAY_ADDR, SEND_COMMAND, value);
}

/* LCD API */
static int
scroll_display(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    uint8_t value = LCD_CURSOR_SHIFT | LCD_DISPLAY_MOVE;
    struct lcd_data *mdata = data;
    int r;

    value |= (port == SOL_FLOW_NODE_TYPE_GROVE_LCD_CHAR__IN__SCROLL_RIGHT ?
        LCD_MOVE_RIGHT : LCD_MOVE_LEFT);

    r = display_scroll_cmd_queue(mdata, value);
    SOL_INT_CHECK(r, < 0, r);
    command_queue_start(mdata);

    return 0;
}

static int
lcd_char_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct lcd_data *mdata = data;
    const struct sol_flow_node_type_grove_lcd_char_options *opts =
        (const struct sol_flow_node_type_grove_lcd_char_options *)options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (opts, SOL_FLOW_NODE_TYPE_GROVE_LCD_CHAR_OPTIONS_API_VERSION, -EINVAL);

    initial = sol_util_timespec_get_current();
    prev = initial;
    r = lcd_open(node, data, options);
    SOL_INT_CHECK(r, < 0, r);

    r = cursor_cmd_queue(mdata, opts->init_col.val, false);
    SOL_INT_CHECK(r, < 0, r);

    r = cursor_cmd_queue(mdata, opts->init_row.val, false);
    SOL_INT_CHECK(r, < 0, r);

    if (opts->ltr)
        mdata->display_mode |= LCD_MODE_SET_LTR;
    else
        mdata->display_mode &= ~LCD_MODE_SET_LTR;

    if (opts->auto_scroll)
        mdata->display_mode |= LCD_MODE_SET_AUTO_SCROLL;
    else
        mdata->display_mode &= ~LCD_MODE_SET_AUTO_SCROLL;

    r = char_entry_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    if (opts->blink_cursor)
        mdata->display_control |= LCD_BLINK_ON;
    else
        mdata->display_control &= ~LCD_BLINK_ON;

    if (opts->underline_cursor)
        mdata->display_control |= LCD_CURSOR_ON;
    else
        mdata->display_control &= ~LCD_CURSOR_ON;

    r = char_display_cmd_queue(mdata);
    SOL_INT_CHECK(r, < 0, r);

    r = color_cmd_queue(mdata,
        opts->color.red, opts->color.green, opts->color.blue);
    return r;
}
#undef LCD_DISPLAY_CONTROL
#undef LCD_ENTRY_MODE_SET
#undef LCD_BLINK_ON
#undef LCD_CURSOR_ON
#undef LCD_DISPLAY_ON
#undef LCD_MODE_SET_LTR
#undef LCD_MODE_SET_AUTO_SCROLL

#include "grove-gen.c"
