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

    if (!strcmp(cmd, "monitor"))
        sol_platform_add_service_monitor(on_service_changed, param, NULL);
    else if (!strcmp(cmd, "stop-monitor"))
        sol_platform_del_service_monitor(on_service_changed, param, NULL);
    else if (!strcmp(cmd, "start"))
        sol_platform_start_service(param);
    else if (!strcmp(cmd, "stop"))
        sol_platform_stop_service(param);
    else if (!strcmp(cmd, "restart"))
        sol_platform_restart_service(param);
    else if (!strcmp(cmd, "target"))
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
