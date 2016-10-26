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

#define CONNMAN_AGENT_PATH "/net/solettaproject/connman"
#define CONNMAN_AGENT_INTERFACE "net.connman.Agent"

int sol_netctl_init(void);
void sol_netctl_shutdown(void);

struct sol_netctl_service {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_SERVICE_API_VERSION (1)
    uint16_t api_version;
#endif
    sd_bus_slot *slot;
    char *path;
    char *name;
    char *type;
    char *error;
    int32_t strength;
    bool favorite;
    bool immutable;
    bool autoconnect;
    bool roaming;
    enum sol_netctl_service_state state;
    struct sol_ptr_vector security;
    struct sol_ptr_vector nameservers;
    struct sol_ptr_vector timeservers;
    struct sol_ptr_vector domains;
    struct sol_network_link link;
    struct sol_netctl_proxy proxy;
    struct sol_netctl_provider provider;
    struct sol_netctl_ethernet ethernet;
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
    sd_bus_slot *agent_slot;
    sd_bus_slot *vtable_slot;
    sd_bus_slot *scan_slot;
    sd_bus_message *agent_msg;
    struct sol_netctl_service *auth_service;
    const struct sol_netctl_agent *agent;
    const void *agent_data;
    struct sol_ptr_vector agent_vector;
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
    const sd_bus_error *ret_error)
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
    SOL_SET_API_VERSION(service->api_version = SOL_NETCTL_SERVICE_API_VERSION; )
    SOL_SET_API_VERSION(service->link.api_version = SOL_NETWORK_LINK_API_VERSION; )
    SOL_SET_API_VERSION(service->proxy.api_version = SOL_NETCTL_PROXY_API_VERSION; )
    SOL_SET_API_VERSION(service->provider.api_version = SOL_NETCTL_PROVIDER_API_VERSION; )
    SOL_SET_API_VERSION(service->ethernet.api_version = SOL_NETCTL_ETHERNET_API_VERSION; )
    sol_vector_init(&service->link.addrs, sizeof(struct sol_netctl_network_params));
    sol_ptr_vector_init(&service->proxy.servers);
    sol_ptr_vector_init(&service->proxy.excludes);
    sol_ptr_vector_init(&service->security);
    sol_ptr_vector_init(&service->nameservers);
    sol_ptr_vector_init(&service->timeservers);
    sol_ptr_vector_init(&service->domains);
}

static void
_free_connman_service(struct sol_netctl_service *service)
{
    uint16_t i;
    char *data;

    service->slot = sd_bus_slot_unref(service->slot);

    free(service->path);
    free(service->name);
    free(service->type);
    free(service->error);
    free(service->proxy.url);
    free(service->provider.host);
    free(service->provider.domain);
    free(service->provider.name);
    free(service->provider.type);
    free(service->ethernet.method);
    free(service->ethernet.interface);
    free(service->ethernet.address);
    free(service->ethernet.duplex);

    sol_vector_clear(&service->link.addrs);

    SOL_PTR_VECTOR_FOREACH_IDX (&service->proxy.servers, data, i)
        free(data);
    sol_ptr_vector_clear(&service->proxy.servers);

    SOL_PTR_VECTOR_FOREACH_IDX (&service->proxy.excludes, data, i)
        free(data);
    sol_ptr_vector_clear(&service->proxy.excludes);

    SOL_PTR_VECTOR_FOREACH_IDX (&service->security, data, i)
        free(data);
    sol_ptr_vector_clear(&service->security);

    SOL_PTR_VECTOR_FOREACH_IDX (&service->nameservers, data, i)
        free(data);
    sol_ptr_vector_clear(&service->nameservers);

    SOL_PTR_VECTOR_FOREACH_IDX (&service->timeservers, data, i)
        free(data);
    sol_ptr_vector_clear(&service->timeservers);

    SOL_PTR_VECTOR_FOREACH_IDX (&service->domains, data, i)
        free(data);
    sol_ptr_vector_clear(&service->domains);

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

SOL_API const char *
sol_netctl_service_get_error(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return service->error;
}

SOL_API enum sol_netctl_service_state
sol_netctl_service_get_state(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, SOL_NETCTL_SERVICE_STATE_UNKNOWN);

    return service->state;
}

SOL_API const struct sol_ptr_vector *
sol_netctl_service_get_security(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->security;
}

SOL_API bool
sol_netctl_service_is_favorite(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, false);

    return service->favorite;
}

SOL_API bool
sol_netctl_service_is_immutable(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, false);

    return service->immutable;
}

SOL_API bool
sol_netctl_service_is_autoconnect(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, false);

    return service->autoconnect;
}

SOL_API bool
sol_netctl_service_is_roaming(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, false);

    return service->roaming;
}

SOL_API const struct sol_ptr_vector *
sol_netctl_service_get_nameservers(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->nameservers;
}

SOL_API const struct sol_ptr_vector *
sol_netctl_service_get_timeservers(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->timeservers;
}

SOL_API const struct sol_ptr_vector *
sol_netctl_service_get_domains(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->domains;
}

SOL_API const struct sol_netctl_proxy *
sol_netctl_service_get_proxy(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->proxy;
}

SOL_API const struct sol_netctl_provider *
sol_netctl_service_get_provider(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->provider;
}

SOL_API const struct sol_netctl_ethernet *
sol_netctl_service_get_ethernet(const struct sol_netctl_service *service)
{
    SOL_NULL_CHECK(service, NULL);

    return &service->ethernet;
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
    SOL_SET_API_VERSION(network_addr->api_version = SOL_NETCTL_NETWORK_PARAMS_API_VERSION; )

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
    int r, exit;

    SOL_NULL_CHECK(link, -EINVAL);

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK_GOTO(r, < 0, end);
        if (streq(str, "Address")) {
            char *address;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &address);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            get_address_ip(link, address, family);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Netmask")) {
            char *netmask;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &netmask);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            get_netmask(link, netmask, family);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Gateway")) {
            char *gateway;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &gateway);
            SOL_INT_CHECK_GOTO(r, < 0, end);

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
    exit = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(exit, < 0, exit);

    return r;
}

static int
get_proxy_info(sd_bus_message *m, struct sol_netctl_proxy *proxy)
{
    char *str;
    uint16_t i;
    int r, exit;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK_GOTO(r, < 0, end);
        if (streq(str, "Method")) {
            char *method;

            static const struct sol_str_table table[] = {
                SOL_STR_TABLE_ITEM("direct",
                    SOL_NETCTL_PROXY_METHOD_DIRECT),
                SOL_STR_TABLE_ITEM("auto",
                    SOL_NETCTL_PROXY_METHOD_AUTO),
                SOL_STR_TABLE_ITEM("manual",
                    SOL_NETCTL_PROXY_METHOD_MANUAL),
                { }
            };

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &method);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            if (method)
                proxy->method = sol_str_table_lookup_fallback(table,
                    sol_str_slice_from_str(method),
                    SOL_NETCTL_PROXY_METHOD_DIRECT);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "URL")) {
            char *url;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &url);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&proxy->url, url);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Servers")) {
            char *server;

            SOL_PTR_VECTOR_FOREACH_IDX (&proxy->servers, server, i)
                free(server);
            sol_ptr_vector_clear(&proxy->servers);

            r = sd_bus_message_enter_container(m, 'a', "s");
            SOL_INT_CHECK(r, < 0, r);

            while (sd_bus_message_read_basic(m, 's', &server) > 0) {
                r = sol_ptr_vector_append(&proxy->servers, server);
                SOL_INT_CHECK_GOTO(r, < 0, end);
            }

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Excludes")) {
            char *exclude;

            SOL_PTR_VECTOR_FOREACH_IDX (&proxy->excludes, exclude, i)
                free(exclude);
            sol_ptr_vector_clear(&proxy->excludes);

            r = sd_bus_message_enter_container(m, 'a', "s");
            SOL_INT_CHECK(r, < 0, r);

            while (sd_bus_message_read_basic(m, 's', &exclude) > 0) {
                r = sol_ptr_vector_append(&proxy->excludes, exclude);
                SOL_INT_CHECK_GOTO(r, < 0, end);
            }

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            SOL_DBG("Ignored service proxy property: %s", str);
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    exit = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(exit, < 0, exit);

    return r;
}

static int
get_provider_info(sd_bus_message *m, struct sol_netctl_provider *provider)
{
    char *str;
    int r, exit;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK_GOTO(r, < 0, end);
        if (streq(str, "Host")) {
            char *host;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &host);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&provider->host, host);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Domain")) {
            char *domain;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &domain);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&provider->domain, domain);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Name")) {
            char *name;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &name);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&provider->name, name);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Type")) {
            char *type;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &type);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&provider->type, type);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            SOL_DBG("Ignored service provider property: %s", str);
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    exit = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(exit, < 0, exit);

    return r;
}

static int
get_ethernet_info(sd_bus_message *m, struct sol_netctl_ethernet *ethernet)
{
    char *str;
    int r, exit;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK_GOTO(r, < 0, end);
        if (streq(str, "Method")) {
            char *method;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &method);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&ethernet->method, method);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Interface")) {
            char *interface;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &interface);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&ethernet->interface, interface);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Address")) {
            char *address;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &address);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&ethernet->address, address);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "MTU")) {
            uint16_t mtu = 0;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "q");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_UINT16, &mtu);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            ethernet->mtu = mtu;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Speed")) {
            uint16_t speed = 0;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "q");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_UINT16, &speed);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            ethernet->speed = speed;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Duplex")) {
            char *duplex;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &duplex);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&ethernet->duplex, duplex);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            SOL_DBG("Ignored service ethernet property: %s", str);
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK(r, < 0, r);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    exit = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(exit, < 0, exit);

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
    int r, exit;
    uint16_t i;
    struct sol_netctl_service *service;

    service = find_service_by_path(path);
    SOL_NULL_CHECK(service, -ENOMEM);

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        char *str;

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK_GOTO(r, < 0, end);
        if (streq(str, "Name")) {
            char *name;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &name);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&service->name, name);
            SOL_INT_CHECK_GOTO(r, < 0, end);

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
            SOL_INT_CHECK_GOTO(r, < 0, end);

            if (state)
                service->state = sol_str_table_lookup_fallback(table,
                    sol_str_slice_from_str(state),
                    SOL_NETCTL_SERVICE_STATE_UNKNOWN);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Error")) {
            char *error;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &error);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&service->error, error);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Strength")) {
            uint8_t strength = 0;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "y");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_BYTE, &strength);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            service->strength = strength;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Type")) {
            char *type;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &type);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sol_util_replace_str_if_changed(&service->type, type);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        }  else if (streq(str, "Security")) {
            char *security;

            SOL_PTR_VECTOR_FOREACH_IDX (&service->security, security, i)
                free(security);
            sol_ptr_vector_clear(&service->security);

            r = sd_bus_message_enter_container(m, 'a', "s");
            SOL_INT_CHECK(r, < 0, r);

            while (sd_bus_message_read_basic(m, 's', &security) > 0) {
                r = sol_ptr_vector_append(&service->security, security);
                SOL_INT_CHECK_GOTO(r, < 0, end);
            }

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Favorite")) {
            bool favorite;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, 'b', &favorite);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            service->favorite = favorite;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Immutable")) {
            bool immutable;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, 'b', &immutable);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            service->immutable = immutable;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "AutoConnect")) {
            bool autoconnect;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, 'b', &autoconnect);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            service->autoconnect = autoconnect;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Roaming")) {
            bool roaming;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "b");
            SOL_INT_CHECK(r, < 0, r);

            r = sd_bus_message_read_basic(m, 'b', &roaming);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            service->roaming = roaming;

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Nameservers")) {
            char *nameserver;

            SOL_PTR_VECTOR_FOREACH_IDX (&service->nameservers, nameserver, i)
                free(nameserver);
            sol_ptr_vector_clear(&service->nameservers);

            r = sd_bus_message_enter_container(m, 'a', "s");
            SOL_INT_CHECK(r, < 0, r);

            while (sd_bus_message_read_basic(m, 's', &nameserver) > 0) {
                r = sol_ptr_vector_append(&service->nameservers, nameserver);
                SOL_INT_CHECK_GOTO(r, < 0, end);
            }

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Timeservers")) {
            char *timeserver;

            SOL_PTR_VECTOR_FOREACH_IDX (&service->timeservers, timeserver, i)
                free(timeserver);
            sol_ptr_vector_clear(&service->timeservers);

            r = sd_bus_message_enter_container(m, 'a', "s");
            SOL_INT_CHECK(r, < 0, r);

            while (sd_bus_message_read_basic(m, 's', &timeserver) > 0) {
                r = sol_ptr_vector_append(&service->timeservers, timeserver);
                SOL_INT_CHECK_GOTO(r, < 0, end);
            }

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Domains")) {
            char *domain;

            SOL_PTR_VECTOR_FOREACH_IDX (&service->domains, domain, i)
                free(domain);
            sol_ptr_vector_clear(&service->domains);

            r = sd_bus_message_enter_container(m, 'a', "s");
            SOL_INT_CHECK(r, < 0, r);

            while (sd_bus_message_read_basic(m, 's', &domain) > 0) {
                r = sol_ptr_vector_append(&service->domains, domain);
                SOL_INT_CHECK_GOTO(r, < 0, end);
            }

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "IPv4")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            r = get_service_ip(m, &service->link, SOL_NETWORK_FAMILY_INET);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "IPv6")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            r = get_service_ip(m, &service->link, SOL_NETWORK_FAMILY_INET6);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Proxy")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            r = get_proxy_info(m, &service->proxy);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Provider")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            r = get_provider_info(m, &service->provider);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else if (streq(str, "Ethernet")) {
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}");
            SOL_INT_CHECK(r, < 0, r);

            r = get_ethernet_info(m, &service->ethernet);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_exit_container(m);
            SOL_INT_CHECK(r, < 0, r);
        } else {
            SOL_DBG("Ignored service property: %s", str);
            r = sd_bus_message_skip(m, NULL);
            SOL_INT_CHECK(r, < 0, r);
        }
        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK(r, < 0, r);
    } while (1);

end:
    exit = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(exit, < 0, exit);

    call_service_monitor_callback(service);

    return r;
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

static void
_destroy_agent_vector(struct sol_ptr_vector *vector)
{
    uint16_t i;
    char *input;

    if (!vector)
        return;

    SOL_PTR_VECTOR_FOREACH_IDX (vector, input, i) {
        free(input);
    }

    sol_ptr_vector_clear(vector);
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
    _ctx.scan_slot =
        sd_bus_slot_unref(_ctx.scan_slot);

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, id)
        _free_connman_service(service);

    sol_ptr_vector_clear(&_ctx.service_vector);
    sol_monitors_clear(&_ctx.service_ms);
    sol_monitors_clear(&_ctx.manager_ms);
    sol_monitors_clear(&_ctx.error_ms);
    _destroy_agent_vector(&_ctx.agent_vector);

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
    sol_ptr_vector_init(&_ctx.agent_vector);

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
    _ctx.scan_slot =
        sd_bus_slot_unref(_ctx.scan_slot);

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.service_vector, service, id)
        _free_connman_service(service);

    sol_ptr_vector_clear(&_ctx.service_vector);
    sol_monitors_clear(&_ctx.service_ms);
    sol_monitors_clear(&_ctx.manager_ms);
    sol_monitors_clear(&_ctx.error_ms);
    _destroy_agent_vector(&_ctx.agent_vector);

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
    return &_ctx.service_vector;
}

SOL_API enum sol_netctl_service_state
sol_netctl_service_state_from_str(const char *state)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("unknown", SOL_NETCTL_SERVICE_STATE_UNKNOWN),
        SOL_STR_TABLE_ITEM("idle", SOL_NETCTL_SERVICE_STATE_IDLE),
        SOL_STR_TABLE_ITEM("association", SOL_NETCTL_SERVICE_STATE_ASSOCIATION),
        SOL_STR_TABLE_ITEM("configuration", SOL_NETCTL_SERVICE_STATE_CONFIGURATION),
        SOL_STR_TABLE_ITEM("ready", SOL_NETCTL_SERVICE_STATE_READY),
        SOL_STR_TABLE_ITEM("online", SOL_NETCTL_SERVICE_STATE_ONLINE),
        SOL_STR_TABLE_ITEM("disconnect", SOL_NETCTL_SERVICE_STATE_DISCONNECT),
        SOL_STR_TABLE_ITEM("failure", SOL_NETCTL_SERVICE_STATE_FAILURE),
        SOL_STR_TABLE_ITEM("remove", SOL_NETCTL_SERVICE_STATE_REMOVE),
        { }
    };

    SOL_NULL_CHECK(state, SOL_NETCTL_SERVICE_STATE_UNKNOWN);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(state), SOL_NETCTL_SERVICE_STATE_UNKNOWN);
}

SOL_API const char *
sol_netctl_service_state_to_str(enum sol_netctl_service_state state)
{
    static const char *service_states[] = {
        [SOL_NETCTL_SERVICE_STATE_UNKNOWN] = "unknown",
        [SOL_NETCTL_SERVICE_STATE_IDLE] = "idle",
        [SOL_NETCTL_SERVICE_STATE_ASSOCIATION] = "association",
        [SOL_NETCTL_SERVICE_STATE_CONFIGURATION] = "configuration",
        [SOL_NETCTL_SERVICE_STATE_READY] = "ready",
        [SOL_NETCTL_SERVICE_STATE_ONLINE] = "online",
        [SOL_NETCTL_SERVICE_STATE_DISCONNECT] = "disconnect",
        [SOL_NETCTL_SERVICE_STATE_FAILURE] = "failure",
        [SOL_NETCTL_SERVICE_STATE_REMOVE] = "remove",
    };

    if (state < sol_util_array_size(service_states))
        return service_states[state];

    return NULL;
}

static void
_release_agent(struct ctx *pending)
{
    pending->agent_slot = sd_bus_slot_unref(pending->agent_slot);
    pending->vtable_slot = sd_bus_slot_unref(pending->vtable_slot);
    pending->agent = NULL;
    pending->agent_data = NULL;

    _ctx.agent_msg = sd_bus_message_unref(_ctx.agent_msg);
    _ctx.auth_service = NULL;
}

static int
agent_cancel(sd_bus_message *m, void *data, sd_bus_error *ret_error)
{
    struct ctx *context = data;
    const struct sol_netctl_agent *agent = context->agent;

    context->agent_msg = sd_bus_message_unref(context->agent_msg);
    context->auth_service = NULL;

    _destroy_agent_vector(&context->agent_vector);

    if (agent->cancel)
        agent->cancel((void *)context->agent_data);

    return 0;
}

static int
agent_release(sd_bus_message *m, void *data, sd_bus_error *ret_error)
{
    struct ctx *context = data;
    const struct sol_netctl_agent *agent = context->agent;

    _destroy_agent_vector(&context->agent_vector);
    _release_agent(context);

    if (agent->release)
        agent->release((void *)context->agent_data);

    return 0;
}

static int
agent_report_error(sd_bus_message *m, void *data, sd_bus_error *ret_error)
{
    struct sol_netctl_service *service;
    struct ctx *context = data;
    const struct sol_netctl_agent *agent = context->agent;
    const char *path, *err;
    int r;

    if (sol_bus_log_callback(m, data, ret_error) < 0)
        goto error;

    r = sd_bus_message_read(m, "os", &path, &err);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    service = find_service_by_path(path);
    SOL_NULL_CHECK_GOTO(service, error);

    context->agent_msg = sd_bus_message_unref(context->agent_msg);
    context->agent_msg = sd_bus_message_ref(m);
    SOL_NULL_CHECK_GOTO(context->agent_msg, error);

    context->auth_service = service;
    if (agent->report_error)
        agent->report_error((void *)context->agent_data,
            (const struct sol_netctl_service *)service, err);

    return 0;
error:
    sd_bus_reply_method_return(m, NULL);
    return -EINVAL;
}

static int
_agent_input_properties(sd_bus_message *m, struct ctx *context)
{
    const struct sol_netctl_agent *agent = context->agent;
    char *str, *input;
    int r;
    bool is_wps = false;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    _destroy_agent_vector(&_ctx.agent_vector);
    sol_ptr_vector_init(&_ctx.agent_vector);

    do {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        SOL_INT_CHECK_GOTO(r, < 1, end);

        str = NULL;
        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
        SOL_INT_CHECK_GOTO(r, < 0, fail);

        if (str != NULL) {
            if (streq(str, SOL_NETCTL_AGENT_WPS)) {
                _destroy_agent_vector(&_ctx.agent_vector);
                sol_ptr_vector_init(&_ctx.agent_vector);
                is_wps = true;
            }

            input = strdup(str);
            SOL_NULL_CHECK_GOTO(input, fail);
            r = sol_ptr_vector_append(&_ctx.agent_vector, input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        }

        r = sd_bus_message_skip(m, "v");
        SOL_INT_CHECK_GOTO(r, < 0, fail);

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK_GOTO(r, < 0, fail);

        if (is_wps) {
            r = 0;
            break;
        }
    } while (1);

end:
    if (r == 0) {
        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK_GOTO(r, < 0, fail);

        if (agent->request_input)
            agent->request_input((void *)context->agent_data,
                (const struct sol_netctl_service *)context->auth_service,
                (const struct sol_ptr_vector *)&_ctx.agent_vector);
    }

    return r;

fail:
    _destroy_agent_vector(&_ctx.agent_vector);

    if (r < 0)
        return r;

    return -ENOMEM;
}

static int
agent_request_input(sd_bus_message *m, void *data, sd_bus_error *ret_error)
{
    struct sol_netctl_service *service;
    const char *path;
    struct ctx *context = data;
    int r;

    if (sol_bus_log_callback(m, data, ret_error) < 0)
        goto error;

    r = sd_bus_message_read(m, "o", &path);
    SOL_INT_CHECK(r, < 0, r);

    service = find_service_by_path(path);
    SOL_NULL_CHECK_GOTO(service, error);

    context->agent_msg = sd_bus_message_unref(context->agent_msg);
    context->agent_msg = sd_bus_message_ref(m);
    context->auth_service = service;

    return _agent_input_properties(m, context);

error:
    sd_bus_reply_method_return(m, NULL);
    return -EINVAL;
}

static const sd_bus_vtable agent_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", NULL, NULL,
        agent_release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ReportError", "os", NULL,
        agent_report_error, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestInput", "oa{sv}", "a{sv}",
        agent_request_input, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel", NULL, NULL, agent_cancel,
        SD_BUS_VTABLE_UNPRIVILEGED | SD_BUS_VTABLE_METHOD_NO_REPLY),
    SD_BUS_VTABLE_END,
};

static int
_agent_callback(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct ctx *pending = userdata;
    const sd_bus_error *error;

    pending->agent_slot = sd_bus_slot_unref(pending->agent_slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0) {
        error = sd_bus_message_get_error(reply);
        _release_agent(pending);
        _set_error_to_callback(NULL, error);
    }

    return 0;
}

SOL_API int
sol_netctl_request_input(struct sol_netctl_service *service,
    const struct sol_ptr_vector *inputs)
{
    int r;
    uint16_t i;
    sd_bus_message *reply = NULL;
    sol_netctl_agent_input *input;

    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(inputs, -EINVAL);
    SOL_NULL_CHECK(_ctx.agent, -EINVAL);
    SOL_NULL_CHECK(_ctx.agent_msg, -EINVAL);

    if (_ctx.auth_service != service) {
        SOL_WRN("The connection is not the one being authenticated");
        return -EINVAL;
    }

    r = sd_bus_message_new_method_return(_ctx.agent_msg, &reply);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sd_bus_message_open_container(reply, 'a', "{sv}");
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    SOL_PTR_VECTOR_FOREACH_IDX (inputs, input, i) {
        if (!input->input || !input->type)
            continue;
        if (streq(input->type, SOL_NETCTL_AGENT_NAME)) {
            r = sd_bus_message_append(reply, "{sv}", "Name", "s", input->input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        } else if (streq(input->type, SOL_NETCTL_AGENT_PASSPHRASE)) {
            r = sd_bus_message_append(reply, "{sv}", "Passphrase", "s", input->input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        } else if (streq(input->type, SOL_NETCTL_AGENT_IDENTITY)) {
            r = sd_bus_message_append(reply, "{sv}", "Identity", "s", input->input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        } else if (streq(input->type, SOL_NETCTL_AGENT_WPS)) {
            r = sd_bus_message_append(reply, "{sv}", "WPS", "s", input->input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        } else if (streq(input->type, SOL_NETCTL_AGENT_USERNAME)) {
            r = sd_bus_message_append(reply, "{sv}", "Username", "s", input->input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        } else if (streq(input->type, SOL_NETCTL_AGENT_PASSWORD)) {
            r = sd_bus_message_append(reply, "{sv}", "Password", "s", input->input);
            SOL_INT_CHECK_GOTO(r, < 0, fail);
        } else {
            SOL_WRN("The input type is not right");
            break;
        }
    }

    r = sd_bus_message_close_container(reply);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sd_bus_send(NULL, reply, NULL);
    sd_bus_message_unref(reply);

    _ctx.agent_msg = sd_bus_message_unref(_ctx.agent_msg);

    return r;
fail:
    sd_bus_message_unref(reply);

    if (r < 0)
        return r;

    return -EINVAL;
}

SOL_API int
sol_netctl_request_retry(struct sol_netctl_service *service,
    bool retry)
{
    int r = 0;
    sd_bus_message *reply = false;
    const char *interface;

    SOL_NULL_CHECK(service, -EINVAL);
    SOL_NULL_CHECK(_ctx.agent, -EINVAL);
    SOL_NULL_CHECK(_ctx.agent_msg, -EINVAL);

    if (_ctx.auth_service != service) {
        SOL_WRN("The connection is not the one being authenticated");
        return -EINVAL;
    }

    if (retry) {
        interface = sd_bus_message_get_interface(_ctx.agent_msg);
        SOL_NULL_CHECK_GOTO(interface, fail);
        if (strcmp(interface, CONNMAN_AGENT_INTERFACE) == 0)
            r = sd_bus_message_new_method_errorf(_ctx.agent_msg, &reply,
                "net.connman.Agent.Error.Retry", NULL);
        else
            r = sd_bus_message_new_method_errorf(_ctx.agent_msg, &reply,
                "net.connman.vpn.Agent.Error.Retry", NULL);
        SOL_INT_CHECK_GOTO(r, < 0, fail);

        r = sd_bus_send(NULL, reply, NULL);
        sd_bus_message_unref(reply);
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    } else {
        r = sd_bus_reply_method_return(_ctx.agent_msg, NULL);
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    _ctx.agent_msg = sd_bus_message_unref(_ctx.agent_msg);

    return 0;
fail:
    sd_bus_message_unref(reply);

    if (r < 0)
        return r;

    return -EINVAL;
}

SOL_API int
sol_netctl_register_agent(const struct sol_netctl_agent *agent, const void *data)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);
    SOL_NULL_CHECK(agent, -EINVAL);

    if (_ctx.agent && agent)
        return -EEXIST;

    _ctx.agent = agent;
    _ctx.agent_data = data;

    r = sd_bus_add_object_vtable(bus, &_ctx.vtable_slot, CONNMAN_AGENT_PATH,
        "net.connman.Agent", agent_vtable, &_ctx);
    SOL_INT_CHECK(r, < 0, -ENOMEM);

    r = sd_bus_call_method_async(bus, &_ctx.agent_slot,
        "net.connman", "/", "net.connman.Manager", "RegisterAgent",
        _agent_callback, &_ctx, "o", CONNMAN_AGENT_PATH);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return 0;

error:
    _release_agent(&_ctx);

    return 0;
}

SOL_API int
sol_netctl_unregister_agent(void)
{
    int r;
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    if (!_ctx.agent)
        return -ENOENT;

    _release_agent(&_ctx);

    r = sd_bus_call_method_async(bus, NULL,
        "net.connman", "/", "net.connman.Manager", "UnregisterAgent",
        sol_bus_log_callback, NULL, "o", CONNMAN_AGENT_PATH);

    return r;
}

static int
_scan_return(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    const sd_bus_error *error;

    _ctx.scan_slot = sd_bus_slot_unref(_ctx.scan_slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0) {
        error = sd_bus_message_get_error(reply);
        _set_error_to_callback(NULL, error);
    }

    return 0;
}

static int
_scan_services(void)
{
    sd_bus *bus = sol_bus_client_get_bus(_ctx.connman);

    SOL_NULL_CHECK(bus, -EINVAL);

    if (_ctx.scan_slot)
        return -EBUSY;

    return sd_bus_call_method_async(bus, &_ctx.scan_slot, "net.connman",
        "/net/connman/technology/wifi", "net.connman.Technology",
        "Scan", _scan_return, NULL, NULL);
}

SOL_API int
sol_netctl_scan(void)
{
    return _scan_services();
}
