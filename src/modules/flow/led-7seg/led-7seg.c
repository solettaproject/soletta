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
static const unsigned char font[] = {
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
            SOL_WRN("Failed to write on gpio %d.", i);
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
    const int array_size = ARRAY_SIZE(font) - 1;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if ((in_value < 0) || (in_value > array_size)) {
        sol_flow_send_error_packet(node, ERANGE,
            "Range invalid, it should be between %d and %d but was %" PRId32,
            0, array_size, in_value);
        return 0;
    }

    r = write_byte(data, node, font[in_value]);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

#define OPEN_GPIO(_pin, _option) \
    do { \
        mdata->gpio[_pin] = sol_gpio_open(opts->pin_ ## _option, \
            &gpio_conf); \
        if (!mdata->gpio[_pin]) { \
            SOL_WRN("could not open gpio #%" PRId32, \
                opts->pin_ ## _option); \
            goto port_error; \
        } \
    } while (0)

static int
led_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct led_7seg_data *mdata = data;
    const struct sol_flow_node_type_led_7seg_led_options *opts =
        (const struct sol_flow_node_type_led_7seg_led_options *)options;
    struct sol_gpio_config gpio_conf = { 0 };
    int i;

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

port_error:
    for (i = 0; i < 8; i++) {
        if (mdata->gpio[i])
            sol_gpio_close(mdata->gpio[i]);
    }
    return -EIO;
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

/* Conversion table between chars and bytes in format 'abcdefgX' */
static const unsigned char conversion_table[] = {
    ['0'] = 0xfc,
    ['1'] = 0x60,
    ['2'] = 0xda,
    ['3'] = 0xf2,
    ['4'] = 0x66,
    ['5'] = 0xb6,
    ['6'] = 0xbe,
    ['7'] = 0xe0,
    ['8'] = 0xfe,
    ['9'] = 0xf6,
    ['A'] = 0xee,
    ['B'] = 0x3e,
    ['C'] = 0x9c,
    ['D'] = 0x7a,
    ['E'] = 0x9e,
    ['F'] = 0x8e,
    ['G'] = 0xbe,
    ['H'] = 0x6e,
    ['I'] = 0xc,
    ['J'] = 0x78,
    ['L'] = 0x1c,
    ['N'] = 0x2a,
    ['O'] = 0xfc,
    ['P'] = 0xce,
    ['R'] = 0xa,
    ['S'] = 0xb6,
    ['T'] = 0x1e,
    ['U'] = 0x7c,
    ['Y'] = 0x76,
    ['a'] = 0xee,
    ['b'] = 0x3e,
    ['c'] = 0x1a,
    ['d'] = 0x7a,
    ['e'] = 0x9e,
    ['f'] = 0x8e,
    ['g'] = 0xf6,
    ['h'] = 0x2e,
    ['i'] = 0x8,
    ['j'] = 0x78,
    ['l'] = 0x60,
    ['n'] = 0x2a,
    ['o'] = 0x3a,
    ['p'] = 0xce,
    ['r'] = 0xa,
    ['s'] = 0xb6,
    ['t'] = 0x1e,
    ['u'] = 0x38,
    ['y'] = 0x76
};

static int
convert_char(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *in_value;
    int c_index, r;
    unsigned char byte = 0;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    c_index = in_value[0];
    if (c_index != '\0' && c_index != ' ') {
        byte = conversion_table[c_index];
        if (!byte) {
            sol_flow_send_error_packet(node, EINVAL,
                "Char '%c' can't be represented with 7 segments.", c_index);
            return 0;
        }
    }

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_LED_7SEG_CHAR_TO_BYTE__OUT__OUT,
        byte);
}

#include "led-7seg-gen.c"
