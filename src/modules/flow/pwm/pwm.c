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

#include "pwm-gen.h"
#include "sol-flow-internal.h"

#include "sol-flow.h"
#include "sol-pwm.h"
#include "sol-util.h"

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
    int r = sol_flow_packet_get_boolean(packet, &enabled);

    SOL_INT_CHECK(r, < 0, r);

    if (!sol_pwm_set_enabled(mdata->pwm, enabled))
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

    if (!sol_pwm_set_period(mdata->pwm, period))
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

    if (!sol_pwm_set_duty_cycle(mdata->pwm, dc))
        return -EIO;

    return 0;
}


static int
pwm_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_pwm_options *opts = (const struct sol_flow_node_type_pwm_options *)options;
    struct pwm_data *mdata = data;
    struct sol_pwm_config pwm_config = { 0 };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_PWM_OPTIONS_API_VERSION, -EINVAL);

    // Use values from options. Period = 0 considered invalid.
    if (opts->period.val <= 0) {
        SOL_WRN("Invalid value for period - pwm #%" PRId32 ":%" PRId32, opts->chip.val, opts->pin.val);
        return -EINVAL;
    }

    if (opts->duty_cycle.val < 0) {
        SOL_WRN("Invalid value for duty_cycle - pwm #%" PRId32 ":%" PRId32, opts->chip.val, opts->pin.val);
        return -EINVAL;
    }

    pwm_config.api_version = SOL_PWM_CONFIG_API_VERSION;
    pwm_config.period_ns = opts->period.val;
    pwm_config.duty_cycle_ns = opts->duty_cycle.val;
    if (opts->inversed_polarity)
        pwm_config.polarity = SOL_PWM_POLARITY_INVERSED;
    pwm_config.enabled = opts->enabled;

    mdata->pwm = sol_pwm_open(opts->chip.val, opts->pin.val, &pwm_config);
    if (!mdata->pwm) {
        SOL_WRN("could not open pwm #%" PRId32 ":%" PRId32, opts->chip.val, opts->pin.val);
        return -ENXIO;
    }

    return 0;
}

static void
pwm_close(struct sol_flow_node *node, void *data)
{
    struct pwm_data *mdata = data;

    sol_pwm_close(mdata->pwm);
}

#include "pwm-gen.c"
