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

#include "sol-flow/hc-sr04.h"
#include "sol-flow-internal.h"

#include "sol-gpio.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"

#include <time.h>

/* It should be from 10 to 300 microseconds */
#define TRIG_PULSE_MS (1)

struct hc_sr04_data {
    struct sol_gpio *trig_gpio;
    struct sol_gpio *echo_gpio;
    struct sol_timeout *timer;
    struct sol_flow_node *node;
    struct timespec t1;
    int32_t offset;
    bool known_low : 1;
    bool low : 1;
    bool busy : 1;
};

static void
echo_event_cb(void *data, struct sol_gpio *gpio, bool value)
{
    struct hc_sr04_data *mdata = data;
    struct timespec t2, delta;
    int usec, centimeters;

    /* started the pulse */
    if (value != mdata->low) {
        mdata->t1 = sol_util_timespec_get_current();
        return;
    }

    /* pulse ended */
    t2 = sol_util_timespec_get_current();
    sol_util_timespec_sub(&t2, &mdata->t1, &delta);
    usec = sol_util_usec_from_timespec(&delta);
    /* distance = time * velocity (340 m/s) / 2 */
    centimeters = usec / 58;

    mdata->busy = false;

    sol_flow_send_irange_value_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_HC_SR04_DISTANCE__OUT__CENTIMETERS, centimeters);
}

static int
hc_sr04_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct hc_sr04_data *mdata = data;
    const struct sol_flow_node_type_hc_sr04_distance_options *opts;
    struct sol_gpio_config trig_gpio_conf = { 0 };
    struct sol_gpio_config echo_gpio_conf = { 0 };
    uint32_t pin;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_HC_SR04_DISTANCE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_hc_sr04_distance_options *)options;

    SOL_SET_API_VERSION(trig_gpio_conf.api_version =
        SOL_GPIO_CONFIG_API_VERSION; )
    trig_gpio_conf.dir = SOL_GPIO_DIR_OUT;

    SOL_SET_API_VERSION(echo_gpio_conf.api_version =
        SOL_GPIO_CONFIG_API_VERSION; )
    echo_gpio_conf.dir = SOL_GPIO_DIR_IN;
    echo_gpio_conf.in.trigger_mode = SOL_GPIO_EDGE_BOTH;
    echo_gpio_conf.in.cb = echo_event_cb;
    echo_gpio_conf.in.user_data = mdata;
    echo_gpio_conf.in.poll_timeout = opts->echo_poll_timeout;

    if (!opts->trigger || *opts->trigger == '\0') {
        SOL_WRN("Option 'trigger' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }
    if (!opts->echo || *opts->echo == '\0') {
        SOL_WRN("Option 'echo' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }

    if (opts->raw) {
        if (!sscanf(opts->trigger, "%" SCNu32, &pin)) {
            SOL_WRN("'raw' option was set, but 'pin' "
                "value=%s couldn't be parsed as integer.", opts->trigger);
        } else {
            mdata->trig_gpio = sol_gpio_open(pin, &trig_gpio_conf);
        }
    } else {
        mdata->trig_gpio = sol_gpio_open_by_label(opts->trigger,
            &trig_gpio_conf);
    }
    if (!mdata->trig_gpio) {
        SOL_WRN("Could not open trigger gpio #%s", opts->trigger);
        return -EIO;
    }

    if (opts->raw) {
        if (!sscanf(opts->echo, "%" SCNu32, &pin)) {
            SOL_WRN("'raw' option was set, but 'pin' "
                "value=%s couldn't be parsed as integer.", opts->echo);
        } else {
            mdata->echo_gpio = sol_gpio_open(pin, &echo_gpio_conf);
        }
    } else {
        mdata->echo_gpio = sol_gpio_open_by_label(opts->echo,
            &echo_gpio_conf);
    }
    if (!mdata->echo_gpio) {
        SOL_WRN("Could not open echo gpio #%s", opts->echo);
        goto err_echo;
    }

    if (!sol_gpio_write(mdata->trig_gpio, false)) {
        SOL_WRN("Failed to write to trigger gpio");
        goto err_write;
    }

    mdata->node = node;
    mdata->offset = opts->offset;

    return 0;

err_write:
    sol_gpio_close(mdata->echo_gpio);
err_echo:
    sol_gpio_close(mdata->trig_gpio);
    return -EIO;
}

static void
hc_sr04_close(struct sol_flow_node *node, void *data)
{
    struct hc_sr04_data *mdata = data;

    sol_gpio_close(mdata->trig_gpio);
    sol_gpio_close(mdata->echo_gpio);
    if (mdata->timer)
        sol_timeout_del(mdata->timer);
}

static bool
timer_cb(void *data)
{
    struct hc_sr04_data *mdata = data;

    mdata->timer = NULL;

    if (!sol_gpio_write(mdata->trig_gpio, false)) {
        SOL_WRN("Failed to write to trigger gpio");
        mdata->busy = false;
    }

    /* Sometimes when reading the interruption on echo for level high
     * it will be low already (on Linux). So it would miss the timestamp
     * of going level high. But since it happens a few microseconds
     * after going level low on trigger, we may use this information.
     * Using opts->offset may help to fine tune that.
     */
    mdata->t1 = sol_util_timespec_get_current();
    mdata->t1.tv_nsec += mdata->offset;

    return false;
}

static int
trigger_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct hc_sr04_data *mdata = data;

    if (mdata->busy) {
        SOL_INF("Ultrasonic sensor is busy");
        return 0;
    }

    if (!mdata->known_low) {
        int r = sol_gpio_read(mdata->echo_gpio);
        SOL_INT_CHECK(r, < 0, r);
        mdata->low = !!r;
        mdata->known_low = true;
    }

    /* send a pulse of 1 millisecond */
    if (!sol_gpio_write(mdata->trig_gpio, true)) {
        SOL_WRN("Failed to write to trigger gpio");
        return -EIO;
    }
    mdata->timer = sol_timeout_add(TRIG_PULSE_MS, timer_cb, mdata);
    SOL_NULL_CHECK(mdata->timer, -ENOMEM);

    mdata->busy = true;

    return 0;
}

#include "hc-sr04-gen.c"
