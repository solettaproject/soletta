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

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mavlink.h>
#include <sol-util.h>
#include <stdio.h>

#define TAKEOFF_ALT 10
#define DEST_LAT -35.361354
#define DEST_LONG 149.166218
#define DEST_ALT 20

// are we greater than altitude margin?
#define GT_MARGIN(_lval, _rval) \
    (_lval > (_rval * 0.95)) \

static void
takeoff(struct sol_mavlink *mavlink)
{
    int err;
    struct sol_mavlink_position takeoff = { 0 };

    takeoff.altitude = TAKEOFF_ALT;
    err = sol_mavlink_take_off(mavlink, &takeoff);

    if (err < 0) {
        SOL_ERR("Could not takeoff: %s", sol_util_strerrora(-err));
        return;
    }

    printf(">>>> Taking off.\n");
}

static void
position_changed_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    struct sol_mavlink_position pos;

    err = sol_mavlink_get_current_position(mavlink, &pos);
    if (err < 0) {
        SOL_ERR("Could not get current position: %s", sol_util_strerrora(-err));
        return;
    }

    if (sol_mavlink_is_armed(mavlink))
        printf("lat: %f, long: %f, alt: %f\n", pos.latitude, pos.longitude,
            pos.altitude);
}

static void
mission_reached_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    enum sol_mavlink_mode mode;
    struct sol_mavlink_position home, curr;
    struct sol_mavlink_position dest = { DEST_LAT, DEST_LONG, DEST_ALT };

    mode = sol_mavlink_get_mode(mavlink);
    err = sol_mavlink_get_current_position(mavlink, &curr);
    if (err < 0) {
        SOL_ERR("Could not get current position: %s", sol_util_strerrora(-err));
        return;
    }

    if (mode != SOL_MAVLINK_MODE_GUIDED)
        return;

    if (GT_MARGIN(curr.altitude, DEST_ALT)) {
        printf(">>>> Going back home.\n");
        err = sol_mavlink_get_home_position(mavlink, &home);

        if (err < 0) {
            SOL_ERR("Could not get home position: %s", sol_util_strerrora(-err));
            return;
        }

        err = sol_mavlink_land(mavlink, &home);
        if (err < 0) {
            SOL_ERR("Could not land vehicle: %s", sol_util_strerrora(-err));
        }
    } else if (GT_MARGIN(curr.altitude, TAKEOFF_ALT)) {
        err = sol_mavlink_go_to(mavlink, &dest);
        if (err < 0) {
            SOL_ERR("Could not send vehicle to: (%f, %f, %f) - %s",
                dest.latitude, dest.longitude, dest.altitude,
                sol_util_strerrora(-err));
            return;
        }

        printf(">>>> Successful takeoff, starting a new mission, heading to: "
            "(%f, %f, %f)\n", dest.latitude, dest.longitude, dest.altitude);
    }
}

static void
mode_changed_cb(void *data, struct sol_mavlink *mavlink)
{
    int err;
    enum sol_mavlink_mode mode = sol_mavlink_get_mode(mavlink);
    bool armed = sol_mavlink_is_armed(mavlink);

    if (mode == SOL_MAVLINK_MODE_GUIDED && !armed) {
        err = sol_mavlink_set_armed(mavlink, true);
        if (err < 0) {
            SOL_ERR("Could not arm vechicle: %s", sol_util_strerrora(-err));
        }
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

    if (!sol_mavlink_is_armed(mavlink)) {
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
        SOL_SET_API_VERSION(.api_version = SOL_MAVLINK_HANDLERS_API_VERSION, )
        .connect = mavlink_connect_cb,
        .mode_changed = mode_changed_cb,
        .armed = armed_cb,
        .disarmed = disarmed_cb,
        .position_changed = position_changed_cb,
        .mission_reached = mission_reached_cb,
    };
    struct sol_mavlink_config config = {
        SOL_SET_API_VERSION(.api_version = SOL_MAVLINK_CONFIG_API_VERSION, )
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
