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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "sol-mainloop.h"
#include "sol-platform.h"
#include "sol-util.h"

#define CMD_TICK 2000

static char **cmds;
static int n_cmds;
static int cur_cmd;
static struct sol_timeout *timeout_handle;

static void
on_state_change(void *data, enum sol_platform_state state)
{
    printf("Platform state changed. New state: %d\n", state);
}

static void
on_service_changed(void *data, const char *service,
    enum sol_platform_service_state state)
{
    printf("Service state changed: '%s'. New state: %d\n", service, state);
}

static bool
on_timeout_cmd(void *data)
{
    const char *cmd;
    const char *param;

    cmd = cmds[cur_cmd++];
    param = cmds[cur_cmd++];

    printf("Firing new command: %s %s\n", cmd, param);

    if (streq(cmd, "monitor"))
        sol_platform_add_service_monitor(on_service_changed, param, NULL);
    else if (streq(cmd, "stop-monitor"))
        sol_platform_del_service_monitor(on_service_changed, param, NULL);
    else if (streq(cmd, "start"))
        sol_platform_start_service(param);
    else if (streq(cmd, "stop"))
        sol_platform_stop_service(param);
    else if (streq(cmd, "restart"))
        sol_platform_restart_service(param);
    else if (streq(cmd, "target"))
        sol_platform_set_target(param);

    if (n_cmds - cur_cmd >= 2)
        return true;

    timeout_handle = NULL;
    return false;
}

int
main(int argc, char *argv[])
{
    int r = 0;

    if (sol_init() < 0)
        return EXIT_FAILURE;

    printf("Initial platform state: %d\n", sol_platform_get_state());
    sol_platform_add_state_monitor(on_state_change, NULL);

    if (argc > 2) {
        cmds = argv + 1;
        n_cmds = argc - 1;
        timeout_handle = sol_timeout_add(CMD_TICK, on_timeout_cmd, NULL);
    }

    sol_run();

    if (timeout_handle)
        sol_timeout_del(timeout_handle);

    sol_platform_del_state_monitor(on_state_change, NULL);

    sol_shutdown();

    return r;
}
