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

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>

#include "soletta.h"
#include "sol-netctl.h"

#include "sol-util.h"
#include "sol-log.h"

#define CONN_AP "Guest"
#define INPUT   "12345678"

struct sol_netctl_agent agent;

static void
manager_cb(void *data)
{
    enum sol_netctl_state g_state;
    bool offline;

    g_state = sol_netctl_get_state();
    printf("manager_cb system state = %d\n", g_state);

    offline = sol_netctl_get_radios_offline();
    printf("manager_cb system offline = %d\n", offline);
}

static void
service_cb(void *data, const struct sol_netctl_service *service)
{
    const char *str;
    int r;
    enum sol_netctl_service_state state;

    state = sol_netctl_service_get_state(service);
    printf("service_cb service state = %d\n", state);

    str = sol_netctl_service_get_type(service);
    if (str)
        printf("service_cb service type = %s\n", str);
    else
        printf("service_cb service type = NULL\n");

    r = sol_netctl_service_get_strength(service);
    printf("service_cb strength = %d\n", r);

    str = sol_netctl_service_get_name(service);
    if (str)
        printf("service_cb service name = %s\n", str);
    else
        printf("service_cb service name = NULL\n");

    if (str && strcmp(str, CONN_AP) == 0) {
        if (state == SOL_NETCTL_SERVICE_STATE_IDLE) {
            printf("connect AP\n");
            sol_netctl_service_connect((struct sol_netctl_service *)service);
        } else if (state == SOL_NETCTL_SERVICE_STATE_READY) {
            printf("Disconnect AP\n");
            sol_netctl_service_disconnect((struct sol_netctl_service *)service);
        }
    }
}

static void
error_cb(void *data, const struct sol_netctl_service *service,
    unsigned int error)
{
    const char *str;

    str = sol_netctl_service_get_name(service);
    if (str)
        printf("error_cb service name = %s\n", str);
    else
        printf("error_cb service name = NULL\n");

    printf("error_cb error is %d\n", error);
}

static void
report_error(void *data, const struct sol_netctl_service *service,
    const char *error)
{
    int r;

    printf("The agent action error is %s\n", error);
    r = sol_netctl_request_retry((struct sol_netctl_service *)service,
        false);
    printf("The agent request retry return value is %d\n", r);
}

static void
request_input(void *data, const struct sol_netctl_service *service,
    const struct sol_ptr_vector *vector)
{
    int r;
    uint16_t i;
    char *value;
    struct sol_netctl_agent_input *input;
    struct sol_ptr_vector input_vector;

    printf("The agent action is input\n");
    sol_ptr_vector_init(&input_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (vector, value, i) {
        printf("The agent input type is %s\n", value);
        input = calloc(1, sizeof(struct sol_netctl_agent_input));
        if (!input) {
            printf("No Memory for the agent input\n");
            goto fail;
        }

        r = sol_ptr_vector_append(&input_vector, input);
        if (r < 0) {
            printf("append the agent input error\n");
            free(input);
            goto fail;
        }

        SOL_SET_API_VERSION(input->api_version = SOL_NETCTL_AGENT_INPUT_API_VERSION; )
        input->type = strdup(value);
        if (!input->type) {
            printf("No Memory for the agent input\n");
            goto fail;
        }

        input->input = strdup(INPUT);
        if (!input->input) {
            printf("No Memory for the agent input\n");
            goto fail;
        }

    }

    r = sol_netctl_request_input((struct sol_netctl_service *)service,
        &input_vector);
    printf("The agent report input return value is %d\n", r);

fail:
    SOL_PTR_VECTOR_FOREACH_IDX (&input_vector, input, i) {
        if (input->input)
            free(input->input);
        if (input->type)
            free(input->type);
        free(input);
    }

    sol_ptr_vector_clear(&input_vector);
}

static void
cancel(void *data)
{
    printf("The agent action is cancelled\n");
}

static void
release(void *data)
{
    printf("The agent action is release\n");
}

static void
shutdown(void)
{
    int r;

    r = sol_netctl_unregister_agent();
    printf("unregister agent return value r = %d\n", r);
    sol_netctl_del_manager_monitor(manager_cb, NULL);
    sol_netctl_del_service_monitor(service_cb, NULL);
    sol_netctl_del_error_monitor(error_cb, NULL);
}

static void
startup(void)
{
    int r;

    sol_netctl_add_service_monitor(service_cb, NULL);
    sol_netctl_add_manager_monitor(manager_cb, NULL);
    sol_netctl_add_error_monitor(error_cb, NULL);

    SOL_SET_API_VERSION(agent.api_version = SOL_NETCTL_AGENT_API_VERSION; )
    agent.report_error = report_error;
    agent.request_input = request_input;
    agent.cancel = cancel;
    agent.release = release;

    r = sol_netctl_register_agent(&agent, NULL);
    printf("register agent return value r = %d\n", r);

    r = sol_netctl_scan();
    printf("scan devices return value r = %d\n", r);
}

SOL_MAIN_DEFAULT(startup, shutdown);
