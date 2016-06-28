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

#include "sol-flow/pwm.h"
#include "sol-flow-internal.h"

#include "sol-flow.h"
#include "sol-pwm.h"
#include "sol-util-internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

struct pwm_data {
    struct sol_pwm *pwm;
};

static int
pwm_process_enable(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool enabled;
    struct pwm_data *mdata = data;
    int r = sol_flow_packet_get_bool(packet, &enabled);

    SOL_INT_CHECK(r, < 0, r);

    if (sol_pwm_set_enabled(mdata->pwm, enabled) < 0)
        return -EIO;

    return 0;
}

static int
pwm_process_period(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t period;
    struct pwm_data *mdata = data;
    int r = sol_flow_packet_get_irange_value(packet, &period);

    SOL_INT_CHECK(r, < 0, r);

    if (sol_pwm_set_period(mdata->pwm, period) < 0)
        return -EIO;

    return 0;
}

static int
pwm_process_duty_cycle(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t dc;
    struct pwm_data *mdata = data;
    int r = sol_flow_packet_get_irange_value(packet, &dc);

    SOL_INT_CHECK(r, < 0, r);

    if (sol_pwm_set_duty_cycle(mdata->pwm, dc) < 0)
        return -EIO;

    return 0;
}

static uint32_t
map_irange_to_period(struct sol_irange val, int32_t period)
{
    int64_t result;

    if (val.max == val.min) {
        SOL_WRN("Max and min values for PWM duty cycle percentage are the same");
        return UINT32_MAX;
    }

    result = (val.val - val.min) * (int64_t)period / (val.max - val.min);
    SOL_INT_CHECK(result, < 0, 0);

    return (uint32_t)result;
}

static int
pwm_process_duty_cycle_percent(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange value;
    struct pwm_data *mdata = data;
    int32_t period;
    uint32_t duty_cycle;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    period = sol_pwm_get_period(mdata->pwm);
    SOL_INT_CHECK(period, < 0, period);

    duty_cycle = map_irange_to_period(value, period);
    SOL_INT_CHECK(duty_cycle, == UINT32_MAX, -EINVAL);

    if (sol_pwm_set_duty_cycle(mdata->pwm, duty_cycle) < 0)
        return -EIO;

    return 0;
}

static int
pwm_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int device, channel;
    const struct sol_flow_node_type_pwm_options *opts =
        (const struct sol_flow_node_type_pwm_options *)options;
    struct pwm_data *mdata = data;
    struct sol_pwm_config pwm_config = { 0 };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PWM_OPTIONS_API_VERSION, -EINVAL);

    // Use values from options. Period = 0 considered invalid.
    if (opts->period <= 0) {
        SOL_WRN("Invalid value for period - pwm (%s)", opts->pin);
        return -EINVAL;
    }

    if (opts->duty_cycle < 0) {
        SOL_WRN("Invalid value for duty_cycle - pwm (%s)", opts->pin);
        return -EINVAL;
    }

    SOL_SET_API_VERSION(pwm_config.api_version = SOL_PWM_CONFIG_API_VERSION; )
    pwm_config.period_ns = opts->period;
    pwm_config.duty_cycle_ns = opts->duty_cycle;
    if (opts->inversed_polarity)
        pwm_config.polarity = SOL_PWM_POLARITY_INVERSED;
    pwm_config.enabled = opts->enabled;

    mdata->pwm = NULL;
    if (!opts->pin || *opts->pin == '\0') {
        SOL_WRN("pwm: Option 'pin' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }

    if (opts->raw) {
        if (sscanf(opts->pin, "%d %d", &device, &channel) == 2) {
            mdata->pwm = sol_pwm_open(device, channel, &pwm_config);
        } else {
            SOL_WRN("pwm (%s): 'raw' option was set, but 'pin' value=%s couldn't be parsed as "
                "\"<device> <channel>\" pair.", opts->pin, opts->pin);
        }
    } else {
        mdata->pwm = sol_pwm_open_by_label(opts->pin, &pwm_config);
    }

    SOL_NULL_CHECK_MSG(mdata->pwm, -ENXIO, "Could not open pwm (%s)", opts->pin);

    return 0;
}

static void
pwm_close(struct sol_flow_node *node, void *data)
{
    struct pwm_data *mdata = data;

    sol_pwm_close(mdata->pwm);
}

#include "pwm-gen.c"
