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

#include "sol-flow/compass.h"
#include "sol-flow-internal.h"

#include <sol-util-internal.h>
#include <errno.h>
#include <math.h>

struct compass_data {
    struct sol_direction_vector accel;
    struct sol_direction_vector mag;
    struct sol_direction_vector result;
    double heading;
    bool has_accel : 1;
    bool has_mag : 1;
};

/* Normalize a value on an arbitrary range to [-1, 1] range */
static double
_normalize(double value, double min, double max)
{
    return ((value - min) * 2.0) / (max - min) - 1.0;
}

static void
_normalize_data(struct compass_data *mdata)
{
    /* Data is normalized based on direction-vector min and max. Maybe,
     * it would be a good idea using the actual min and max present on readings.
     */
    mdata->accel.x = _normalize(mdata->accel.x, mdata->accel.min, mdata->accel.max);
    mdata->accel.y = _normalize(mdata->accel.y, mdata->accel.min, mdata->accel.max);
    mdata->accel.z = _normalize(mdata->accel.z, mdata->accel.min, mdata->accel.max);

    mdata->mag.x = _normalize(mdata->mag.x, mdata->mag.min, mdata->mag.max);
    mdata->mag.y = _normalize(mdata->mag.y, mdata->mag.min, mdata->mag.max);
    mdata->mag.z = _normalize(mdata->mag.z, mdata->mag.min, mdata->mag.max);
}

/* Calcule Magnetic North Pole direction based on
 * https://www.sparkfun.com/datasheets/Sensors/Magneto/Tilt%20Compensated%20Compass.pdf
 * Appendix A.
 */
static void
_calculate_result(struct compass_data *mdata)
{
    double pitch, roll, heading, mx, my, mz;

    _normalize_data(mdata);

    pitch = asin(-mdata->accel.x);
    if (!sol_util_double_eq(fabs(pitch), M_PI / 2))
        roll = asin(mdata->accel.y / cos(pitch));
    else
        roll = 0.0;

    mx = mdata->mag.x * cos(pitch) + mdata->mag.z * sin(pitch);
    my = mdata->mag.x * sin(roll) * sin(pitch) + mdata->mag.y * cos(roll) - mdata->mag.z * sin(roll) * cos(pitch);
    mz = (-mdata->mag.x) * cos(roll) * sin(pitch) + mdata->mag.y * sin(roll) + mdata->mag.z * cos(roll) * cos(pitch);

    heading = 180 * atan2(my, mx) / M_PI;
    if (my >= 0)
        heading += 360;

    mdata->result.min = -1.0;
    mdata->result.max = 1.0;
    mdata->result.x = mx;
    mdata->result.y = my;
    mdata->result.z = mz;
    mdata->heading = heading;
}

static void
_send_result(struct sol_flow_node *node, struct compass_data *mdata)
{
    _calculate_result(mdata);

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_COMPASS_ACCELEROMETER_MAGNETOMETER__OUT__VECTOR, &mdata->result);
    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_COMPASS_ACCELEROMETER_MAGNETOMETER__OUT__HEADING, mdata->heading);

    mdata->has_accel = false;
    mdata->has_mag = false;
}

static int
compass_accel_open(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct compass_data *mdata = data;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &mdata->accel);
    SOL_INT_CHECK(r, < 0, r);

    mdata->has_accel = true;
    if (mdata->has_mag)
        _send_result(node, mdata);

    return 0;
}

static int
compass_mag_open(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct compass_data *mdata = data;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &mdata->mag);
    SOL_INT_CHECK(r, < 0, r);

    mdata->has_mag = true;
    if (mdata->has_accel)
        _send_result(node, mdata);

    return 0;
}

#include "compass-gen.c"
