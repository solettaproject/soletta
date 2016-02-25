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
#include <math.h>

#include "sol-flow-internal.h"
#include "sol-flow/robotics.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"

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

/*
 * Skid-steering odometry code based on sample code on [1].
 * Copyright (c) 2000 Dafydd Walters <dafydd@walters.net>
 * Permission to copy all or part of this article, and to use or modify
 * the code samples is FREELY GRANTED, with the condition that copyright
 * messages must be retained.
 *
 * [1]: http://www.seattlerobotics.org/encoder/200010/dead_reckoning_article.html
 */

struct skid_steer_odometer_data {
    struct sol_timeout *timeout;
    struct sol_flow_node *node;

    long left_ticks, right_ticks;

    struct sol_direction_vector cur_pos;
    double space_coeff;
    double axle_length;

    bool dirty;
};

static bool
update_odometry(void *data)
{
    struct skid_steer_odometer_data *priv = data;
    double left_dist, curr_sin, curr_cos;

    if (!priv->dirty)
        return true;

    priv->dirty = false;

    left_dist = priv->left_ticks * priv->space_coeff;
    curr_sin = sin(priv->cur_pos.z);
    curr_cos = cos(priv->cur_pos.z);

    if (priv->left_ticks == priv->right_ticks) {
        priv->cur_pos.x += left_dist * curr_cos;
        priv->cur_pos.y += left_dist * curr_sin;
    } else {
        const double right_dist = priv->right_ticks * priv->space_coeff;
        const double right_minus_left = right_dist - left_dist;
        const double distance = priv->axle_length * (right_dist + left_dist) / 2.0 / right_minus_left;

        priv->cur_pos.x += distance *
            (sin(right_minus_left / priv->axle_length + priv->cur_pos.z) - curr_sin);
        priv->cur_pos.y -= distance *
            (cos(right_minus_left / priv->axle_length + priv->cur_pos.z) - curr_cos);
        priv->cur_pos.z += right_minus_left / priv->axle_length;

        while (priv->cur_pos.z > M_PI)
            priv->cur_pos.z -= 2 * M_PI;
        while (priv->cur_pos.z < -M_PI)
            priv->cur_pos.z += 2 * M_PI;
    }

    sol_flow_send_direction_vector_packet(priv->node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_SKID_STEER_ODOMETER__OUT__OUT,
        &priv->cur_pos);

    return true;
}

static int
skid_steer_odometer_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct skid_steer_odometer_data *priv = data;
    const struct sol_flow_node_type_robotics_skid_steer_odometer_options *opts =
        (const struct sol_flow_node_type_robotics_skid_steer_odometer_options *)options;

    if (opts->pulses_per_revolution <= 0) {
        SOL_WRN("pulses_per_revolution must be greater than 0");
        return -EINVAL;
    }
    if (opts->axle_length <= 0) {
        SOL_WRN("axle_length must be greater than 0");
        return -EINVAL;
    }
    if (opts->wheel_diameter <= 0) {
        SOL_WRN("wheel_diameter must be greater than 0");
        return -EINVAL;
    }
    if (opts->update_period <= 1) {
        SOL_WRN("update_period must be greater than 1");
        return -EINVAL;
    }

    priv->axle_length = opts->axle_length;
    priv->space_coeff = M_PI * opts->wheel_diameter / opts->pulses_per_revolution;
    priv->right_ticks = priv->left_ticks = 0.0;
    priv->cur_pos = (struct sol_direction_vector) {
        .x = 0.0, .y = 0.0, .z = 0.0
    };
    priv->timeout = NULL;
    priv->node = node;

    priv->dirty = true;
    priv->timeout = sol_timeout_add(opts->update_period, update_odometry, priv);
    SOL_NULL_CHECK(priv->timeout, -errno);

    return 0;
}

static void
skid_steer_odometer_close(struct sol_flow_node *node, void *data)
{
    struct skid_steer_odometer_data *priv = data;

    sol_timeout_del(priv->timeout);
}

static int
skid_steer_odometer_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct skid_steer_odometer_data *priv = data;

    if (port == SOL_FLOW_NODE_TYPE_ROBOTICS_SKID_STEER_ODOMETER__IN__LEFT) {
        priv->left_ticks++;
    } else if (port == SOL_FLOW_NODE_TYPE_ROBOTICS_SKID_STEER_ODOMETER__IN__RIGHT) {
        priv->right_ticks++;
    }

    priv->dirty = true;

    return 0;
}

struct skid_steer_data {
    struct sol_flow_node *node;
    struct sol_timeout *timeout;
    struct sol_direction_vector curdir;

    int min_throttle, max_throttle;

    double turn_angle;
    int throttle;
};

static double
skid_steer_calculate_motor_output(double desired_angle, double measured_angle)
{
    double angle_error = fabs(measured_angle - desired_angle);
    double pct;

    if (desired_angle < 0.001)
        return 1.0;

    pct = angle_error / desired_angle;
    if (pct < 0.4)
        return 0.25;
    if (pct < 0.55)
        return 0.75;
    if (pct < 0.7)
        return 1.0;
    if (pct < 0.8)
        return 0.75;
    return 0.10;
}

static bool
skid_steer_control_motors(void *data)
{
    struct skid_steer_data *priv = data;
    double throttle_factor = skid_steer_calculate_motor_output(priv->turn_angle,
        priv->curdir.z);
    int throttle = (int)(throttle_factor * priv->throttle);
    int left_throttle, right_throttle;
    int r;

    if (throttle < priv->min_throttle)
        throttle = priv->min_throttle;
    else if (throttle > priv->max_throttle)
        throttle = priv->max_throttle;

    if (fabs(priv->turn_angle) < 0.1) {
        left_throttle = right_throttle = throttle;
    } else if (priv->turn_angle > 0) {
        left_throttle = throttle;
        right_throttle = -throttle;
    } else {
        left_throttle = -throttle;
        right_throttle = throttle;
    }

    r = sol_flow_send_irange_value_packet(priv->node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_SKID_STEER__OUT__LEFT_OUT,
        left_throttle);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_irange_value_packet(priv->node,
        SOL_FLOW_NODE_TYPE_ROBOTICS_SKID_STEER__OUT__RIGHT_OUT,
        right_throttle);
    SOL_INT_CHECK(r, < 0, r);

    return true;
}

static int
skid_steer_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct skid_steer_data *priv = data;
    const struct sol_flow_node_type_robotics_skid_steer_options *opts =
        (const struct sol_flow_node_type_robotics_skid_steer_options *)options;

    priv->node = node;
    priv->turn_angle = 0.0;
    priv->throttle = 100.0;

    priv->max_throttle = opts->max_throttle;
    priv->min_throttle = opts->min_throttle;
    if (priv->min_throttle >= priv->max_throttle) {
        SOL_WRN("min_throttle is greater than max_throttle");
        return -EINVAL;
    }

    priv->timeout = sol_timeout_add(100, skid_steer_control_motors, priv);
    SOL_NULL_CHECK(priv->timeout, -ENOMEM);

    return 0;
}

static void
skid_steer_close(struct sol_flow_node *node, void *data)
{
    struct skid_steer_data *priv = data;

    sol_timeout_del(priv->timeout);
}

static int
skid_steer_throttle_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct skid_steer_data *priv = data;
    int v, r;

    r = sol_flow_packet_get_irange_value(packet, &v);
    SOL_INT_CHECK(r, < 0, r);

    priv->throttle = v;

    return 0;
}

static int
skid_steer_turn_angle_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct skid_steer_data *priv = data;
    double v;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &v);
    SOL_INT_CHECK(r, < 0, r);

    priv->turn_angle = v;

    return 0;
}

static int
skid_steer_curdir_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct skid_steer_data *priv = data;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &priv->curdir);
    SOL_INT_CHECK(r, < 0, r);

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
