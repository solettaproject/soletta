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

#include "sol-flow/led-7seg.h"
#include "sol-flow-internal.h"

#include <sol-gpio.h>
#include <sol-util.h>
#include <errno.h>

struct led_7seg_data {
    struct sol_gpio *gpio[8];
    bool common_cathode : 1;
};

/* Chars from 0-9 and A-F in format 'abcdefgX' */
static unsigned char font[] = {
    0xfc,
    0x60,
    0xda,
    0xf2,
    0x66,
    0xb6,
    0xbe,
    0xe0,
    0xfe,
    0xf6,
    0xee,
    0x3e,
    0x9c,
    0x7a,
    0x9e,
    0x8e
};

static int
write_byte(struct led_7seg_data *mdata, struct sol_flow_node *node, unsigned char byte)
{
    int i;

    if (!mdata->common_cathode)
        byte = ~byte;

    for (i = 0; i < 8; i++) {
        bool val = (byte >> i) & 1;
        if (!sol_gpio_write(mdata->gpio[i], val)) {
            SOL_WRN("Failed to write on gpio %" PRId32 ".", i);
            return -EIO;
        }
    }

    return 0;
}

static int
segments_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    unsigned char in_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = write_byte(data, node, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
value_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t in_value;
    int r;

#define RANGE_MIN (0)
#define RANGE_MAX (15)

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if ((in_value < RANGE_MIN) || (in_value > RANGE_MAX)) {
        sol_flow_send_error_packet(node, ERANGE,
            "Range invalid, it should be between %d and %d but was %d",
            RANGE_MIN, RANGE_MAX, in_value);
        return 0;
    }

    r = write_byte(data, node, font[in_value]);
    SOL_INT_CHECK(r, < 0, r);

    return 0;

#undef RANGE_MAX
#undef RANGE_MIN
}

#define OPEN_GPIO(_pin, _option) \
    do { \
        mdata->gpio[_pin] = sol_gpio_open(opts->pin_ ## _option.val, \
            &gpio_conf); \
        if (!mdata->gpio[_pin]) { \
            SOL_WRN("could not open gpio #%" PRId32, \
                opts->pin_ ## _option.val); \
            return -EIO; \
        } \
    } while (0)

static int
led_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct led_7seg_data *mdata = data;
    const struct sol_flow_node_type_led_7seg_led_options *opts =
        (const struct sol_flow_node_type_led_7seg_led_options *)options;
    struct sol_gpio_config gpio_conf = { 0 };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_LED_7SEG_LED_OPTIONS_API_VERSION, -EINVAL);

    gpio_conf.api_version = SOL_GPIO_CONFIG_API_VERSION;
    gpio_conf.dir = SOL_GPIO_DIR_OUT;

    OPEN_GPIO(0, dp);
    OPEN_GPIO(1, g);
    OPEN_GPIO(2, f);
    OPEN_GPIO(3, e);
    OPEN_GPIO(4, d);
    OPEN_GPIO(5, c);
    OPEN_GPIO(6, b);
    OPEN_GPIO(7, a);

    mdata->common_cathode = opts->common_cathode;

    return 0;
}

#undef OPEN_GPIO

static void
led_close(struct sol_flow_node *node, void *data)
{
    struct led_7seg_data *mdata = data;
    int i;

    for (i = 0; i < 8; i++)
        sol_gpio_close(mdata->gpio[i]);
}

#include "led-7seg-gen.c"
