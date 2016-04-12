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

#define SOL_LOG_DOMAIN &_sol_connman_log_domain
#include <sol-log-internal.h>

#include "sol-bus.h"
#include "sol-connman.h"

SOL_LOG_INTERNAL_DECLARE(_sol_connman_log_domain, "connman");

int sol_connman_init(void);
void sol_connman_shutdown(void);

struct sol_connman_service {
    sd_bus_slot *slot;
    char *path;
    char *name;
    enum sol_connman_service_state state;
    char *type;
    struct sol_network_link_addr *link_addr;
    struct sol_network_link_addr *link_addr6;
    int32_t strength;
    bool is_call_success;
};

struct ctx {
    struct sol_bus_client *connman;
    sd_bus_slot *manager_slot;
    sd_bus_slot *state_slot;
    enum sol_connman_state connman_state;
};

static struct ctx _ctx;

static void
_free_connman_service(struct sol_connman_service *service)
{
    if (!service)
        return;

    service->slot = sd_bus_slot_unref(service->slot);

    free(service->path);
    free(service->type);
    free(service->link_addr);
    free(service->link_addr6);
}

SOL_API bool
sol_connman_service_get_call_result(const struct sol_connman_service *service)
{
    SOL_NULL_CHECK(service, false);

    return service->is_call_success;
}

SOL_API const char *
sol_connman_service_get_name(const struct sol_connman_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return service->name;
}

SOL_API const char *
sol_connman_service_get_type(const struct sol_connman_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return service->type;
}

SOL_API enum sol_connman_service_state
sol_connman_service_get_state(const struct sol_connman_service *service)
{
    SOL_NULL_CHECK(service, SOL_CONNMAN_SERVICE_STATE_UNKNOWN);

    return service->state;
}

SOL_API struct sol_network_link_addr *
sol_connman_service_get_network_address(const struct sol_connman_service *service,
    enum sol_network_family family)
{
    SOL_NULL_CHECK(service, NULL);

    if (family == SOL_NETWORK_FAMILY_INET)
        return service->link_addr;
    else if (family == SOL_NETWORK_FAMILY_INET6)
        return service->link_addr6;
    else
        return NULL;
}

SOL_API int32_t
sol_connman_service_get_strength(const struct sol_connman_service *service)
{
    SOL_NULL_CHECK(service, -EINVAL);

    return service->strength;
}

static int
get_manager_properties(sd_bus_message *m)
{
    char *state;
    int r;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &state);
    SOL_INT_CHECK(r, < 0, r);

    if (state) {
        if (!strcmp(state, "online"))
            _ctx.connman_state = SOL_CONNMAN_STATE_ONLINE;
        else if (!strcmp(state, "ready"))
            _ctx.connman_state = SOL_CONNMAN_STATE_READY;
        else if (!strcmp(state, "idle"))
            _ctx.connman_state = SOL_CONNMAN_STATE_IDLE;
        else if (!strcmp(state, "offline"))
            _ctx.connman_state = SOL_CONNMAN_STATE_OFFLINE;
        else
            _ctx.connman_state = SOL_CONNMAN_STATE_UNKNOWN;
    }

    r = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
_manager_properties_changed(sd_bus_message *m, void *userdata,
    sd_bus_error *ret_error)
{
    struct ctx *pending = userdata;
    char *str;
    int r;

    pending->manager_slot = sd_bus_slot_unref(pending->manager_slot);

    if (sol_bus_log_callback(m, userdata, ret_error) < 0)
        return -EINVAL;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK_GOTO(r, < 1, end);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK(r, < 0, r);

        if (strcmp(str, "State") == 0) {
            r = get_manager_properties(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    if (r == 0)
        r = sd_bus_message_exit_container(m);

    return r;
}

static int
dbus_connection_get_manager_properties(void)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    r = sd_bus_call_method_async(bus, &_ctx.manager_slot,
        "net.connman", "/", "net.connman.Manager", "GetProperties",
        _manager_properties_changed, &_ctx, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API enum sol_connman_state
sol_connman_get_state(void)
{
    return _ctx.connman_state;
}

static int
_set_state_property_changed(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct ctx *pending = userdata;

    pending->state_slot = sd_bus_slot_unref(pending->state_slot);

    return sol_bus_log_callback(reply, userdata, ret_error);
}

SOL_API int
sol_connman_set_offline(bool enabled)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    r = sd_bus_call_method_async(bus, &_ctx.state_slot, "net.connman", "/",
        "net.connman.Manager", "SetProperty", _set_state_property_changed,
        &_ctx, "sv", "OfflineMode", "b", enabled);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API bool
sol_connman_get_offline(void)
{
    if (_ctx.connman_state != SOL_CONNMAN_STATE_OFFLINE)
        return false;

    return true;
}

SOL_API int
sol_connman_service_connect(struct sol_connman_service *service)
{
    return 0;
}

SOL_API int
sol_connman_service_disconnect(struct sol_connman_service *service)
{
    return 0;
}

int
sol_connman_init(void)
{
    return 0;
}

void
sol_connman_shutdown(void)
{
    return;
}

static int
sol_connman_init_lazy(void)
{
    sd_bus *bus;

    if (_ctx.connman)
        return 0;

    bus = sol_bus_get(NULL);
    if (!bus) {
        SOL_WRN("Unable to get sd bus\n");
        return -EINVAL;
    }

    _ctx.connman = sol_bus_client_new(bus, "org.connman");
    if (!_ctx.connman) {
        SOL_WRN("Unable to new a bus client\n");
        return -EINVAL;
    }

    return 0;
}

static void
sol_connman_shutdown_lazy(void)
{
    struct sol_connman_service *service;

    if (_ctx.connman) {
        sol_bus_client_free(_ctx.connman);
        _ctx.connman = NULL;
    }

    _ctx.state_slot =
        sd_bus_slot_unref(_ctx.state_slot);
    _ctx.manager_slot =
        sd_bus_slot_unref(_ctx.manager_slot);

    _ctx.connman_state = SOL_CONNMAN_STATE_UNKNOWN;
}

SOL_API int
sol_connman_add_service_monitor(
    void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data)
{
    return 0;
}

SOL_API int
sol_connman_del_service_monitor(
    void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data)
{
    return 0;
}

SOL_API int
sol_connman_get_service_vector(struct sol_vector **vector)
{
    return 0;
}
