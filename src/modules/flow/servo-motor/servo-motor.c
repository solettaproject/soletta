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
#include <sol-pwm.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "sol-flow/servo-motor.h"

struct servo_motor_data {
    struct sol_irange duty_cycle_range;
    struct sol_pwm *pwm;
    int32_t duty_cycle_diff;
    bool pwm_enabled : 1;
};

static int
servo_motor_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int device, channel;
    const struct sol_flow_node_type_servo_motor_controller_options *opts;
    struct servo_motor_data *mdata = data;
    struct sol_pwm_config pwm_config = { 0 };

    opts = (const struct sol_flow_node_type_servo_motor_controller_options *)options;

    mdata->duty_cycle_range.min = opts->duty_cycle_range.min;
    mdata->duty_cycle_range.max = opts->duty_cycle_range.max;
    mdata->duty_cycle_range.step = opts->duty_cycle_range.step;

    if (mdata->duty_cycle_range.min > mdata->duty_cycle_range.max) {
        SOL_WRN("Max pulse width shouldn't be less than min. Swapping values.");
        mdata->duty_cycle_range.max = opts->duty_cycle_range.min;
        mdata->duty_cycle_range.min = opts->duty_cycle_range.max;
    }

    mdata->duty_cycle_diff = mdata->duty_cycle_range.max -
        mdata->duty_cycle_range.min;

    SOL_SET_API_VERSION(pwm_config.api_version = SOL_PWM_CONFIG_API_VERSION; )
    pwm_config.period_ns = opts->period * 1000;
    pwm_config.duty_cycle_ns = 0;

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

    if (!mdata->pwm) {
        SOL_WRN("Could not open pwm (%s)", opts->pin);
        return -ENOMEM;
    }

    return 0;
}

static void
servo_motor_close(struct sol_flow_node *node, void *data)
{
    struct servo_motor_data *mdata = data;

    sol_pwm_close(mdata->pwm);
}

/* TODO send a packet to pwm node */
static int
set_pulse_width(struct servo_motor_data *mdata, int32_t pulse_width)
{
    SOL_DBG("Pulse width %" PRId32 " microseconds (%" PRId32 " -"
        "%" PRId32 ")", pulse_width,
        mdata->duty_cycle_range.min, mdata->duty_cycle_range.max);

    if (!mdata->pwm_enabled) {
        sol_pwm_set_enabled(mdata->pwm, true);
        mdata->pwm_enabled = true;
    } else if (pulse_width == mdata->duty_cycle_range.val)
        return 0;

    mdata->duty_cycle_range.val = pulse_width;
    if (!sol_pwm_set_duty_cycle(mdata->pwm,
        mdata->duty_cycle_range.val * 1000)) {
        SOL_WRN("Failed to write duty cycle %" PRId32 "ns.",
            mdata->duty_cycle_range.val * 1000);
        return -1;
    }

    return 0;
}

static int
duty_cycle_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct servo_motor_data *mdata = data;
    int r;
    int32_t in_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value < mdata->duty_cycle_range.min ||
        in_value > mdata->duty_cycle_range.max) {
        SOL_WRN("Invalid value %" PRId32 "."
            "It must be >= %" PRId32 " and =< %" PRId32 "", in_value,
            mdata->duty_cycle_range.min, mdata->duty_cycle_range.max);
        return -EINVAL;
    }

    return set_pulse_width(mdata, in_value);
}

static int
angle_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    double in_value;
    struct servo_motor_data *mdata = data;
    int r, pulse_width;

    r = sol_flow_packet_get_drange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (isless(in_value, 0) || isgreaterequal(in_value, 180)) {
        SOL_WRN("Invalid value %f. It must be >= 0 and < 180", in_value);
        return -EINVAL;
    }

    pulse_width = in_value * mdata->duty_cycle_diff / 180 +
        mdata->duty_cycle_range.min;

    return set_pulse_width(mdata, pulse_width);
}


#include "servo-motor-gen.c"
