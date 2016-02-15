/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <stdio.h>
#include <errno.h>

#include "sol-flow-internal.h"
#include "sol-flow/robotics.h"
#include "sol-log-internal.h"
#include "sol-pwm.h"

struct hbridge_data {
    struct {
        struct sol_pwm *pwm;
        uint32_t period;
        bool enabled;
    } pwm;
};

enum switches {
    SW_ALL_OFF = 0,
    SW_S1 = 1 << 0,
        SW_S2 = 1 << 1,
        SW_S3 = 1 << 2,
        SW_S4 = 1 << 3
};

static const uint32_t default_period_ns = 25000;

static int
hbridge_command(struct sol_flow_node *node, enum switches switches)
{
    int r;

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__S1,
        switches & SW_S1);
    if (r == 0) {
        r = sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__S2,
            switches & SW_S2);
    }
    if (r == 0) {
        r = sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__S3,
            switches & SW_S3);
    }
    if (r == 0) {
        r = sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__S4,
            switches & SW_S4);
    }

    return r;
}

static int
hbridge_process_cw(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return hbridge_command(node, SW_S1 | SW_S4);
}

static int
hbridge_process_ccw(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return hbridge_command(node, SW_S2 | SW_S3);
}

static int
hbridge_process_coast(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return hbridge_command(node, SW_ALL_OFF);
}

static int
hbridge_process_brake(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return hbridge_command(node, SW_S1 | SW_S3);
}

static int64_t
_map_to_percentage(int64_t in_value, int64_t in_min, int64_t in_max)
{
    if (unlikely(in_max == in_min))
        return 0;

    return ((in_value - in_min) * 100) / (in_max - in_min);
}

static int
hbridge_process_speed(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct hbridge_data *priv = data;
    uint32_t duty_cycle;
    struct sol_irange value;
    int32_t value_pct;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    value_pct = _map_to_percentage(value.val, value.min, value.max);
    if (value_pct < 0 || value_pct > 100) {
        SOL_WRN("H-Bridge speed should be between 0 (disabled) and 100 (full power)");
        return -EINVAL;
    }

    if (!value_pct) {
        if (priv->pwm.enabled) {
            priv->pwm.enabled = false;
            return sol_pwm_set_enabled(priv->pwm.pwm, false) ? 0 : -EIO;
        }

        return 0;
    }

    if (!priv->pwm.enabled) {
        sol_pwm_set_enabled(priv->pwm.pwm, true);
        priv->pwm.enabled = true;
    }

    duty_cycle = (priv->pwm.period * value_pct) / 100;
    return sol_pwm_set_duty_cycle(priv->pwm.pwm, duty_cycle) ? 0 : -EIO;
}

static int
hbridge_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct hbridge_data *priv = data;
    const struct sol_flow_node_type_robotics_hbridge_l293d_options *opts =
        (const struct sol_flow_node_type_robotics_hbridge_l293d_options *)options;
    struct sol_pwm_config pwm_config = {
        SOL_SET_API_VERSION(.api_version = SOL_PWM_CONFIG_API_VERSION),
        .duty_cycle_ns = 0,
        .enabled = false,
    };

    if (!opts->pwm_period_ns) {
        pwm_config.period_ns = default_period_ns;
    } else if (opts->pwm_period_ns < 0) {
        SOL_WRN("pwm_period_ns specified as a negative value; assuming default of %dns",
            default_period_ns);
        pwm_config.period_ns = default_period_ns;
    } else {
        pwm_config.period_ns = opts->pwm_period_ns;
    }

    if (!opts->pwm_pin || *opts->pwm_pin == '\0') {
        SOL_WRN("Pin cannot be either empty or null");
        return -EINVAL;
    }

    if (opts->pwm_raw) {
        int device, channel;

        if (sscanf(opts->pwm_pin, "%d %d", &device, &channel) == 2) {
            priv->pwm.pwm = sol_pwm_open(device, channel, &pwm_config);
        } else {
            SOL_WRN("hbridge pwm_pin(%s): pwm_raw option was set, but pin value could not be parsed as \"<device> <channel>\" pair.",
                opts->pwm_pin);
        }
    } else {
        priv->pwm.pwm = sol_pwm_open_by_label(opts->pwm_pin, &pwm_config);
    }

    SOL_NULL_CHECK_MSG(priv->pwm.pwm, -ENXIO, "Could not open PWM (%s)",
        opts->pwm_pin);

    return 0;
}

static void
hbridge_close(struct sol_flow_node *node, void *data)
{
    struct hbridge_data *priv = data;

    sol_pwm_close(priv->pwm.pwm);
}

/*
 * Algorithm based off of
 * https://web.archive.org/web/20150728092206/http://letsmakerobots.com/node/24031
 */

struct quadrature_encoder_data {
    int old, new;
    bool input_a, input_b;
};

static const int quadrature_encoder_matrix[] =
{ 0, -1, 1, 2, 1, 0, 2, -1, -1, 2, 0, 1, 2, 1, -1, 0 };

static int
quadrature_encoder_process(struct sol_flow_node *node,
    struct quadrature_encoder_data *priv)
{
    bool cw = false, ccw = false, stopped = false, invalid = false;
    int r;

    priv->old = priv->new;
    priv->new = priv->input_a * 2 + priv->input_b;

    switch (quadrature_encoder_matrix[priv->old * 4 + priv->new]) {
    case -1:
        cw = true;
        break;
    case 1:
        ccw = true;
        break;
    case 0:
        stopped = true;
        break;
    default:
        invalid = true;
    }

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__OUT__CW, cw);
    if (!r) {
        r = sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__OUT__CCW, ccw);
    }
    if (!r) {
        r = sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__OUT__STOPPED,
            stopped);
    }
    if (!r && invalid) {
        r = sol_flow_send_error_packet(node, -EINVAL,
            "Invalid state for the quadrature encoder");
    }

    return r;
}

static int
quadrature_encoder_process_port(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct quadrature_encoder_data *priv = data;
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (port == SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__IN__A)
        priv->input_a = value;
    else if (port == SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__IN__B)
        priv->input_b = value;

    return quadrature_encoder_process(node, priv);
}

static int
quadrature_encoder_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct quadrature_encoder_data *priv = data;

    priv->old = 0;
    priv->new = 0;
    priv->input_a = false;
    priv->input_b = false;

    return 0;
}

#include "robotics-gen.c"
