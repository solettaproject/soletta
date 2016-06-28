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

#include "sol-flow/trigonometry.h"

#include "sol-flow-internal.h"

#include <sol-util-internal.h>
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
