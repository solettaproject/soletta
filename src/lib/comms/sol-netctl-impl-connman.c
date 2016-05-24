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

#include "sol-str-table.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-monitors.h"
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

struct ctx {
    struct sol_ptr_vector service_vector;
    struct sol_monitors service_ms;
    struct sol_monitors manager_ms;
    struct sol_monitors error_ms;
    struct sol_bus_client *connman;
    sd_bus_slot *properties_changed;
    sd_bus_slot *manager_slot;
    sd_bus_slot *service_slot;
    sd_bus_slot *state_slot;
    enum sol_netctl_state connman_state;
    int32_t refcount;
};

static struct ctx _ctx;

static void
call_service_monitor_callback(struct sol_netctl_service *service)
{
    struct sol_monitors_entry *m;
    uint16_t i;

    SOL_MONITORS_WALK (&_ctx.service_ms, m, i)
        ((sol_netctl_service_monitor_cb)m->cb)((void *)m->data, service);
}

static void
call_manager_monitor_callback(void)
{
    struct sol_monitors_entry *m;
    uint16_t i;

    SOL_MONITORS_WALK (&_ctx.manager_ms, m, i)
        ((sol_netctl_manager_monitor_cb)m->cb)((void *)m->data);
}

static void
call_error_monitor_callback(struct sol_netctl_service *service,
    unsigned int error)
{
    struct sol_monitors_entry *m;
    uint16_t i;

    SOL_MONITORS_WALK (&_ctx.error_ms, m, i)
        ((sol_netctl_error_monitor_cb)m->cb)((void *)m->data,
            service, error);
}

static void
_set_error_to_callback(struct sol_netctl_service *service,
    sd_bus_error *ret_error)
{
    unsigned int error;
    static const struct sol_str_table err_table[] = {
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.NoMemory",
            ENOMEM),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.AccessDenied",
            EPERM),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.InvalidArgs",
            EINVAL),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.UnixProcessIdUnknown",
            ESRCH),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.FileNotFound",
            ENOENT),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.FileExists",
            EEXIST),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.Timeout",
            ETIMEDOUT),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.IOError",
            EIO),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.Disconnected",
            ECONNRESET),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.NotSupported",
            ENOTSUP),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.BadAddress",
            EFAULT),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.LimitsExceeded",
            ENOBUFS),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.AddressInUse",
            EADDRINUSE),
        SOL_STR_TABLE_ITEM("org.freedesktop.DBus.Error.InconsistentMessage",
            EBADMSG),
        { }
    };

    if (ret_error) {
        error = sol_str_table_lookup_fallback(err_table,
            sol_str_slice_from_str(ret_error->name), EINVAL);
        call_error_monitor_callback(service, error);
    }
}

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

static struct sol_netctl_network_params *
get_network_link(
    struct sol_network_link *link, enum sol_network_family family)
{
    struct sol_netctl_network_params *network_addr;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (&link->addrs, network_addr, idx) {
        if (network_addr->addr.family == family)
            return network_addr;
    }

    network_addr = sol_vector_append(&link->addrs);
    SOL_NULL_CHECK(network_addr, NULL);

    return network_addr;
}

static void
get_address_ip(struct sol_network_link *link,
    char *address, enum sol_network_family family)
{
    struct sol_netctl_network_params *params;

    params = get_network_link(link, family);
    SOL_NULL_CHECK(params);

    params->addr.family = family;
    sol_network_link_addr_from_str(&params->addr, address);
    link->flags = SOL_NETWORK_LINK_UP;
}

static void
get_netmask(struct sol_network_link *link,
    char *netmask, enum sol_network_family family)
{
    struct sol_netctl_network_params *params;

    params = get_network_link(link, family);
    SOL_NULL_CHECK(params);

    params->netmask.family = family;
    sol_network_link_addr_from_str(&params->netmask, netmask);
}

static void
get_gateway(struct sol_network_link *link,
    char *gateway, enum sol_network_family family)
{
    struct sol_netctl_network_params *params;

    params = get_network_link(link, family);
    SOL_NULL_CHECK(params);

    params->gateway.family = family;
    sol_network_link_addr_from_str(&params->gateway, gateway);
}

static int
get_service_ip(sd_bus_message *m,
    struct sol_network_link *link, enum sol_network_family family)
{
    char *str;
    int r;

    SOL_NULL_CHECK(link, -EINVAL);

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK_GOTO(r, < 1, end);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK(r, < 0, r);
        if (streq(str, "Address")) {
            char *address;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &address);
            SOL_INT_CHECK(r, < 0, r);

            get_address_ip(link, address, family);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Netmask")) {
            char *netmask;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &netmask);
            SOL_INT_CHECK(r, < 0, r);

            get_netmask(link, netmask, family);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Gateway")) {
            char *gateway;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &gateway);
            SOL_INT_CHECK(r, < 0, r);

            get_gateway(link, gateway, family);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            SOL_DBG("Ignored service ip property: %s", str);
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
remove_services(const char *path)
{
    struct sol_netctl_service *service;
    uint16_t i;

    if (!path)
        return;

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, i) {
        if (streq(service->path, path)) {
            service->state = SOL_NETCTL_SERVICE_STATE_REMOVE;
            call_service_monitor_callback(service);
            sol_ptr_vector_del(&_ctx.service_vector, i);
            _free_connman_service(service);
            break;
        }
    }
}

static struct sol_netctl_service *
find_service_by_path(const char *path)
{
    struct sol_netctl_service *service;
    uint16_t i;
    bool is_exist = false;
    int r;

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, i) {
        if (streq(service->path, path)) {
            is_exist = true;
            break;
        }
    }

    if (is_exist == false) {
        service = calloc(1, sizeof(struct sol_netctl_service));
        SOL_NULL_CHECK(service, NULL);
        _init_connman_service(service);

        r = sol_ptr_vector_append(&_ctx.service_vector, service);
        SOL_INT_CHECK_GOTO(r, < 0, fail_append);

        service->path = strdup(path);
        SOL_NULL_CHECK_GOTO(service->path, fail);
    }

    return service;

fail:
    sol_ptr_vector_del_last(&_ctx.service_vector);
fail_append:
    _free_connman_service(service);
    return NULL;
}

static int
get_services_properties(sd_bus_message *m, const char *path)
{
    int r;
    struct sol_netctl_service *service;

    service = find_service_by_path(path);
    SOL_NULL_CHECK(service, -ENOMEM);

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        char *str;

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK_GOTO(r, < 1, end);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK(r, < 0, r);
        if (streq(str, "Name")) {
            char *name;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &name);
            SOL_INT_CHECK(r, < 0, r);

            r = sol_util_replace_str_if_changed(&service->name, name);
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "State")) {
            char *state;
            static const struct sol_str_table table[] = {
                SOL_STR_TABLE_ITEM("online",
                    SOL_NETCTL_SERVICE_STATE_ONLINE),
                SOL_STR_TABLE_ITEM("ready",
                    SOL_NETCTL_SERVICE_STATE_READY),
                SOL_STR_TABLE_ITEM("association",
                    SOL_NETCTL_SERVICE_STATE_ASSOCIATION),
                SOL_STR_TABLE_ITEM("configuration",
                    SOL_NETCTL_SERVICE_STATE_CONFIGURATION),
                SOL_STR_TABLE_ITEM("disconnect",
                    SOL_NETCTL_SERVICE_STATE_DISCONNECT),
                SOL_STR_TABLE_ITEM("idle",
                    SOL_NETCTL_SERVICE_STATE_IDLE),
                SOL_STR_TABLE_ITEM("failure",
                    SOL_NETCTL_SERVICE_STATE_FAILURE),
                { }
            };

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &state);
            SOL_INT_CHECK(r, < 0, r);

            if (state)
                service->state = sol_str_table_lookup_fallback(table,
                    sol_str_slice_from_str(state),
                    SOL_NETCTL_SERVICE_STATE_UNKNOWN);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Strength")) {
            uint8_t strength = 0;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "y");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_BYTE, &strength);
            SOL_INT_CHECK(r, < 0, r);

            service->strength = strength;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Type")) {
            char *type;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &type);
            SOL_INT_CHECK(r, < 0, r);

            r = sol_util_replace_str_if_changed(&service->type, type);
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "IPv4")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            get_service_ip(m, &service->link, SOL_NETWORK_FAMILY_INET);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "IPv6")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            get_service_ip(m, &service->link, SOL_NETWORK_FAMILY_INET6);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        }  else {
            SOL_DBG("Ignored service property: %s", str);
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

    call_service_monitor_callback(service);

    return 0;
}

static int
get_manager_properties(sd_bus_message *m)
{
    char *state;
    int r;
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("online",
            SOL_NETCTL_STATE_ONLINE),
        SOL_STR_TABLE_ITEM("ready",
            SOL_NETCTL_STATE_READY),
        SOL_STR_TABLE_ITEM("idle",
            SOL_NETCTL_STATE_IDLE),
        SOL_STR_TABLE_ITEM("offline",
            SOL_NETCTL_STATE_OFFLINE),
        { }
    };

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &state);
    SOL_INT_CHECK(r, < 0, r);

    if (state)
        _ctx.connman_state = sol_str_table_lookup_fallback(table,
            sol_str_slice_from_str(state),
            SOL_NETCTL_STATE_UNKNOWN);

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

        if (streq(str, "State")) {
            r = get_manager_properties(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            SOL_DBG("Ignored global manager property: %s", str);
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    if (r == 0)
        r = sd_bus_message_exit_container(m);
    else
        return r;

    call_manager_monitor_callback();

    return r;
}

static int
dbus_connection_get_manager_properties(void)
{
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    return sd_bus_call_method_async(bus, &_ctx.manager_slot,
        "net.connman", "/", "net.connman.Manager", "GetProperties",
        _manager_properties_changed, &_ctx, NULL);
}

static int
dbus_connection_get_service_properties(void)
{
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    return sd_bus_call_method_async(bus, &_ctx.service_slot,
        "net.connman", "/", "net.connman.Manager", "GetServices",
        _services_properties_changed, &_ctx, NULL);
}

SOL_API enum sol_netctl_state
sol_netctl_get_state(void)
{
    return _ctx.connman_state;
}

static int
_set_state_property_changed(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct ctx *pending = userdata;
    const sd_bus_error *error;

    pending->state_slot = sd_bus_slot_unref(pending->state_slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0) {
        error = sd_bus_message_get_error(reply);
        _set_error_to_callback(NULL, error);
    }

    return 0;
}

SOL_API int
sol_netctl_set_radios_offline(bool enabled)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    if (_ctx.state_slot)
        return -EBUSY;

    r = sd_bus_call_method_async(bus, &_ctx.state_slot, "net.connman", "/",
        "net.connman.Manager", "SetProperty", _set_state_property_changed,
        &_ctx, "sv", "OfflineMode", "b", enabled);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API bool
sol_netctl_get_radios_offline(void)
{
    if (_ctx.connman_state != SOL_NETCTL_STATE_OFFLINE)
        return false;

    return true;
}

static int
_service_connect(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct sol_netctl_service *service = userdata;
    sd_bus_error *error;

    SOL_NULL_CHECK(service, -EINVAL);

    service->slot = sd_bus_slot_unref(service->slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0) {
        error = sd_bus_message_get_error(reply);
        _set_error_to_callback(service, error);
    }

    return 0;
}

SOL_API int
sol_netctl_service_connect(struct sol_netctl_service *service)
{
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);
    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(service->path, -EINVAL);

    if (service->slot)
        return -EBUSY;

    return sd_bus_call_method_async(bus, &service->slot, "net.connman", service->path,
        "net.connman.Service", "Connect", _service_connect, service, NULL);
}

static int
_service_disconnect(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct sol_netctl_service *service = userdata;
    const sd_bus_error *error;

    SOL_NULL_CHECK(service, -EINVAL);

    service->slot = sd_bus_slot_unref(service->slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0) {
        error = sd_bus_message_get_error(reply);
        _set_error_to_callback(service, error);
    }

    return 0;
}

SOL_API int
sol_netctl_service_disconnect(struct sol_netctl_service *service)
{
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);
    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(service->path, -EINVAL);

    if (service->slot)
        service->slot = sd_bus_slot_unref(service->slot);

    return sd_bus_call_method_async(bus, &service->slot, "net.connman", service->path,
        "net.connman.Service", "Disconnect", _service_disconnect, service, NULL);
}

int
sol_netctl_init(void)
{
    return 0;
}

void
sol_netctl_shutdown(void)
{
    struct sol_netctl_service *service;
    uint16_t id;

    _ctx.refcount = 0;

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

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, id)
        _free_connman_service(service);

    sol_ptr_vector_clear(&_ctx.service_vector);
    sol_monitors_clear(&_ctx.service_ms);
    sol_monitors_clear(&_ctx.manager_ms);
    sol_monitors_clear(&_ctx.error_ms);

    _ctx.connman_state = SOL_NETCTL_STATE_UNKNOWN;
}

static int
sol_netctl_init_lazy(void)
{
    sd_bus *bus;

    _ctx.refcount++;

    if (_ctx.connman)
        return 0;

    bus = sol_bus_get(NULL);
    if (!bus) {
        SOL_WRN("Unable to get sd bus");
        return -EINVAL;
    }

    _ctx.connman = sol_bus_client_new(bus, "org.connman");
    if (!_ctx.connman) {
        sd_bus_unref(bus);
        SOL_WRN("Unable to new a bus client");
        return -EINVAL;
    }

    sol_ptr_vector_init(&_ctx.service_vector);
    sol_monitors_init(&_ctx.service_ms, NULL);
    sol_monitors_init(&_ctx.manager_ms, NULL);
    sol_monitors_init(&_ctx.error_ms, NULL);

    return 0;
}

static void
sol_netctl_shutdown_lazy(void)
{
    struct sol_netctl_service *service;
    uint16_t id;

    _ctx.refcount--;

    if (_ctx.refcount)
        return;

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

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, id)
        _free_connman_service(service);

    sol_ptr_vector_clear(&_ctx.service_vector);
    sol_monitors_clear(&_ctx.service_ms);
    sol_monitors_clear(&_ctx.manager_ms);
    sol_monitors_clear(&_ctx.error_ms);

    _ctx.connman_state = SOL_NETCTL_STATE_UNKNOWN;
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

    if (!strstartswith(interface, "net.connman."))
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
        if (streq(str, "State")) {
            r = get_manager_properties(m);
            SOL_INT_CHECK(r, < 0, r);
            call_manager_monitor_callback();
        } else {
            SOL_DBG("Ignored changed property: %s", str);
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }
    }

    return 0;
}

static int
dbus_service_add_monitor(
    sol_netctl_service_monitor_cb cb, const void *data)
{
    int r;
    struct sol_monitors_entry *e;
    char matchstr[] = "type='signal',interface='net.connman.Manager'";
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    e = sol_monitors_append(&_ctx.service_ms, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);

    if (_ctx.properties_changed)
        goto end;

    r = sd_bus_add_match(bus, &_ctx.properties_changed, matchstr,
        _match_properties_changed, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
end:
    if (sol_monitors_count(&_ctx.service_ms) == 1)
        return 0;
    else
        return 1;
}

static int
dbus_service_del_monitor(
    sol_netctl_service_monitor_cb cb, const void *data)
{
    int r;

    r = sol_monitors_find(&_ctx.service_ms, (sol_monitors_cb_t)cb, data);
    SOL_INT_CHECK(r, < 0, r);

    return sol_monitors_del(&_ctx.service_ms, r);
}

SOL_API int
sol_netctl_add_service_monitor(
    sol_netctl_service_monitor_cb cb, const void *data)
{
    int r;

    r = sol_netctl_init_lazy();
    SOL_INT_CHECK_GOTO(r, < 0, fail_init);

    r = dbus_service_add_monitor(cb, data);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    if (!r) {
        r = dbus_connection_get_service_properties();
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    return 0;

fail:
    dbus_service_del_monitor(cb, data);

fail_init:
    sol_netctl_shutdown_lazy();

    return r;
}

SOL_API int
sol_netctl_del_service_monitor(
    sol_netctl_service_monitor_cb cb, const void *data)
{
    SOL_NULL_CHECK(_ctx.connman, -EINVAL);

    dbus_service_del_monitor(cb, data);
    sol_netctl_shutdown_lazy();

    return 0;
}

static int
dbus_manager_add_monitor(
    sol_netctl_manager_monitor_cb cb, const void *data)
{
    int r;
    struct sol_monitors_entry *e;
    char matchstr[] = "type='signal',interface='net.connman.Manager'";
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    e = sol_monitors_append(&_ctx.manager_ms, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);

    if (_ctx.properties_changed)
        goto end;

    r = sd_bus_add_match(bus, &_ctx.properties_changed, matchstr,
        _match_properties_changed, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
end:
    if (sol_monitors_count(&_ctx.manager_ms) == 1)
        return 0;
    else
        return 1;
}

static int
dbus_manager_del_monitor(
    sol_netctl_manager_monitor_cb cb, const void *data)
{
    int r;

    r = sol_monitors_find(&_ctx.manager_ms, (sol_monitors_cb_t)cb, data);
    SOL_INT_CHECK(r, < 0, r);

    return sol_monitors_del(&_ctx.manager_ms, r);
}

SOL_API int
sol_netctl_add_manager_monitor(
    sol_netctl_manager_monitor_cb cb, const void *data)
{
    int r;

    r = sol_netctl_init_lazy();
    SOL_INT_CHECK_GOTO(r, < 0, fail_init);

    r = dbus_manager_add_monitor(cb, data);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    if (!r) {
        r = dbus_connection_get_manager_properties();
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    return 0;

fail:
    dbus_manager_del_monitor(cb, data);

fail_init:
    sol_netctl_shutdown_lazy();

    return r;
}

SOL_API int
sol_netctl_del_manager_monitor(
    sol_netctl_manager_monitor_cb cb, const void *data)
{
    SOL_NULL_CHECK(_ctx.connman, -EINVAL);

    dbus_manager_del_monitor(cb, data);
    sol_netctl_shutdown_lazy();

    return 0;
}

static int
dbus_error_add_monitor(
    sol_netctl_error_monitor_cb cb, const void *data)
{
    struct sol_monitors_entry *e;

    e = sol_monitors_append(&_ctx.error_ms, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);

    return 0;
}

static int
dbus_error_del_monitor(
    sol_netctl_error_monitor_cb cb, const void *data)
{
    int r;

    r = sol_monitors_find(&_ctx.error_ms, (sol_monitors_cb_t)cb, data);
    SOL_INT_CHECK(r, < 0, r);

    return sol_monitors_del(&_ctx.error_ms, r);
}

SOL_API int
sol_netctl_add_error_monitor(
    sol_netctl_error_monitor_cb cb, const void *data)
{
    int r;

    r = sol_netctl_init_lazy();
    SOL_INT_CHECK_GOTO(r, < 0, fail_init);

    r = dbus_error_add_monitor(cb, data);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    return 0;

fail:
    dbus_error_del_monitor(cb, data);

fail_init:
    sol_netctl_shutdown_lazy();

    return r;
}

SOL_API int
sol_netctl_del_error_monitor(
    sol_netctl_error_monitor_cb cb, const void *data)
{
    SOL_NULL_CHECK(_ctx.connman, -EINVAL);

    dbus_error_del_monitor(cb, data);
    sol_netctl_shutdown_lazy();

    return 0;
}

SOL_API const struct sol_ptr_vector *
sol_netctl_get_services(void)
{
    return NULL;
}
