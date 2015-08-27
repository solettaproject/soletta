/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <systemd/sd-bus.h>

#include "sol-platform-impl.h"

#include "sol-bus.h"
#include "sol-platform.h"
#include "sol-str-table.h"
#include "sol-util.h"
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

    struct sol_ptr_vector services;
};

static struct ctx _ctx;

static bool
_manager_set_system_state(void *data, const void *value)
{
    struct ctx *ctx = data;
    const char *str = value;
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
_manager_properties_changed(void *data, uint64_t mask)
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
        .type = 's',
        .set = _manager_set_system_state,
    },
    { }
};

static void
_bus_initialized(sd_bus *bus)
{
    sd_bus_message *m = NULL;
    int r;

    _ctx.properties.system_state = SOL_PLATFORM_STATE_UNKNOWN;

    r = sd_bus_message_new_method_call(bus, &m,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "Subscribe");
    SOL_INT_CHECK_GOTO(r, < 0, fail_new_method);

    r = sd_bus_call_async(bus, NULL, m, sol_bus_log_callback, NULL, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail_call);
    sd_bus_message_unref(m);

    r = sol_bus_map_cached_properties(bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        _manager_properties,
        _manager_properties_changed,
        &_ctx);
    SOL_INT_CHECK_GOTO(r, < 0, fail_map_properties);

    return;

fail_call:
    sd_bus_message_unref(m);
fail_map_properties:
fail_new_method:
    sol_bus_close();
}

static const char *
sanitize_unit_name(char buf[static PATH_MAX], const char *unit,
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
sanitize_service_name(char buf[static PATH_MAX], const char *service,
    const char *action)
{
    return sanitize_unit_name(buf, service, ".service", action);
}

int
sol_platform_impl_get_state(void)
{
    sd_bus *bus = sol_bus_get(_bus_initialized);

    SOL_NULL_CHECK(bus, -ENOTCONN);

    return _ctx.properties.system_state;
}

static bool
_service_set_state(void *data, const void *value)
{
    struct service *x = data;
    const char *str = value;
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
_service_properties_changed(void *data, uint64_t mask)
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
        .type = 's',
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

    sol_bus_map_cached_properties(sd_bus_message_get_bus(reply),
        "org.freedesktop.systemd1",
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
    const char *unit;
    struct service *x;

    unit = sanitize_service_name(buf, service, "add_service_monitor");
    SOL_NULL_CHECK(unit, -EINVAL);

    bus = sol_bus_get(_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    x = calloc(1, sizeof(*x));
    SOL_NULL_CHECK(x, -ENOMEM);
    x->service = service;
    x->properties.state = SOL_PLATFORM_SERVICE_STATE_UNKNOWN;

    r = sd_bus_message_new_method_call(bus, &m,
        "org.freedesktop.systemd1",
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
        sol_bus_unmap_cached_properties(_service_properties, found);

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
    const char *unit;
    int r;

    unit = sanitize_unit_name(buf, _unit, suffix, action);
    SOL_NULL_CHECK(unit, -EINVAL);

    bus = sol_bus_get(_bus_initialized);
    SOL_NULL_CHECK(bus, -ENOTCONN);

    r = sd_bus_message_new_method_call(bus, &m,
        "org.freedesktop.systemd1",
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
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_POWEROFF,  TARGET_PARAM_REPLACE_IRREVERSIBLY),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_REBOOT,    TARGET_PARAM_REPLACE_IRREVERSIBLY),
        SOL_STR_TABLE_ITEM(SOL_PLATFORM_TARGET_SUSPEND,   TARGET_PARAM_REPLACE_IRREVERSIBLY),
        { }
    };

    idx = sol_str_table_lookup_fallback(table, sol_str_slice_from_str(target),
        TARGET_PARAM_ISOLATE);

    return call_manager("StartUnit", target, ".target", params[idx], "set_target");
}

int
sol_platform_impl_get_machine_id(char id[static 33])
{
    return sol_util_read_file("/etc/machine-id", "%33c", id);
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
}
