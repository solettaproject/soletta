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
        r = sol_flow_send_irange_packet(node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_HBRIDGE_L293D__OUT__THROTTLE,
            &value);
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

    return hbridge_process_brake(node, data, 0, 0, NULL);
}

struct quadrature_encoder_data {
    struct sol_timeout *timeout;
    struct sol_flow_node *node;

    int old_index, new_index;
    int ticks;

    bool input_a, input_b;
};

static int
quadrature_encoder_process_port(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    /* Matrix based off of: http://www.robotshop.com/media/files/PDF/tutorial-how-to-use-a-quadrature-encoder-rs011a.pdf */
    static const int8_t qem[16] = { 0, -1, 1, 2, 1, 0, 2, -1, -1, 2, 0, 1, 2, 1, -1, 0 };
    struct quadrature_encoder_data *priv = data;
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (port == SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__IN__A)
        priv->input_a = value;
    else if (port == SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__IN__B)
        priv->input_b = value;

    priv->old_index = priv->new_index;
    priv->new_index = priv->input_a * 2 + priv->input_b;

    r = qem[priv->old_index * 4 + priv->new_index];
    if (r != 2) {
        priv->ticks += r;
    } else {
        SOL_WRN("Invalid state for quadrature encoder; losing I/O?");
    }

    return 0;
}

static bool
quadrature_encoder_send_ticks(void *data)
{
    struct quadrature_encoder_data *priv = data;

    if (priv->ticks) {
        int r;

        r = sol_flow_send_irange_value_packet(priv->node,
            SOL_FLOW_NODE_TYPE_ROBOTICS_QUADRATURE_ENCODER__OUT__OUT,
            priv->ticks);
        priv->ticks = 0;

        SOL_INT_CHECK(r, < 0, true);
    }

    return true;
}

static int
quadrature_encoder_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct quadrature_encoder_data *priv = data;
    const struct sol_flow_node_type_robotics_quadrature_encoder_options *opts =
        (const struct sol_flow_node_type_robotics_quadrature_encoder_options *)options;

    SOL_INT_CHECK(opts->period, < 0, -EINVAL);

    priv->old_index = priv->new_index = 0;
    priv->input_a = priv->input_b = 0;
    priv->ticks = 0;

    priv->node = node;
    priv->timeout = sol_timeout_add(opts->period, quadrature_encoder_send_ticks, priv);
    SOL_NULL_CHECK(priv->timeout, -ENOMEM);

    return 0;
}

static void
quadrature_encoder_close(struct sol_flow_node *node, void *data)
{
    struct quadrature_encoder_data *priv = data;

    sol_timeout_del(priv->timeout);
}

#include "robotics-gen.c"
