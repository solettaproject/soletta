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

#include "sol-flow/gpio.h"
#include "sol-flow-internal.h"

#include "sol-flow.h"
#include "sol-gpio.h"
#include "sol-util.h"

#include <stdio.h>
#include <stdlib.h>

struct gpio_data {
    struct sol_gpio *gpio;
};

static void
gpio_close(struct sol_flow_node *node, void *data)
{
    struct gpio_data *mdata = data;

    sol_gpio_close(mdata->gpio);
}

/* GPIO READER ********************************************************************/
static void
gpio_reader_event(void *data, struct sol_gpio *gpio)
{
    int value;
    struct sol_flow_node *node = data;
    struct gpio_data *mdata = sol_flow_node_get_private_data(node);

    value = sol_gpio_read(mdata->gpio);
    SOL_INT_CHECK(value, < 0);

    sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_GPIO_READER__OUT__OUT, value);
}

static int
gpio_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int pin;
    struct gpio_data *mdata = data;
    const struct sol_flow_node_type_gpio_reader_options *opts =
        (const struct sol_flow_node_type_gpio_reader_options *)options;
    struct sol_gpio_config gpio_conf = { 0 };
    static const enum sol_gpio_edge mode_lut[] = {
        [0 + 2 * 0] = SOL_GPIO_EDGE_NONE,
        [1 + 2 * 0] = SOL_GPIO_EDGE_RISING,
        [0 + 2 * 1] = SOL_GPIO_EDGE_FALLING,
        [1 + 2 * 1] = SOL_GPIO_EDGE_BOTH
    };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_GPIO_READER_OPTIONS_API_VERSION, -EINVAL);

    gpio_conf.api_version = SOL_GPIO_CONFIG_API_VERSION;
    gpio_conf.dir = SOL_GPIO_DIR_IN;
    gpio_conf.active_low = opts->active_low;
    gpio_conf.in.trigger_mode = mode_lut[opts->edge_rising + 2 * opts->edge_falling];
    gpio_conf.in.cb = gpio_reader_event;
    gpio_conf.in.user_data = node;
    gpio_conf.in.poll_timeout = opts->poll_timeout.val;

    if (gpio_conf.in.trigger_mode == SOL_GPIO_EDGE_NONE) {
        SOL_WRN("gpio reader #%s: either edge_rising or edge_falling need to be"
            " set for the node to generate events.", opts->pin);
        return -EINVAL;
    }

    if (streq(opts->pull, "up"))
        gpio_conf.drive_mode = SOL_GPIO_DRIVE_PULL_UP;
    else if (streq(opts->pull, "down"))
        gpio_conf.drive_mode = SOL_GPIO_DRIVE_PULL_DOWN;

    mdata->gpio = NULL;
    if (!opts->pin || *opts->pin == '\0') {
        SOL_WRN("gpio: Option 'pin' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }

    if (opts->raw) {
        if (!sscanf(opts->pin, "%d", &pin)) {
            SOL_WRN("gpio (%s): 'raw' option was set, but 'pin' value=%s couldn't be parsed as "
                "integer.", opts->pin, opts->pin);
        } else {
            mdata->gpio = sol_gpio_open(pin, &gpio_conf);
        }
    } else {
        mdata->gpio = sol_gpio_open_by_label(opts->pin, &gpio_conf);
    }

    if (!mdata->gpio) {
        SOL_WRN("Could not open gpio #%s", opts->pin);
        return -EIO;
    }

    return 0;
}

/* GPIO WRITER *******************************************************************/

static int
gpio_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool value;
    struct gpio_data *mdata = data;
    int r = sol_flow_packet_get_boolean(packet, &value);

    SOL_INT_CHECK(r, < 0, r);
    if (!sol_gpio_write(mdata->gpio, value)) {
        return -EIO;
    }
    return 0;
}

static int
gpio_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int pin;
    struct gpio_data *mdata = data;
    const struct sol_flow_node_type_gpio_writer_options *opts =
        (const struct sol_flow_node_type_gpio_writer_options *)options;
    struct sol_gpio_config gpio_conf = { 0 };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_API_VERSION, -EINVAL);

    gpio_conf.api_version = SOL_GPIO_CONFIG_API_VERSION;
    gpio_conf.dir = SOL_GPIO_DIR_OUT;
    gpio_conf.active_low = opts->active_low;

    mdata->gpio = NULL;
    if (!opts->pin || *opts->pin == '\0') {
        SOL_WRN("gpio: Option 'pin' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }

    if (opts->raw) {
        if (!sscanf(opts->pin, "%d", &pin)) {
            SOL_WRN("gpio (%s): 'raw' option was set, but 'pin' value=%s couldn't be parsed as "
                "integer.", opts->pin, opts->pin);
        } else {
            mdata->gpio = sol_gpio_open(pin, &gpio_conf);
        }
    } else {
        mdata->gpio = sol_gpio_open_by_label(opts->pin, &gpio_conf);
    }

    if (!mdata->gpio) {
        SOL_WRN("Could not open gpio #%s", opts->pin);
        return -EIO;
    }

    return 0;
}

#include "gpio-gen.c"
