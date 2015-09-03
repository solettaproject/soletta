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

#include "sol-flow/compass.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
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
    if (!sol_drange_val_equal(fabs(pitch), M_PI / 2))
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
