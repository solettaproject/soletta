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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#define SOL_LOG_DOMAIN &_sol_netctl_log_domain
#include <sol-log-internal.h>

#include "sol-bus.h"
#include "sol-netctl.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_sol_netctl_log_domain, "netctl");

int sol_netctl_init(void);
void sol_netctl_shutdown(void);

struct sol_netctl_service {
    sd_bus_slot *slot;
    char *path;
    char *name;
    enum sol_netctl_service_state state;
    char *type;
    struct sol_network_link link;
    int32_t strength;
};

static void
_init_connman_service(struct sol_netctl_service *service)
{
    SOL_SET_API_VERSION(service->link.api_version = SOL_NETWORK_LINK_API_VERSION; )
    sol_vector_init(&service->link.addrs, sizeof(struct sol_netctl_network_params));
}

static void
_free_connman_service(struct sol_netctl_service *service)
{
    service->slot = sd_bus_slot_unref(service->slot);

    free(service->path);
    free(service->type);
    sol_vector_clear(&service->link.addrs);
    free(service);
}

SOL_API const char *
sol_netctl_service_get_name(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return service->name;
}

SOL_API const char *
sol_netctl_service_get_type(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return service->type;
}

SOL_API enum sol_netctl_service_state
sol_netctl_service_get_state(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, SOL_NETCTL_SERVICE_STATE_UNKNOWN);

    return service->state;
}

SOL_API int
sol_netctl_service_get_network_address(const struct sol_netctl_service *service,
    struct sol_network_link **link)
{
    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(link, -EINVAL);

    *link = (struct sol_network_link *)&service->link;

    return 0;
}

SOL_API int32_t
sol_netctl_service_get_strength(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, -EINVAL);

    return service->strength;
}

SOL_API enum sol_netctl_state
sol_netctl_get_state(void)
{
    return SOL_NETCTL_STATE_UNKNOWN;
}

SOL_API int
sol_netctl_set_radios_offline(bool enabled)
{
    return 0;
}

SOL_API bool
sol_netctl_get_radios_offline(void)
{
    return true;
}

SOL_API int
sol_netctl_service_connect(struct sol_netctl_service *service)
{
    return 0;
}

SOL_API int
sol_netctl_service_disconnect(struct sol_netctl_service *service)
{
    return 0;
}

int
sol_netctl_init(void)
{
    return 0;
}

void
sol_netctl_shutdown(void)
{
    return;
}

SOL_API int
sol_netctl_add_service_monitor(
    sol_netctl_service_monitor_cb cb, const void *data)
{
    return 0;
}

SOL_API int
sol_netctl_del_service_monitor(
    sol_netctl_service_monitor_cb cb, const void *data)
{
    return 0;
}

SOL_API int
sol_netctl_add_manager_monitor(
    sol_netctl_manager_monitor_cb cb, const void *data)
{
    return 0;
}

SOL_API int
sol_netctl_del_manager_monitor(
    sol_netctl_manager_monitor_cb cb, const void *data)
{
    return 0;
}

SOL_API int
sol_netctl_add_error_monitor(
    sol_netctl_error_monitor_cb cb, const void *data)
{
    return 0;
}

SOL_API int
sol_netctl_del_error_monitor(
    sol_netctl_error_monitor_cb cb, const void *data)
{
    return 0;
}

SOL_API const struct sol_ptr_vector *
sol_netctl_get_services(void)
{
    return NULL;
}
