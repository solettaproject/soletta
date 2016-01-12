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

enum switches {
    SW_ALL_OFF = 0,
    SW_S1 = 1 << 0,
        SW_S2 = 1 << 1,
        SW_S3 = 1 << 2,
        SW_S4 = 1 << 3
};

struct hbridge_data {
    bool inverted;
};

static int
hbridge_command(struct sol_flow_node *node, enum switches switches)
{
    int r;

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__OUT_0,
        switches & SW_S1);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__OUT_0 + 1,
        switches & SW_S2);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__OUT_0 + 2,
        switches & SW_S3);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__OUT_0 + 3,
        switches & SW_S4);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int64_t
map_to_percentage(int64_t in_value, int64_t in_min, int64_t in_max)
{
    if (SOL_UNLIKELY(in_max == in_min))
        return 0;

    return ((in_value - in_min) * 100) / (in_max - in_min);
}

static int
hbridge_process_in(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct hbridge_data *priv = data;
    struct sol_irange value;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value.val == 0) {
        r = hbridge_command(node, SW_ALL_OFF);
    } else {
        if (priv->inverted)
            value.val = -value.val;

        if (value.val > 0) {
            r = hbridge_command(node, SW_S1 | SW_S4);
        } else {
            value.val = -value.val;
            r = hbridge_command(node, SW_S2 | SW_S3);
        }
    }

    if (!r) {
        int64_t pct = map_to_percentage(value.val, value.min, value.max);

        r = sol_flow_send_irange_value_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__THROTTLE,
            pct);
    }

    return r;
}

static int
hbridge_process_brake(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return hbridge_command(node, SW_S1 | SW_S3);
}

static int
hbridge_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct hbridge_data *priv = data;
    const struct sol_flow_node_type_robotics_hbridge_l293d_options *opts =
        (const struct sol_flow_node_type_robotics_hbridge_l293d_options *)options;

    priv->inverted = opts->inverted;

    return 0;
}

struct quadrature_encoder_data {
    bool last_a, input_a, input_b;
};

static int
quadrature_encoder_process(struct sol_flow_node *node,
    struct quadrature_encoder_data *priv)
{
    int value;

    if (!priv->last_a && priv->input_a) {
        if (priv->input_b)
            value = 1; /* clockwise */
        else
            value = -1; /* counter-clockwise */
    } else {
        value = 0; /* stopped */
    }
    priv->last_a = priv->input_a;

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__OUT__OUT, value);
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

    priv->last_a = false;
    priv->input_a = false;
    priv->input_b = false;

    return 0;
}

struct pid_controller_data {
    double kp, ki, kd;
    double set_point;
    double last_error;
    double integral;
    struct timespec last_time;
};

static int
pid_controller_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct pid_controller_data *priv = data;
    double value, error, dt_sec;
    double p, i, d;
    struct timespec dt, now;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    now = sol_util_timespec_get_current();
    sol_util_timespec_sub(&now, &priv->last_time, &dt);
    dt_sec = ((double)sol_util_msec_from_timespec(&dt) / (double)SOL_MSEC_PER_SEC);

    error = priv->set_point - value;
    p = error;
    i = priv->integral + error * dt_sec;
    d = (dt_sec >= 0.0001) ? (error - priv->last_error) / dt_sec : 0.0;

    r = sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_PID__OUT__OUT,
        p * priv->kp + i * priv->ki + d * priv->kd);

    priv->integral = i;
    priv->last_time = now;
    priv->last_error = error;

    return r;
}

static int
pid_controller_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct pid_controller_data *priv = data;
    const struct sol_flow_node_type_robotics_pid_options *opts =
        (const struct sol_flow_node_type_robotics_pid_options *)options;

    priv->kp = opts->kp;
    priv->ki = opts->ki;
    priv->kd = opts->kd;
    priv->set_point = opts->set_point;

    priv->last_error = 0.0;
    priv->integral = 0.0;
    priv->last_time = sol_util_timespec_get_current();

    return 0;
}

#include "robotics-gen.c"
