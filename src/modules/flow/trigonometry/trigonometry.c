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

#include "sol-flow/trigonometry.h"

#include "sol-flow-internal.h"

#include <sol-util.h>
#include <math.h>
#include <float.h>
#include <errno.h>


struct trigonometry_node_type {
    struct sol_flow_node_type base;
    double (*func) (double value);
};

struct trigonometry {
    double min;
    double max;
};

static int
_trigonometry_calculate(struct sol_flow_node *node, const struct sol_flow_packet *packet, const struct trigonometry *trig)
{
    const struct trigonometry_node_type *type;
    struct sol_drange value;
    double angle;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &angle);
    SOL_INT_CHECK(r, < 0, r);

    type = (const struct trigonometry_node_type *)
        sol_flow_node_get_type(node);

    value.val = type->func(angle);
    if (isnan(value.val)) {
        SOL_WRN("Angle out of domain");
        return -errno;
    }

    value.min = trig->min;
    value.max = trig->max;
    value.step = 0;

    return sol_flow_send_drange_packet(node, 0, &value);
}

static int
cosine_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const struct trigonometry trig = {
        .min = -1,
        .max = 1,
    };

    return _trigonometry_calculate(node, packet, &trig);
}

static int
sine_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const struct trigonometry trig = {
        .min = -1,
        .max = 1,
    };

    return _trigonometry_calculate(node, packet, &trig);
}

static int
tangent_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const struct trigonometry trig = {
        .min = -DBL_MAX,
        .max = DBL_MAX,
    };

    return _trigonometry_calculate(node, packet, &trig);
}

static const double RAD_MAX = 2 * M_PI;
static const double DEGREES_MAX = 360;

static double
radian_to_degrees(double radian)
{
    const double degrees_per_rad = DEGREES_MAX / RAD_MAX;

    return fmod(radian * degrees_per_rad, DEGREES_MAX);
}

static int
radian_to_degrees_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct trigonometry trig = {
        .min = 0,
        .max = RAD_MAX,
    };

    return _trigonometry_calculate(node, packet, &trig);
}

static double
degrees_to_radian(double degrees)
{
    const double rad_per_degrees = RAD_MAX / DEGREES_MAX;

    return fmod(degrees * rad_per_degrees, RAD_MAX);
}

static int
degrees_to_radian_convert(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct trigonometry trig = {
        .min = 0,
        .max = DEGREES_MAX,
    };

    return _trigonometry_calculate(node, packet, &trig);
}

#include "trigonometry-gen.c"
