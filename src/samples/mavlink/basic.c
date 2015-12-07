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
#include <stdio.h>

#define ALTITUDE_TAKEOFF 10

struct context {
    struct sol_mavlink *mavlink;
    struct sol_timeout *timer;
    bool landing;
};

static bool
land_on_timeout(void *data)
{
    struct sol_mavlink_position home;
    struct context *ctx = data;
    struct sol_mavlink *mavlink = ctx->mavlink;

    sol_mavlink_get_home_position(mavlink, &home);
    sol_mavlink_land(mavlink, &home);
    ctx->landing = true;
    printf("time to go home... landing...\n");

    return false;
}

static void
takeoff(struct context *ctx, struct sol_mavlink *mavlink)
{
    struct sol_mavlink_position takeoff = { 0 };

    if (ctx->timer) {
        sol_timeout_del(ctx->timer);
        ctx->timer = NULL;
    }

    /** wait 10 seconds and then land it back */
    ctx->timer = sol_timeout_add(1000 * 10, land_on_timeout, ctx);
    takeoff.altitude = ALTITUDE_TAKEOFF;
    sol_mavlink_takeoff(mavlink, &takeoff);
}

static void
mode_changed_cb(void *data, struct sol_mavlink *mavlink)
{
    struct context *ctx = data;

    if (ctx->landing)
        return;

    if (sol_mavlink_check_mode(mavlink, SOL_MAVLINK_MODE_GUIDED))
        sol_mavlink_set_armed(mavlink, true);
}

static void
armed_cb(void *data, struct sol_mavlink *mavlink)
{
    struct context *ctx = data;

    if (ctx->landing)
        return;

    if (sol_mavlink_check_mode(mavlink, SOL_MAVLINK_MODE_GUIDED)) {
        takeoff(data, mavlink);
    }
}

static void
disarmed_cb(void *data, struct sol_mavlink *mavlink)
{
    struct context *ctx = data;
    bool landed = false;

    landed = sol_mavlink_check_mode(mavlink, SOL_MAVLINK_MODE_LAND);
    if (ctx->landing) {
        if (landed) {
            printf("We've landed successfully.\n");
            return;
        } else {
            printf("We're landing.\n");
            return;
        }
    }

    sol_mavlink_set_armed(mavlink, true);
}

static void
mavlink_connect_cb(void *data, struct sol_mavlink *mavlink)
{
    struct sol_mavlink_position home, curr;

    if (!sol_mavlink_check_mode(mavlink, SOL_MAVLINK_MODE_GUIDED)) {
        sol_mavlink_set_mode(mavlink, SOL_MAVLINK_MODE_GUIDED);
        return;
    }

    if (!sol_mavlink_check_armed(mavlink)) {
        sol_mavlink_set_armed(mavlink, true);
    }

    sol_mavlink_get_curr_position(mavlink, &curr);
    sol_mavlink_get_home_position(mavlink, &home);

    if (curr.altitude == home.altitude) {
        takeoff(data, mavlink);
    }
}

static void
position_changed_cb(void *data, struct sol_mavlink *mavlink)
{
    struct sol_mavlink_position pos;

    sol_mavlink_get_curr_position(mavlink, &pos);
    printf("position: %d, %d, %d\n", pos.latitude, pos.longitude, pos.altitude);
}

int
main(int argc, char *argv[])
{
    struct context ctx;
    struct sol_mavlink *mavlink;
    struct sol_mavlink_handlers mavlink_handlers = {
        .connect = mavlink_connect_cb,
        .mode_changed = mode_changed_cb,
        .armed = armed_cb,
        .disarmed = disarmed_cb,
        .position_changed = position_changed_cb,
    };
    struct sol_mavlink_config config = {
        .handlers = &mavlink_handlers,
    };

    sol_init();

    if (argc < 2) {
        SOL_ERR("Usage: %s <address>", argv[0]);
        goto err;
    }

    ctx.timer = NULL;
    ctx.landing = false;
    mavlink = sol_mavlink_connect(argv[1], &config, &ctx);
    if (!mavlink) {
        SOL_ERR("Unable to stablish a Mavlink connection");
        goto err;
    }

    ctx.mavlink = mavlink;

    sol_run();
    sol_mavlink_disconnect(mavlink);
    sol_shutdown();
    return EXIT_SUCCESS;

err:
    sol_shutdown();
    return EXIT_FAILURE;
}
