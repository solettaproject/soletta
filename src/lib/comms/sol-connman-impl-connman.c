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

#include "sol-str-table.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
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

struct dbus_monitor_callback {
    void (*cb)(void *data, const struct sol_connman_service *service);
    const void *data;
};

struct ctx {
    struct sol_vector service_vector;
    struct sol_vector monitor_vector;
    struct sol_bus_client *connman;
    sd_bus_slot *properties_changed;
    sd_bus_slot *manager_slot;
    sd_bus_slot *service_slot;
    sd_bus_slot *state_slot;
    enum sol_connman_state connman_state;
};

static struct ctx _ctx;

static void
call_monitor_callback(struct sol_connman_service *service)
{
    struct dbus_monitor_callback *monitor;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&_ctx.monitor_vector, monitor, i) {
        if (monitor->cb)
            monitor->cb((void *)monitor->data, service);
    }
}

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
get_service_ip(sd_bus_message *m, struct sol_network_link_addr *link_addr)
{
    char *str;
    int r;

    SOL_NULL_CHECK(link_addr, -EINVAL);

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK_GOTO(r, < 1, end);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK(r, < 0, r);
        if (strcmp(str, "Address") == 0) {
            char *address;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &address);
            SOL_INT_CHECK(r, < 0, r);

            sol_network_link_addr_from_str(link_addr, address);

            r = sd_bus_message_exit_container(m);
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

static void
get_service_ipv4(sd_bus_message *m, struct sol_connman_service *service)
{
    if (!service)
        return;

    if (!service->link_addr) {
        service->link_addr = malloc(sizeof(*service->link_addr));
        if (!service->link_addr)
            return;
        service->link_addr->family = SOL_NETWORK_FAMILY_INET;
    }
    get_service_ip(m, service->link_addr);
}

static void
get_service_ipv6(sd_bus_message *m, struct sol_connman_service *service)
{
    if (!service)
        return;

    if (!service->link_addr6) {
        service->link_addr6 = malloc(sizeof(*service->link_addr6));
        if (!service->link_addr6)
            return;
        service->link_addr6->family = SOL_NETWORK_FAMILY_INET6;
    }
    get_service_ip(m, service->link_addr6);
}

static void
remove_services(const char *path)
{
    struct sol_connman_service *service;
    uint16_t i;

    if (!path)
        return;

    SOL_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, i) {
        if (strcmp(service->path, path) == 0) {
            service->state = SOL_CONNMAN_SERVICE_STATE_REMOVE;
            call_monitor_callback(service);
            _free_connman_service(service);
            sol_vector_del(&_ctx.service_vector, i);
            break;
        }
    }
}

static int
get_services_properties(sd_bus_message *m, const char *path)
{
    int r;
    struct sol_connman_service *service;
    uint16_t i;
    bool is_exist = false;

    SOL_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, i) {
        if (strcmp(service->path, path) == 0) {
            is_exist = true;
            break;
        }
    }

    if (is_exist == false) {
        service = sol_vector_append(&_ctx.service_vector);
        SOL_NULL_CHECK(service, -EINVAL);

        service->path = strdup(path);
        SOL_NULL_CHECK(service->path, -ENOMEM);
    }

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        char *str;

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK_GOTO(r, < 1, end);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK(r, < 0, r);
        if (strcmp(str, "Name") == 0) {
            char *name;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &name);
            SOL_INT_CHECK(r, < 0, r);

            if (service->name)
                free(service->name);
            service->name = strdup(name);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
            SOL_NULL_CHECK(service->name, -ENOMEM);
        } else if (strcmp(str, "State") == 0) {
            char *state;
            static const struct sol_str_table table[] = {
                SOL_STR_TABLE_ITEM("online",
                    SOL_CONNMAN_SERVICE_STATE_ONLINE),
                SOL_STR_TABLE_ITEM("ready",
                    SOL_CONNMAN_SERVICE_STATE_READY),
                SOL_STR_TABLE_ITEM("association",
                    SOL_CONNMAN_SERVICE_STATE_ASSOCIATION),
                SOL_STR_TABLE_ITEM("configuration",
                    SOL_CONNMAN_SERVICE_STATE_CONFIGURATION),
                SOL_STR_TABLE_ITEM("disconnect",
                    SOL_CONNMAN_SERVICE_STATE_DISCONNECT),
                SOL_STR_TABLE_ITEM("idle",
                    SOL_CONNMAN_SERVICE_STATE_IDLE),
                SOL_STR_TABLE_ITEM("failure",
                    SOL_CONNMAN_SERVICE_STATE_FAILURE),
                { }
            };

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &state);
            SOL_INT_CHECK(r, < 0, r);

            if (state)
                service->state = sol_str_table_lookup_fallback(table,
                    sol_str_slice_from_str(state),
                    SOL_CONNMAN_SERVICE_STATE_UNKNOWN);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (strcmp(str, "Strength") == 0) {
            uint8_t strength = 0;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "y");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_BYTE, &strength);
            SOL_INT_CHECK(r, < 0, r);

            service->strength = strength;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (strcmp(str, "Type") == 0) {
            char *type;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &type);
            SOL_INT_CHECK(r, < 0, r);

            if (service->type)
                free(service->type);
            service->type = strdup(type);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
            SOL_NULL_CHECK(service->type, -ENOMEM);
        } else if (strcmp(str, "IPv4") == 0) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            get_service_ipv4(m, service);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (strcmp(str, "IPv6") == 0) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            get_service_ipv6(m, service);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        }  else {
            r = sd_bus_message_skip(m, NULL);
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    if (r == 0) {
        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } else
        return r;

    call_monitor_callback(service);

    return 0;
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
services_list_changed(sd_bus_message *m)
{
    int r = 0;
    char *path = NULL;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "(oa{sv})");
    SOL_INT_CHECK(r, < 0, r);

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "oa{sv}") > 0) {
        r = sd_bus_message_read(m, "o", &path);
        SOL_INT_CHECK(r, < 0, r);

        r = get_services_properties(m, path);
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    }

    r = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "o");
    SOL_INT_CHECK(r, < 0, r);

    while (sd_bus_message_read(m, "o", &path) > 0) {
        remove_services(path);
    }

    r = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
_services_properties_changed(sd_bus_message *m, void *userdata,
    sd_bus_error *ret_error)
{
    struct ctx *pending = userdata;

    pending->service_slot = sd_bus_slot_unref(pending->service_slot);

    if (sol_bus_log_callback(m, userdata, ret_error) < 0)
        return -EINVAL;

    return services_list_changed(m);
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

static int
dbus_connection_get_service_properties(void)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    r = sd_bus_call_method_async(bus, &_ctx.service_slot,
        "net.connman", "/", "net.connman.Manager", "GetServices",
        _services_properties_changed, &_ctx, NULL);
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

static int
_service_connect(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct sol_connman_service *service = userdata;

    SOL_NULL_CHECK(service, -EINVAL);

    service->slot = sd_bus_slot_unref(service->slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0)
        service->is_call_success = false;
    else
        service->is_call_success = true;

    call_monitor_callback(service);

    return 0;
}

SOL_API int
sol_connman_service_connect(struct sol_connman_service *service)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);
    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(service->path, -EINVAL);

    service->is_call_success = false;

    r = sd_bus_call_method_async(bus, &service->slot, "net.connman", service->path,
        "net.connman.Service", "Connect", _service_connect, service, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
_service_disconnect(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct sol_connman_service *service = userdata;

    SOL_NULL_CHECK(service, -EINVAL);

    service->slot = sd_bus_slot_unref(service->slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0)
        service->is_call_success = false;
    else
        service->is_call_success = true;

    call_monitor_callback(service);

    return 0;
}

SOL_API int
sol_connman_service_disconnect(struct sol_connman_service *service)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);
    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(service->path, -EINVAL);

    if (service->slot)
        service->slot = sd_bus_slot_unref(service->slot);

    service->is_call_success = false;

    r = sd_bus_call_method_async(bus, &service->slot, "net.connman", service->path,
        "net.connman.Service", "Disconnect", _service_disconnect, service, NULL);
    SOL_INT_CHECK(r, < 0, r);

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
    struct sol_connman_service *service;
    uint16_t id;

    if (_ctx.connman) {
        sol_bus_client_free(_ctx.connman);
        _ctx.connman = NULL;
    }

    _ctx.state_slot =
        sd_bus_slot_unref(_ctx.state_slot);
    _ctx.manager_slot =
        sd_bus_slot_unref(_ctx.manager_slot);
    _ctx.service_slot =
        sd_bus_slot_unref(_ctx.service_slot);

    SOL_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, id) {
        _free_connman_service(service);
    }

    sol_vector_clear(&_ctx.service_vector);
    sol_vector_clear(&_ctx.monitor_vector);

    _ctx.connman_state = SOL_CONNMAN_STATE_UNKNOWN;
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

    sol_vector_init(&_ctx.service_vector, sizeof(struct sol_connman_service));
    sol_vector_init(&_ctx.monitor_vector, sizeof(struct dbus_monitor_callback));

    return 0;
}

static void
sol_connman_shutdown_lazy(void)
{
    struct sol_connman_service *service;
    uint16_t id;

    if (_ctx.connman) {
        sol_bus_client_free(_ctx.connman);
        _ctx.connman = NULL;
    }

    _ctx.state_slot =
        sd_bus_slot_unref(_ctx.state_slot);
    _ctx.manager_slot =
        sd_bus_slot_unref(_ctx.manager_slot);
    _ctx.service_slot =
        sd_bus_slot_unref(_ctx.service_slot);

    SOL_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, id) {
        _free_connman_service(service);
    }

    sol_vector_clear(&_ctx.service_vector);
    sol_vector_clear(&_ctx.monitor_vector);

    _ctx.connman_state = SOL_CONNMAN_STATE_UNKNOWN;
}

static int
_match_properties_changed(sd_bus_message *m, void *userdata,
    sd_bus_error *ret_error)
{
    const char *interface;
    int r;

    SOL_NULL_CHECK(_ctx.properties_changed, -EINVAL);

    interface = sd_bus_message_get_interface(m);
    SOL_NULL_CHECK(interface, -EINVAL);

    if (strncmp(interface, "net.connman.", 12) != 0)
        return -EINVAL;

    if (sd_bus_message_is_signal(m, "net.connman.Manager",
        "ServicesChanged")) {
        if (_ctx.service_slot)
            return -EINVAL;

        services_list_changed(m);
    } else if (sd_bus_message_is_signal(m, "net.connman.Manager",
        "PropertyChanged")) {
        char *str = NULL;

        if (_ctx.manager_slot)
            return -EINVAL;

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK(r, < 0, r);
        if (strcmp(str, "State") == 0) {
            get_manager_properties(m);
        } else {
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }
    }

    return 0;
}

static int
dbus_connection_add_monitor(
    void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data)
{
    int r;
    uint16_t i;
    struct dbus_monitor_callback *monitor;
    bool is_exist = false;
    char matchstr[256];
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&_ctx.monitor_vector, monitor, i) {
        if (monitor->cb == cb) {
            monitor->data = data;
            is_exist = true;
            break;
        }
    }

    if (is_exist == false) {
        monitor = sol_vector_append(&_ctx.monitor_vector);
        SOL_NULL_CHECK(monitor, -EINVAL);

        monitor->cb = cb;
        monitor->data = data;
    }

    if (_ctx.properties_changed)
        return 0;

    r = snprintf(matchstr, sizeof(matchstr),
        "type='signal',"
        "interface='net.connman.Manager'");
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_add_match(bus, &_ctx.properties_changed, matchstr,
        _match_properties_changed, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static void
dbus_connection_del_monitor(
    void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data)
{

}

SOL_API int
sol_connman_add_service_monitor(
    void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data)
{
    int r;

    r = sol_connman_init_lazy();
    SOL_INT_CHECK_GOTO(r, < 0, fail_init);

    r = dbus_connection_add_monitor(cb, data);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = dbus_connection_get_manager_properties();
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = dbus_connection_get_service_properties();
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    return 0;

fail:
    dbus_connection_del_monitor(cb, data);

fail_init:
    sol_connman_shutdown_lazy();

    return -EINVAL;
}

SOL_API int
sol_connman_del_service_monitor(
    void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data)
{
    SOL_NULL_CHECK(_ctx.connman, -EINVAL);

    dbus_connection_del_monitor(cb, data);
    sol_connman_shutdown_lazy();

    return 0;
}

SOL_API int
sol_connman_get_service_vector(struct sol_vector **vector)
{
    SOL_NULL_CHECK(vector, -EINVAL);

    *vector = &_ctx.service_vector;

    return 0;
}
