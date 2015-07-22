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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "calamari-gen.h"
#include "gpio-gen.h"

#include "sol-flow.h"
#include "sol-flow-static.h"
#include "sol-mainloop.h"
#include "sol-pwm.h"
#include "sol-spi.h"
#include "sol-util.h"

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
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, true);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, false);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLEAR, true);
}


static void
_tick(struct sol_flow_node *node)
{
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLOCK, true);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__CLOCK, false);
}

static void
_latch(struct sol_flow_node *node)
{
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, false);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, true);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__LATCH, false);
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
        sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL__OUT__DATA, val);
        _tick(node);
    }
    _latch(node);
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
    _write_byte(node, byte);

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

    if (value < RANGE_MIN || value > RANGE_MAX)
        return -ERANGE;
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

    const int pins[] = {
        [SEG_CLEAR] = calamari_7seg_opts->clear_pin.val,
        [SEG_LATCH] = calamari_7seg_opts->latch_pin.val,
        [SEG_CLOCK] = calamari_7seg_opts->clock_pin.val,
        [SEG_DATA] = calamari_7seg_opts->data_pin.val
    };

    if (child_index == SEG_CTL || child_index > SEG_DATA)
        return 0;

    gpio_opts->pin.val = pins[child_index];

    return 0;
}

static void
calamari_7seg_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;

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
        .api_version = SOL_FLOW_STATIC_API_VERSION,
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .child_opts_set = calamari_7seg_child_opts_set,
    };

    nodes[SEG_CTL].type = SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL;
    nodes[SEG_CLEAR].type = nodes[SEG_LATCH].type = nodes[SEG_CLOCK].type = nodes[SEG_DATA].type = SOL_FLOW_NODE_TYPE_GPIO_WRITER;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->new_options = (*current)->new_options;
    type->free_options = (*current)->free_options;
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

    mdata->period = opts->period.val;
    mdata->val = opts->range;
    mdata->node = node;

    pwm_config.api_version = SOL_PWM_CONFIG_API_VERSION;
    pwm_config.period_ns = mdata->period;
    pwm_config.duty_cycle_ns = 0;
    pwm_config.enabled = true;

    mdata->pwm = sol_pwm_open(opts->address.val - 1, 0, &pwm_config);

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

static int
calamari_lever_spi_read(struct sol_spi *spi)
{
    unsigned int value;

    /* MCP300X message - Start, Single ended - pin 0, null */
    uint8_t tx[] = { 0x01, 0x80, 0x00 };
    /* rx must be the same size as tx */
    uint8_t rx[ARRAY_SIZE(tx)] = { 0x00, };

    if (!sol_spi_transfer(spi, tx, rx, ARRAY_SIZE(tx)))
        return -EIO;

    /* MCP300x - 10 bit precision */
    value = (rx[1] << 8 | rx[2]) & 0x3ff;

    return value;
}

static bool
calamari_lever_spi_poll(void *data)
{
    struct calamari_lever_data *mdata = data;
    int val;

    SOL_NULL_CHECK(mdata, false);
    SOL_NULL_CHECK(mdata->spi, false);

    val = calamari_lever_spi_read(mdata->spi);
    if (val < 0) {
        SOL_WRN("Error reading lever during poll. Polling disabled. %d", val);
        if (mdata->timer) {
            sol_timeout_del(mdata->timer);
            mdata->timer = NULL;
        }
        return false;
    }

    val = calamari_lever_convert_range(mdata, val);

    if (val != mdata->last_value || mdata->forced) {
        mdata->last_value = val;
        mdata->forced = false;

        mdata->val.val = val;
        sol_flow_send_irange_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_CALAMARI_LEVER__OUT__OUT,
            &mdata->val);
    }

    return true;
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

    SOL_NULL_CHECK(options, 0);

    mdata->node = node;
    mdata->last_value = 0;
    mdata->forced = true;
    mdata->val = opts->range;
    spi_config.api_version = SOL_SPI_CONFIG_API_VERSION;
    spi_config.chip_select = opts->chip_select.val;
    spi_config.mode = SOL_SPI_MODE_0;
    spi_config.speed = SOL_SPI_SPEED_100K;
    spi_config.bits_per_word = SOL_SPI_DATA_BITS_DEFAULT;
    mdata->spi = sol_spi_open(opts->bus.val, &spi_config);

    if (opts->poll_interval.val != 0)
        mdata->timer = sol_timeout_add(opts->poll_interval.val,
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

    int r = sol_flow_packet_get_boolean(packet, &val);

    SOL_INT_CHECK(r, < 0, r);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__RED, val);

    return 0;
}

static int
calamari_rgb_led_process_green(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool val;

    int r = sol_flow_packet_get_boolean(packet, &val);

    SOL_INT_CHECK(r, < 0, r);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__GREEN, val);

    return 0;
}

static int
calamari_rgb_led_process_blue(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool val;

    int r = sol_flow_packet_get_boolean(packet, &val);

    SOL_INT_CHECK(r, < 0, r);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL__OUT__BLUE, val);

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

    const int pins[] = {
        [RGB_LED_RED] = calamari_rgb_opts->red_pin.val,
        [RGB_LED_GREEN] = calamari_rgb_opts->green_pin.val,
        [RGB_LED_BLUE] = calamari_rgb_opts->blue_pin.val,
    };

    // There is nothing to do for node 0, which is rgb-ctl
    if (child_index == RGB_LED_CTL || child_index > RGB_LED_BLUE)
        return 0;

    gpio_opts->pin.val = pins[child_index];

    return 0;
}

static void
calamari_rgb_led_new_type(const struct sol_flow_node_type **current)
{
    struct sol_flow_node_type *type;

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
        .api_version = SOL_FLOW_STATIC_API_VERSION,
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .child_opts_set = calamari_rgb_child_opts_set,
    };

    nodes[RGB_LED_CTL].type = SOL_FLOW_NODE_TYPE_CALAMARI_RGB_CTL;
    nodes[RGB_LED_RED].type = nodes[RGB_LED_GREEN].type = nodes[RGB_LED_BLUE].type = SOL_FLOW_NODE_TYPE_GPIO_WRITER;

    type = sol_flow_static_new_type(&spec);
    SOL_NULL_CHECK(type);
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    type->description = (*current)->description;
#endif
    type->new_options = (*current)->new_options;
    type->free_options = (*current)->free_options;
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
