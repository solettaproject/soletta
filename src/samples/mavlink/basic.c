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

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mavlink.h>
#include <sol-util.h>
#include <stdio.h>

#define TAKEOFF_ALT 10

static void
takeoff(struct sol_mavlink *mavlink)
{
    int err;
    struct sol_mavlink_position takeoff = { 0 };

    takeoff.altitude = TAKEOFF_ALT;
    err = sol_mavlink_takeoff(mavlink, &takeoff);

    if (err < 0) {
        SOL_ERR("Could not takeoff: %s", sol_util_strerrora(-err));
        return;
    }

    printf(">>>> Taking off.\n");
}

static void
mode_changed_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    enum sol_mavlink_mode mode = sol_mavlink_get_mode(mavlink);
    bool armed = sol_mavlink_check_armed(mavlink);

    if (mode == SOL_MAVLINK_MODE_GUIDED && !armed) {
        err = sol_mavlink_set_armed(mavlink, true);
        if (err < 0) {
            SOL_ERR("Could not arm vechicle: %s", sol_util_strerrora(-err));
        }
    }
}

static void
position_changed_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    struct sol_mavlink_position pos;

    err = sol_mavlink_get_curr_position(mavlink, &pos);
    if (err < 0) {
        SOL_ERR("Could not get current position: %s", sol_util_strerrora(-err));
        return;
    }

    if (sol_mavlink_check_armed(mavlink))
        printf("lat: %f, long: %f, alt: %f\n", pos.latitude, pos.longitude,
            pos.altitude);
}

static void
armed_cb(void *data, struct sol_mavlink *mavlink)
{
    enum sol_mavlink_mode mode;

    SOL_DBG("vehicle just armed");

    mode = sol_mavlink_get_mode(mavlink);
    if (mode == SOL_MAVLINK_MODE_GUIDED) {
        takeoff(mavlink);
    }
}

static void
disarmed_cb(void *data, struct sol_mavlink *mavlink)
{
    enum sol_mavlink_mode mode;

    mode = sol_mavlink_get_mode(mavlink);
    if (mode == SOL_MAVLINK_MODE_LAND)
        printf(">>>> Landed...\n");
}

static void
mission_reached_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    enum sol_mavlink_mode mode;
    struct sol_mavlink_position home;

    mode = sol_mavlink_get_mode(mavlink);
    err = sol_mavlink_get_home_position(mavlink, &home);
    if (err < 0) {
        SOL_ERR("Could not get home position: %s", sol_util_strerrora(-err));
        return;
    }

    if (mode != SOL_MAVLINK_MODE_GUIDED)
        return;

    err = sol_mavlink_land(mavlink, &home);
    if (err < 0) {
        SOL_ERR("Could not land vehicle: %s", sol_util_strerrora(-err));
        return;
    }

    printf(">>>> Successful takeoff, now landing.\n");
}

static void
mavlink_connect_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    enum sol_mavlink_mode mode;

    SOL_INF("mavlink connection stablished");

    mode = sol_mavlink_get_mode(mavlink);
    if (mode != SOL_MAVLINK_MODE_GUIDED) {
        err = sol_mavlink_set_mode(mavlink, SOL_MAVLINK_MODE_GUIDED);
        if (err < 0) {
            SOL_ERR("Could not set mode: %s", sol_util_strerrora(-err));
        }
        return;
    }

    if (!sol_mavlink_check_armed(mavlink)) {
        err = sol_mavlink_set_armed(mavlink, true);
        if (err < 0) {
            SOL_ERR("Could not arm vechicle: %s", sol_util_strerrora(-err));
        }
        return;
    }

    takeoff(mavlink);
}

int
main(int argc, char *argv[])
{
    struct sol_mavlink *mavlink;
    struct sol_mavlink_handlers mavlink_handlers = {
        .connect = mavlink_connect_cb,
        .position_changed = position_changed_cb,
        .mode_changed = mode_changed_cb,
        .armed = armed_cb,
        .disarmed = disarmed_cb,
        .mission_reached = mission_reached_cb,
    };
    struct sol_mavlink_config config = {
        .handlers = &mavlink_handlers,
    };

    sol_init();

    if (argc < 2) {
        SOL_ERR("Usage: %s <address>", argv[0]);
        goto err;
    }

    mavlink = sol_mavlink_connect(argv[1], &config, NULL);
    if (!mavlink) {
        SOL_ERR("Unable to stablish a Mavlink connection");
        goto err;
    }

    sol_run();
    sol_mavlink_disconnect(mavlink);
    sol_shutdown();
    return EXIT_SUCCESS;

err:
    sol_shutdown();
    return EXIT_FAILURE;
}
