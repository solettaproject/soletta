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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "sol-flow/calamari.h"
#include "sol-flow/gpio.h"

#include "sol-flow.h"
#include "sol-flow-static.h"
#include "sol-mainloop.h"
#include "sol-pwm.h"
#include "sol-spi.h"
#include "sol-util-internal.h"
#include "sol-flow-internal.h"

///////// SEGMENTS CTL ///////////

// The order expected by the display for each bit is 'degabXcf', where X
// is the DP, as described in
// http://en.wikipedia.org/wiki/Seven-segment_display_character_representations
static unsigned char font[] = {
    0xdb,
    0x0a,
    0xf8,
    0xba,
    0x2b,
    0xb3,
    0xf3,
    0x1a,
    0xfb,
    0xbb,
    0x7b,
    0xe3,
    0xd1,
    0xea,
    0xf1,
    0x71
};

struct segments_ctl_data {
    bool needs_clear;
};

static void
_clear(struct sol_flow_node *node)
{
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, true);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, false);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, true);
}


static void
_tick(struct sol_flow_node *node)
{
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLOCK, true);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLOCK, false);
}

static void
_latch(struct sol_flow_node *node)
{
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, false);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, true);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, false);
}

static void
_write_byte(struct sol_flow_node *node, unsigned char byte)
{
    int i;
    struct segments_ctl_data *mdata = sol_flow_node_get_private_data(node);

    if (mdata->needs_clear) {
        _clear(node);
        mdata->needs_clear = false;
    }

    // Unless we set active_low on the data gpio, it expects 1 for the led
    // to be off, and 0 for on, so we invert the byte here.
    byte = ~byte;
    for (i = 0; i < 8; i++) {
        int val = (byte >> i) & 1;
        sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__DATA, val);
        _tick(node);
    }
    _latch(node);
}

// Convert byte from 'abcdefgX' to 'degabXcf'
static unsigned char
_convert_order(unsigned char byte)
{
    unsigned char conv_byte = 0;

    conv_byte |= (byte & (1 << 7)) >> 3;
    conv_byte |= (byte & (1 << 6)) >> 3;
    conv_byte |= (byte & (1 << 5)) >> 4;
    conv_byte |= (byte & (1 << 4)) << 3;
    conv_byte |= (byte & (1 << 3)) << 3;
    conv_byte |= (byte & (1 << 2)) >> 2;
    conv_byte |= (byte & (1 << 1)) << 4;
    conv_byte |= (byte & 1) << 2;

    return conv_byte;
}

static int
segments_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    unsigned char byte;
    int r;
    struct segments_ctl_data *mdata = sol_flow_node_get_private_data(node);

    if (mdata->needs_clear) {
        _clear(node);
        mdata->needs_clear = false;
    }

    r = sol_flow_packet_get_byte(packet, &byte);
    SOL_INT_CHECK(r, < 0, r);
    _write_byte(node, _convert_order(byte));

    return 0;
}

static int
value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t value;
    int r;
    struct segments_ctl_data *mdata = data;

    if (mdata->needs_clear) {
        _clear(node);
        mdata->needs_clear = false;
    }

#define RANGE_MIN (0)
#define RANGE_MAX (15)

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if ((value < RANGE_MIN) || (value > RANGE_MAX)) {
        sol_flow_send_error_packet(node, ERANGE,
            "Range invalid, it should be between %d and %d but was %" PRId32,
            RANGE_MIN, RANGE_MAX, value);
        return 0;
    }
    _write_byte(node, font[value]);

    return 0;

#undef RANGE_MAX
#undef RANGE_MIN
}

static int
segments_ctl_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct segments_ctl_data *mdata = data;

    mdata->needs_clear = true;
    return 0;
}

static void
segments_ctl_close(struct sol_flow_node *node, void *data)
{
}

///////// CALAMARI 7SEG ///////////

#define SEG_CTL 0
#define SEG_CLEAR 1
#define SEG_LATCH 2
#define SEG_CLOCK 3
#define SEG_DATA 4

static int
calamari_7seg_child_opts_set(const struct sol_flow_node_type *type,
    uint16_t child_index,
    const struct sol_flow_node_options *opts,
    struct sol_flow_node_options *child_opts)
{
    const struct sol_flow_node_type_calamari_7seg_options *calamari_7seg_opts =
        (const struct sol_flow_node_type_calamari_7seg_options *)opts;

    struct sol_flow_node_type_gpio_writer_options *gpio_opts =
        (struct sol_flow_node_type_gpio_writer_options *)child_opts;

    const char *pins[] = {
        [SEG_CLEAR] = calamari_7seg_opts->clear_pin,
        [SEG_LATCH] = calamari_7seg_opts->latch_pin,
        [SEG_CLOCK] = calamari_7seg_opts->clock_pin,
        [SEG_DATA] = calamari_7seg_opts->data_pin
    };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
        SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    if (child_index == SEG_CTL || child_index > SEG_DATA)
        return 0;

    gpio_opts->raw = true;
    gpio_opts->pin = pins[child_index];

    return 0;
}

static void
calamari_7seg_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;
    const struct sol_flow_node_type **gpio_writer, **ctl;

    static struct sol_flow_static_node_spec nodes[] = {
        [SEG_CTL] = { NULL, "segments-ctl", NULL },
        [SEG_CLEAR] = { NULL, "gpio-clear", NULL },
        [SEG_LATCH] = { NULL, "gpio-latch", NULL },
        [SEG_CLOCK] = { NULL, "gpio-clock", NULL },
        [SEG_DATA] = { NULL, "gpio-data", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { SEG_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, SEG_CLEAR, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        { SEG_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, SEG_LATCH, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        { SEG_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLOCK, SEG_CLOCK, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        { SEG_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__DATA, SEG_DATA, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_in[] = {
        { SEG_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__IN__SEGMENTS },
        { SEG_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__IN__VALUE },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .child_opts_set = calamari_7seg_child_opts_set,
    };

    if (sol_flow_get_node_type("gpio", SOL_FLOW_NODE_TYPE_GPIO_WRITER, &gpio_writer) < 0) {
        *current = NULL;
        return;
    }
    if ((*gpio_writer)->init_type)
        (*gpio_writer)->init_type();

    ctl = &SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL;
    if ((*ctl)->init_type)
        (*ctl)->init_type();

    nodes[SEG_CTL].type = *ctl;
    nodes[SEG_CLEAR].type = nodes[SEG_LATCH].type = nodes[SEG_CLOCK].type = nodes[SEG_DATA].type = *gpio_writer;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

#undef SEG_CTL
#undef SEG_CLEAR
#undef SEG_LATCH
#undef SEG_CLOCK
#undef SEG_DATA

static void
segments_init_type(void)
{
    calamari_7seg_new_type(&SOL_FLOW_NODE_TYPE_CALAMARI_7SEG);
}

///////// CALAMARI LED ///////////

struct calamari_led_data {
    struct sol_flow_node *node;
    struct sol_pwm *pwm;

    int period;
    struct sol_irange val;
};

static int
calamari_led_process_intensity(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct calamari_led_data *mdata = data;
    double dc;
    int32_t value;
    int err;

    err = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(err, < 0, err);

    if (value < mdata->val.min)
        value = mdata->val.min;
    else if (value > mdata->val.max)
        value = mdata->val.max;

    dc = (double)((double)(value - mdata->val.min) / (double)mdata->val.max);
    sol_pwm_set_duty_cycle(mdata->pwm, mdata->period * dc);

    return 0;
}

static int
calamari_led_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct calamari_led_data *mdata = data;
    const struct sol_flow_node_type_calamari_led_options *opts =
        (const struct sol_flow_node_type_calamari_led_options *)options;
    struct sol_pwm_config pwm_config = { 0 };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CALAMARI_LED_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->period = opts->period;
    mdata->val.min = opts->range.min;
    mdata->val.max = opts->range.max;
    mdata->val.step = opts->range.step;
    mdata->node = node;

    SOL_SET_API_VERSION(pwm_config.api_version = SOL_PWM_CONFIG_API_VERSION; )
    pwm_config.period_ns = mdata->period;
    pwm_config.duty_cycle_ns = 0;
    pwm_config.enabled = true;

    mdata->pwm = sol_pwm_open(opts->address - 1, 0, &pwm_config);

    return 0;
}

static void
calamari_led_close(struct sol_flow_node *node, void *data)
{
    struct calamari_led_data *mdata = data;

    sol_pwm_close(mdata->pwm);
}

///////// CALAMARI LEVER ///////////

struct calamari_lever_data {
    struct sol_flow_node *node;
    struct sol_spi *spi;
    struct sol_timeout *timer;
    uint32_t poll_interval;

    struct sol_irange val;

    int last_value;
    bool forced;
};

#define RANGE_MIN 0
#define RANGE_MAX 1023

static int
calamari_lever_convert_range(const struct calamari_lever_data *mdata, int value)
{
    return (value - RANGE_MIN) * (mdata->val.max - mdata->val.min)
           / (RANGE_MAX - RANGE_MIN) + mdata->val.min;
}

static bool calamari_lever_spi_poll(void *data);

static void
spi_transfer_cb(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status)
{
    struct calamari_lever_data *mdata = cb_data;
    int val;

    if (status < 1) {
        SOL_WRN("Error reading lever during poll. Polling disabled.");
        return;
    }

    /* MCP300x - 10 bit precision */
    val = (rx[1] << 8 | rx[2]) & 0x3ff;
    val = calamari_lever_convert_range(mdata, val);

    if (val != mdata->last_value || mdata->forced) {
        mdata->last_value = val;
        mdata->forced = false;

        mdata->val.val = val;
        sol_flow_send_irange_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_CALAMARI_LEVER__OUT__OUT,
            &mdata->val);
    }

    mdata->timer = sol_timeout_add(mdata->poll_interval,
        calamari_lever_spi_poll, mdata);
}

static bool
calamari_lever_spi_poll(void *data)
{
    struct calamari_lever_data *mdata = data;
    /* MCP300X message - Start, Single ended - pin 0, null */
    static const uint8_t tx[] = { 0x01, 0x80, 0x00 };
    /* rx must be the same size as tx */
    static uint8_t rx[sol_util_array_size(tx)] = { 0x00, };

    SOL_NULL_CHECK(mdata, false);
    SOL_NULL_CHECK(mdata->spi, false);

    if (!sol_spi_transfer(mdata->spi, tx, rx, sol_util_array_size(tx), spi_transfer_cb,
        mdata)) {
        SOL_WRN("Error reading lever during poll.");
    }

    mdata->timer = NULL;
    return false;
}

static int
calamari_lever_process_poll(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct calamari_lever_data *mdata = data;

    SOL_NULL_CHECK(mdata, -EINVAL);

    mdata->forced = true;

    return (int)calamari_lever_spi_poll(data);
}

static int
calamari_lever_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct calamari_lever_data *mdata = data;
    const struct sol_flow_node_type_calamari_lever_options *opts =
        (const struct sol_flow_node_type_calamari_lever_options *)options;
    struct sol_spi_config spi_config;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CALAMARI_LEVER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    mdata->last_value = 0;
    mdata->forced = true;
    mdata->val.min = opts->range.min;
    mdata->val.max = opts->range.max;
    mdata->val.step = opts->range.step;
    mdata->poll_interval = opts->poll_interval;
    SOL_SET_API_VERSION(spi_config.api_version = SOL_SPI_CONFIG_API_VERSION; )
    spi_config.chip_select = opts->chip_select;
    spi_config.mode = SOL_SPI_MODE_0;
    spi_config.frequency = 100 * 1000; //100KHz
    spi_config.bits_per_word = SOL_SPI_DATA_BITS_DEFAULT;
    mdata->spi = sol_spi_open(opts->bus, &spi_config);

    if (opts->poll_interval != 0)
        mdata->timer = sol_timeout_add(mdata->poll_interval,
            calamari_lever_spi_poll, mdata);

    return 0;
}

static void
calamari_lever_close(struct sol_flow_node *node, void *data)
{
    struct calamari_lever_data *mdata = data;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    if (mdata->spi)
        sol_spi_close(mdata->spi);
}


///////// CALAMARI RGB LED ///////////

static int
calamari_rgb_led_process_red(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool val;

    int r = sol_flow_packet_get_bool(packet, &val);

    SOL_INT_CHECK(r, < 0, r);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__RED, val);

    return 0;
}

static int
calamari_rgb_led_process_green(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool val;

    int r = sol_flow_packet_get_bool(packet, &val);

    SOL_INT_CHECK(r, < 0, r);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__GREEN, val);

    return 0;
}

static int
calamari_rgb_led_process_blue(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool val;

    int r = sol_flow_packet_get_bool(packet, &val);

    SOL_INT_CHECK(r, < 0, r);
    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__BLUE, val);

    return 0;
}

#define RGB_LED_CTL 0
#define RGB_LED_RED 1
#define RGB_LED_GREEN 2
#define RGB_LED_BLUE 3

static int
calamari_rgb_child_opts_set(const struct sol_flow_node_type *type,
    uint16_t child_index,
    const struct sol_flow_node_options *opts,
    struct sol_flow_node_options *child_opts)
{
    const struct sol_flow_node_type_calamari_rgb_led_options *calamari_rgb_opts =
        (const struct sol_flow_node_type_calamari_rgb_led_options *)opts;

    struct sol_flow_node_type_gpio_writer_options *gpio_opts =
        (struct sol_flow_node_type_gpio_writer_options *)child_opts;

    const char *pins[] = {
        [RGB_LED_RED] = calamari_rgb_opts->red_pin,
        [RGB_LED_GREEN] = calamari_rgb_opts->green_pin,
        [RGB_LED_BLUE] = calamari_rgb_opts->blue_pin,
    };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(child_opts,
        SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    // There is nothing to do for node 0, which is rgb-ctl
    if (child_index == RGB_LED_CTL || child_index > RGB_LED_BLUE)
        return 0;

    gpio_opts->raw = true;
    gpio_opts->pin = pins[child_index];

    return 0;
}

static void
calamari_rgb_led_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;
    const struct sol_flow_node_type **gpio_writer, **ctl;

    static struct sol_flow_static_node_spec nodes[] = {
        [RGB_LED_CTL] = { NULL, "rgb-ctl", NULL },
        [RGB_LED_RED] = { NULL, "gpio-red", NULL },
        [RGB_LED_GREEN] = { NULL, "gpio-green", NULL },
        [RGB_LED_BLUE] = { NULL, "gpio-blue", NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { RGB_LED_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__RED, RGB_LED_RED, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        { RGB_LED_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__GREEN, RGB_LED_GREEN, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        { RGB_LED_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__BLUE, RGB_LED_BLUE, SOL_FLOW_NODE_TYPE_GPIO_WRITER__IN__IN },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_in[] = {
        { RGB_LED_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__IN__RED },
        { RGB_LED_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__IN__GREEN },
        { RGB_LED_CTL, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__IN__BLUE },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .child_opts_set = calamari_rgb_child_opts_set,
    };

    if (sol_flow_get_node_type("gpio", SOL_FLOW_NODE_TYPE_GPIO_WRITER, &gpio_writer) < 0) {
        *current = NULL;
        return;
    }
    if ((*gpio_writer)->init_type)
        (*gpio_writer)->init_type();

    ctl = &SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL;
    if ((*ctl)->init_type)
        (*ctl)->init_type();

    nodes[RGB_LED_CTL].type = *ctl;
    nodes[RGB_LED_RED].type = nodes[RGB_LED_GREEN].type = nodes[RGB_LED_BLUE].type = *gpio_writer;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->options_size = (*current)->options_size;
    type->default_options = (*current)->default_options;
    *current = type;
}

#undef RGB_LED_CTL
#undef RGB_LED_RED
#undef RGB_LED_GREEN
#undef RGB_LED_BLUE

static void
rgb_led_init_type(void)
{
    calamari_rgb_led_new_type(&SOL_FLOW_NODE_TYPE_CALAMARI_RGB_LED);
}

#include "calamari-gen.c"
