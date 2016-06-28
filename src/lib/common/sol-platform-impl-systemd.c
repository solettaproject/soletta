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
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <systemd/sd-bus.h>
#include <time.h>

#include "sol-platform-impl.h"

#include "sol-bus.h"
#include "sol-platform.h"
#include "sol-str-table.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

struct service {
    struct {
        enum sol_platform_service_state state;
    } properties;

    sd_bus_slot *slot;
    /* owned by sol-platform */
    const char *service;
};

struct ctx {
    struct {
        enum sol_platform_state system_state;
    } properties;

    struct sol_bus_client *systemd;
    struct sol_bus_client *locale;
    struct sol_bus_client *timedate;
    struct sol_bus_client *hostname;
    struct sol_ptr_vector services;
    bool locale_monitor_registered : 1;
    bool timedate_monitor_registered : 1;
    bool hostname_monitor_registered : 1;
};

static struct ctx _ctx;

static bool
_manager_set_system_state(void *data, const char *path, sd_bus_message *m)
{
    struct ctx *ctx = data;
    const char *str;
    bool changed;
    enum sol_platform_state state;
    static const struct sol_str_table table[] = {
        /* systemd differentiates these 2, we don't */
        SOL_STR_TABLE_ITEM("initializing", SOL_PLATFORM_STATE_INITIALIZING),
        SOL_STR_TABLE_ITEM("starting",     SOL_PLATFORM_STATE_INITIALIZING),

        SOL_STR_TABLE_ITEM("running",      SOL_PLATFORM_STATE_RUNNING),
        SOL_STR_TABLE_ITEM("degraded",     SOL_PLATFORM_STATE_DEGRADED),
        SOL_STR_TABLE_ITEM("maintenance",  SOL_PLATFORM_STATE_MAINTENANCE),
        SOL_STR_TABLE_ITEM("stopping",     SOL_PLATFORM_STATE_STOPPING),
        { }
    };
    int r;

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
    SOL_INT_CHECK(r, < 0, false);

    state = sol_str_table_lookup_fallback(table, sol_str_slice_from_str(str),
        SOL_PLATFORM_SERVICE_STATE_UNKNOWN);
    changed = state != ctx->properties.system_state;
    ctx->properties.system_state = state;

    return changed;
}

enum {
    MANAGER_PROPERTY_SYSTEM_STATE,
};

static void
_manager_properties_changed(void *data, const char *path, uint64_t mask)
{
    struct ctx *ctx = data;

    SOL_DBG("mask=%" PRIu64, mask);

    if (mask & (1 << MANAGER_PROPERTY_SYSTEM_STATE)) {
        SOL_DBG("New system state: [%d]", ctx->properties.system_state);
        sol_platform_inform_state_monitors(ctx->properties.system_state);
    }
}

static const struct sol_bus_properties _manager_properties[] = {
    [MANAGER_PROPERTY_SYSTEM_STATE] = {
        .member = "SystemState",
        .set = _manager_set_system_state,
    },
    { }
};

static int
_systemd_bus_initialized(sd_bus *bus)
{
    sd_bus_message *m = NULL;
    int r = -ENOMEM;

    _ctx.properties.system_state = SOL_PLATFORM_STATE_UNKNOWN;

    _ctx.systemd = sol_bus_client_new(bus, "org.freedesktop.systemd1");
    SOL_NULL_CHECK_GOTO(_ctx.systemd, fail);

    r = sd_bus_message_new_method_call(bus, &m,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "Subscribe");
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sd_bus_call_async(bus, NULL, m, sol_bus_log_callback, NULL, 0);
    sd_bus_message_unref(m);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sol_bus_map_cached_properties(_ctx.systemd,
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        _manager_properties,
        _manager_properties_changed,
        &_ctx);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    return 0;

fail:
    return r;
}

static const char *
sanitize_unit_name(char buf[SOL_STATIC_ARRAY_SIZE(PATH_MAX)], const char *unit,
    const char *suffix, const char *action)
{
    size_t len;
    size_t suffixlen;

    SOL_NULL_CHECK(unit, NULL);
    SOL_NULL_CHECK(suffix, NULL);

    len = strlen(unit);
    suffixlen = strlen(suffix);
    SOL_INT_CHECK(len + suffixlen, >= PATH_MAX, NULL);
    if (!len || !suffixlen) {
        SOL_WRN("Invalid unit name '%s%s' to %s", unit, suffix, action);
        return NULL;
    }

    memcpy(buf, unit, len);
    memcpy(buf + len, suffix, suffixlen);
    buf[len + suffixlen] = '\0';

    return buf;
}

static inline const char *
sanitize_service_name(char buf[SOL_STATIC_ARRAY_SIZE(PATH_MAX)], const char *service,
    const char *action)
{
    return sanitize_unit_name(buf, service, ".service", action);
}

static sd_bus *
_get_sd_bus(struct sol_bus_client *client, int (*init_cb)(sd_bus *bus))
{
    sd_bus *bus;

    if (!client)
        bus = sol_bus_get(init_cb);
    else
        bus = sol_bus_client_get_bus(client);

    return bus;
}

int
sol_platform_impl_get_state(void)
{
    sd_bus *bus;

    bus = _get_sd_bus(_ctx.systemd, _systemd_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);
    return _ctx.properties.system_state;
}

static bool
_service_set_state(void *data, const char *path, sd_bus_message *m)
{
    struct service *x = data;
    const char *str;
    bool changed;
    enum sol_platform_service_state state;
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("active",       SOL_PLATFORM_SERVICE_STATE_ACTIVE),
        SOL_STR_TABLE_ITEM("reloading",    SOL_PLATFORM_SERVICE_STATE_RELOADING),
        SOL_STR_TABLE_ITEM("inactive",     SOL_PLATFORM_SERVICE_STATE_INACTIVE),
        SOL_STR_TABLE_ITEM("failed",       SOL_PLATFORM_SERVICE_STATE_FAILED),
        SOL_STR_TABLE_ITEM("activating",   SOL_PLATFORM_SERVICE_STATE_ACTIVATING),
        SOL_STR_TABLE_ITEM("deactivating", SOL_PLATFORM_SERVICE_STATE_DEACTIVATING),
        { }
    };
    int r;

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &str);
    SOL_INT_CHECK(r, < 0, false);

    state = sol_str_table_lookup_fallback(table, sol_str_slice_from_str(str),
        SOL_PLATFORM_SERVICE_STATE_UNKNOWN);
    changed = state != x->properties.state;
    x->properties.state = state;

    return changed;
}

enum {
    SERVICE_PROPERTY_STATE,
};

static void
_service_properties_changed(void *data, const char *path, uint64_t mask)
{
    struct service *x = data;

    SOL_DBG("mask=%" PRIu64, mask);

    if (mask & (1 << SERVICE_PROPERTY_STATE)) {
        SOL_DBG("New service (%s) state: [%d]",
            x->service, x->properties.state);
        sol_platform_inform_service_monitors(x->service, x->properties.state);
    }
}

static const struct sol_bus_properties _service_properties[] = {
    [SERVICE_PROPERTY_STATE] = {
        .member = "ActiveState",
        .set = _service_set_state,
    },
    { }
};

static int
_add_service_monitor(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct service *x = userdata;
    const char *path;
    int r;

    x->slot = sd_bus_slot_unref(x->slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0)
        return 0;

    r = sd_bus_message_read(reply, "o", &path);
    SOL_INT_CHECK(r, < 0, r);

    sol_bus_map_cached_properties(_ctx.systemd,
        path,
        "org.freedesktop.systemd1.Unit",
        _service_properties,
        _service_properties_changed,
        x);

    return 0;
}

int
sol_platform_impl_add_service_monitor(const char *service)
{
    int r;
    sd_bus_message *m = NULL;
    sd_bus *bus;
    char buf[PATH_MAX];
    const char *unit, *systemd_service;
    struct service *x;

    bus = _get_sd_bus(_ctx.systemd, _systemd_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    systemd_service = sol_bus_client_get_service(_ctx.systemd);
    SOL_NULL_CHECK(systemd_service, -EINVAL);

    unit = sanitize_service_name(buf, service, "add_service_monitor");
    SOL_NULL_CHECK(unit, -EINVAL);

    x = calloc(1, sizeof(*x));
    SOL_NULL_CHECK(x, -ENOMEM);
    x->service = service;
    x->properties.state = SOL_PLATFORM_SERVICE_STATE_UNKNOWN;

    r = sd_bus_message_new_method_call(bus, &m,
        systemd_service,
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "GetUnit");
    SOL_INT_CHECK_GOTO(r, < 0, fail_new_method);

    r = sd_bus_message_append(m, "s", unit);
    SOL_INT_CHECK_GOTO(r, < 0, fail_append);

    r = sd_bus_call_async(bus, &x->slot, m, _add_service_monitor, x, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail_call);

    sd_bus_message_unref(m);

    sol_ptr_vector_append(&_ctx.services, x);

    return 0;

fail_call:
fail_append:
    sd_bus_message_unref(m);
fail_new_method:
    free(x);
    return r;
}

int
sol_platform_impl_del_service_monitor(const char *service)
{
    struct service *x, *found = NULL;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.services, x, i)
        if (streq(x->service, service)) {
            found = x;
            break;
        }
    SOL_NULL_CHECK(found, -ENOENT);

    if (found->slot)
        sd_bus_slot_unref(found->slot);
    else
        sol_bus_unmap_cached_properties(_ctx.systemd, _service_properties, found);

    sol_ptr_vector_del(&_ctx.services, i);
    free(found);

    return 0;
}

static int
call_manager(const char *method, const char *_unit, const char *suffix,
    const char *param, const char *action)
{
    sd_bus_message *m = NULL;
    sd_bus *bus;
    char buf[PATH_MAX];
    const char *unit, *service;
    int r;

    bus = _get_sd_bus(_ctx.systemd, _systemd_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    service = sol_bus_client_get_service(_ctx.systemd);
    SOL_NULL_CHECK(service, -EINVAL);

    unit = sanitize_unit_name(buf, _unit, suffix, action);
    SOL_NULL_CHECK(unit, -EINVAL);

    r = sd_bus_message_new_method_call(bus, &m,
        service,
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        method);
    SOL_INT_CHECK_GOTO(r, < 0, fail_new_method);

    r = sd_bus_message_append(m, "ss", unit, param);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sd_bus_call_async(bus, NULL, m, sol_bus_log_callback, NULL, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    sd_bus_message_unref(m);

    return 0;

fail:
    sd_bus_message_unref(m);
fail_new_method:
    SOL_WRN("Failed to %s service '%s': %s", action, unit, sol_util_strerrora(-r));
    return r;
}

static inline int
call_manager_for_service(const char *method, const char *service,
    const char *action)
{
    return call_manager(method, service, ".service", "replace", action);
}

int
sol_platform_impl_start_service(const char *service)
{
    return call_manager_for_service("StartUnit", service, "start");
}

int
sol_platform_impl_stop_service(const char *service)
{
    return call_manager_for_service("StopUnit", service, "stop");
}

int
sol_platform_impl_restart_service(const char *service)
{
    return call_manager_for_service("RestartUnit", service, "restart");
}

enum {
    TARGET_PARAM_ISOLATE = 0,
    TARGET_PARAM_REPLACE_IRREVERSIBLY,
};

int
sol_platform_impl_set_target(const char *target)
{
    int16_t idx;
    static const char *params[] = {
        [TARGET_PARAM_ISOLATE] = "isolate",
        [TARGET_PARAM_REPLACE_IRREVERSIBLY] = "replace-irreversibly",
    };
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_DEFAULT,   TARGET_PARAM_ISOLATE),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_RESCUE,    TARGET_PARAM_ISOLATE),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_EMERGENCY, TARGET_PARAM_ISOLATE),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_POWER_OFF,  TARGET_PARAM_REPLACE_IRREVERSIBLY),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_REBOOT,    TARGET_PARAM_REPLACE_IRREVERSIBLY),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_SUSPEND,   TARGET_PARAM_REPLACE_IRREVERSIBLY),
        { }
    };

    idx = sol_str_table_lookup_fallback(table, sol_str_slice_from_str(target),
        TARGET_PARAM_ISOLATE);

    return call_manager("StartUnit", target, ".target", params[idx], "set_target");
}

int
sol_platform_impl_get_machine_id(char id[SOL_STATIC_ARRAY_SIZE(33)])
{
    int r;

    r = sol_util_read_file("/etc/machine-id", "%32s", id);
    /* that id should have already been validated by systemd */

    return r;
}

int
sol_platform_impl_get_serial_number(char **number)
{
    int r;
    char id[37];

    /* root access required for this */
    r = sol_util_read_file("/sys/class/dmi/id/product_uuid", "%36s", id);
    SOL_INT_CHECK(r, < 0, r);

    *number = strdup(id);
    if (!*number)
        return -errno;

    return r;
}

int
sol_platform_impl_init(void)
{
    /* For systemd backend we do nothing: we will only attach its mainloop to
     * ours and connect to D-Bus if client calls any method that needs it:
     * see _bus_initialized() */

    sol_ptr_vector_init(&_ctx.services);

    return 0;
}

void
sol_platform_impl_shutdown(void)
{
    sol_ptr_vector_clear(&_ctx.services);

    sol_bus_close();
    sol_platform_unregister_system_clock_monitor();
}


static int
_hostname_bus_initialized(sd_bus *bus)
{
    _ctx.hostname = sol_bus_client_new(bus, "org.freedesktop.hostname1");
    SOL_NULL_CHECK(_ctx.hostname, -ENOMEM);
    return 0;
}

int
sol_platform_impl_set_hostname(const char *name)
{
    sd_bus *bus;
    const char *service;
    int r;

    bus = _get_sd_bus(_ctx.hostname, _hostname_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    service = sol_bus_client_get_service(_ctx.hostname);
    SOL_NULL_CHECK(service, -EINVAL);

    bus = sol_bus_get(NULL);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    r = sd_bus_call_method_async(bus, NULL, service,
        "/org/freedesktop/hostname1", "org.freedesktop.hostname1",
        "SetStaticHostname", sol_bus_log_callback, NULL, "sb", name, false);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static bool
skip_prop(void *data, const char *path, sd_bus_message *m)
{
    int r;
    const char *contents;
    char type;

    r = sd_bus_message_peek_type(m, &type, &contents);
    SOL_INT_CHECK(r, < 0, true);

    r = sd_bus_message_skip(m, contents);
    SOL_INT_CHECK(r, < 0, true);

    return true;
}

static const struct sol_bus_properties _hostname_property = {
    .member = "StaticHostname",
    .set = skip_prop,
};

int
sol_platform_unregister_hostname_monitor(void)
{
    if (!_ctx.hostname || !_ctx.hostname_monitor_registered)
        return 0;
    _ctx.hostname_monitor_registered = false;
    return sol_bus_unmap_cached_properties(_ctx.hostname,
        &_hostname_property, NULL);
}

static void
_hostname_changed(void *data, const char *path, uint64_t mask)
{
    sol_platform_inform_hostname_monitors();
}

int
sol_platform_register_hostname_monitor(void)
{
    sd_bus *bus;
    int r;

    if (_ctx.hostname_monitor_registered)
        return 0;

    bus = _get_sd_bus(_ctx.hostname, _hostname_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    r = sol_bus_map_cached_properties(_ctx.hostname,
        "/org/freedesktop/hostname1", "org.freedesktop.hostname1",
        &_hostname_property, _hostname_changed, NULL);
    if (!r)
        _ctx.hostname_monitor_registered = true;
    return r;
}

static int
_timedate_bus_initialized(sd_bus *bus)
{
    _ctx.timedate = sol_bus_client_new(bus, "org.freedesktop.timedate1");
    SOL_NULL_CHECK(_ctx.timedate, -ENOMEM);
    return 0;
}

int
sol_platform_impl_set_system_clock(int64_t timestamp)
{
    sd_bus *bus;
    int r;
    int64_t timestamp_micro;
    const char *service;

    bus = _get_sd_bus(_ctx.timedate, _timedate_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    service = sol_bus_client_get_service(_ctx.timedate);
    SOL_NULL_CHECK(service, -EINVAL);

    r = sol_util_int64_mul(timestamp, SOL_UTIL_USEC_PER_SEC, &timestamp_micro);
    SOL_INT_CHECK(r, < 0, r);

    bus = sol_bus_get(NULL);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    r = sd_bus_call_method_async(bus, NULL, service,
        "/org/freedesktop/timedate1", "org.freedesktop.timedate1",
        "SetTime", sol_bus_log_callback, NULL, "xbb", timestamp_micro, false, false);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

int
sol_platform_impl_set_timezone(const char *tmz)
{
    sd_bus *bus;
    const char *service;
    int r;

    bus = _get_sd_bus(_ctx.timedate, _timedate_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    service = sol_bus_client_get_service(_ctx.timedate);
    SOL_NULL_CHECK(service, -EINVAL);

    r = sd_bus_call_method_async(bus, NULL, service,
        "/org/freedesktop/timedate1", "org.freedesktop.timedate1",
        "SetTimezone", sol_bus_log_callback, NULL, "sb", tmz, false);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static const struct sol_bus_properties _timezone_property = {
    .member = "Timezone",
    .set = skip_prop,
};

static void
_timezone_changed(void *data, const char *path, uint64_t mask)
{
    sol_platform_inform_timezone_changed();
}

int
sol_platform_register_timezone_monitor(void)
{
    sd_bus *bus;
    int r;

    if (_ctx.timedate_monitor_registered)
        return 0;

    bus = _get_sd_bus(_ctx.timedate, _timedate_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    r = sol_bus_map_cached_properties(_ctx.timedate,
        "/org/freedesktop/timedate1", "org.freedesktop.timedate1",
        &_timezone_property, _timezone_changed, NULL);
    if (!r)
        _ctx.timedate_monitor_registered = true;
    return r;
}

int
sol_platform_unregister_timezone_monitor(void)
{
    if (!_ctx.timedate || !_ctx.timedate_monitor_registered)
        return 0;
    _ctx.timedate_monitor_registered = false;
    return sol_bus_unmap_cached_properties(_ctx.timedate,
        &_timezone_property, NULL);
}

static int
_localed_bus_initialized(sd_bus *bus)
{
    _ctx.locale = sol_bus_client_new(bus, "org.freedesktop.locale1");
    SOL_NULL_CHECK(_ctx.locale, -ENOMEM);
    return 0;
}

int
sol_platform_impl_set_locale(char **locales)
{
    sd_bus *bus;
    int r;
    char *str;
    sd_bus_message *m;
    const char *service;
    enum sol_platform_locale_category i;

    bus = _get_sd_bus(_ctx.locale, _localed_bus_initialized);
    SOL_NULL_CHECK(bus, -EINVAL);

    service = sol_bus_client_get_service(_ctx.locale);
    SOL_NULL_CHECK(service, -EINVAL);

    r = sd_bus_message_new_method_call(bus, &m,
        service,
        "/org/freedesktop/locale1",
        "org.freedesktop.locale1",
        "SetLocale");
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_message_open_container(m, 'a', "s");
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    for (i = SOL_PLATFORM_LOCALE_LANGUAGE; i <= SOL_PLATFORM_LOCALE_TIME; i++) {
        r = asprintf(&str, "%s=%s", sol_platform_locale_to_c_str_category(i),
            locales[i] ? locales[i] : "C");
        if (r < 0) {
            r = -ENOMEM;
            goto exit;
        }
        r = sd_bus_message_append_basic(m, 's', str);
        free(str);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sd_bus_message_close_container(m);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sd_bus_message_append(m, "b", false);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sd_bus_call_async(bus, NULL, m, sol_bus_log_callback, NULL, 0);

exit:
    sd_bus_message_unref(m);
    return r;
}

static void
_locale_changed(void *data, const char *path, uint64_t mask)
{
    sol_platform_inform_locale_changed();
}

static const struct sol_bus_properties _locale_property = {
    .member = "Locale",
    .set = skip_prop,
};

int
sol_platform_register_locale_monitor(void)
{
    sd_bus *bus;
    int r;

    if (_ctx.timedate_monitor_registered)
        return 0;

    bus = _get_sd_bus(_ctx.locale, _localed_bus_initialized);
    SOL_NULL_CHECK(bus, -EINVAL);

    r = sol_bus_map_cached_properties(_ctx.locale, "/org/freedesktop/locale1",
        "org.freedesktop.locale1", &_locale_property, _locale_changed, NULL);
    if (!r)
        _ctx.timedate_monitor_registered = true;
    return r;
}

int
sol_platform_unregister_locale_monitor(void)
{
    if (!_ctx.locale || !_ctx.timedate_monitor_registered)
        return 0;
    _ctx.locale_monitor_registered = false;
    return sol_bus_unmap_cached_properties(_ctx.locale,
        &_locale_property, NULL);
}
