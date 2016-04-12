/*
 * This file is part of the Soletta Project
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

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>

#include "sol-mainloop.h"
#include "sol-connman.h"

#include "sol-util.h"
#include "sol-log.h"

#include "test.h"

void
callback(void *data, const struct sol_connman_service *service)
{
    char *str;
    enum sol_connman_service_state state;
    enum sol_connman_state g_state;
    int r;
    bool offline;

    str = sol_connman_service_get_name(service);
    if (!str)
        SOL_INF("service name = %s\n", str);
    else
        SOL_INF("service name = NULL\n");

    state = sol_connman_service_get_state(service);
    SOL_INF("service state = %d\n", state);

    str = sol_connman_service_get_type(service);
    if (!str)
        SOL_INF("service type = %s\n", str);
    else
        SOL_INF("service type = NULL\n");

    r = sol_connman_service_get_strength(service);
    SOL_INF("strength = %d\n", r);

    g_state = sol_connman_get_state();
    SOL_INF("system state = %d\n", g_state);

    offline = sol_connman_get_offline();
    SOL_INF("system offline = %d\n", offline);

}

static void
shutdown(void)
{
    sol_connman_add_service_monitor(callback, NULL);
}

static void
startup(void)
{
    sol_connman_add_service_monitor(callback, NULL);
}

SOL_MAIN_DEFAULT(startup, shutdown);
