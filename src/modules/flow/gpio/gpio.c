/*
 * This file is part of the Soletta (TM) Project
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

#include "sol-flow/gpio.h"
#include "sol-flow-internal.h"

#include "sol-flow.h"
#include "sol-gpio.h"
#include "sol-util-internal.h"

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
gpio_reader_event(void *data, struct sol_gpio *gpio, bool value)
{
    struct sol_flow_node *node = data;

    sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_GPIO_READER__OUT__OUT, value);
}

static int
gpio_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    uint32_t pin;
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

    SOL_SET_API_VERSION(gpio_conf.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    gpio_conf.dir = SOL_GPIO_DIR_IN;
    gpio_conf.active_low = opts->active_low;
    gpio_conf.in.trigger_mode = mode_lut[opts->edge_rising + 2 * opts->edge_falling];
    gpio_conf.in.cb = gpio_reader_event;
    gpio_conf.in.user_data = node;
    gpio_conf.in.poll_timeout = opts->poll_timeout;

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
        if (!sscanf(opts->pin, "%" SCNu32, &pin)) {
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
    int r = sol_flow_packet_get_bool(packet, &value);

    SOL_INT_CHECK(r, < 0, r);
    if (!sol_gpio_write(mdata->gpio, value)) {
        return -EIO;
    }
    return 0;
}

static int
gpio_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    uint32_t pin;
    struct gpio_data *mdata = data;
    const struct sol_flow_node_type_gpio_writer_options *opts =
        (const struct sol_flow_node_type_gpio_writer_options *)options;
    struct sol_gpio_config gpio_conf = { 0 };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_API_VERSION, -EINVAL);

    SOL_SET_API_VERSION(gpio_conf.api_version = SOL_GPIO_CONFIG_API_VERSION; )
    gpio_conf.dir = SOL_GPIO_DIR_OUT;
    gpio_conf.active_low = opts->active_low;

    mdata->gpio = NULL;
    if (!opts->pin || *opts->pin == '\0') {
        SOL_WRN("gpio: Option 'pin' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }

    if (opts->raw) {
        if (!sscanf(opts->pin, "%" SCNu32, &pin)) {
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
