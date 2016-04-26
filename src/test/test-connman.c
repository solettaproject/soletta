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

#include "soletta.h"
#include "sol-netctl.h"

#include "sol-util.h"
#include "sol-log.h"

#include "test.h"

void
manager_cb(void *data)
{
    enum sol_connman_state g_state;
    bool offline;

    g_state = sol_connman_get_state();
    printf("system state = %d\n", g_state);

    offline = sol_connman_get_radios_offline();
    printf("system offline = %d\n", offline);
}

void
service_cb(void *data, const struct sol_connman_service *service)
{
    const char *str;
    int r;
    enum sol_connman_service_state state;

    str = sol_connman_service_get_name(service);
    if (str)
        printf("service name = %s\n", str);
    else
        printf("service name = NULL\n");

    state = sol_connman_service_get_state(service);
    printf("service state = %d\n", state);

    str = sol_connman_service_get_type(service);
    if (str)
        printf("service type = %s\n", str);
    else
        printf("service type = NULL\n");

    r = sol_connman_service_get_strength(service);
    printf("strength = %d\n", r);
}

static void
shutdown(void)
{
    sol_connman_del_manager_monitor(manager_cb, NULL);
    sol_connman_del_service_monitor(service_cb, NULL);
}

static void
startup(void)
{
    sol_connman_add_service_monitor(service_cb, NULL);
    sol_connman_add_manager_monitor(manager_cb, NULL);
}

SOL_MAIN_DEFAULT(startup, shutdown);
