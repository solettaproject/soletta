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

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>

#include "sol-platform-impl.h"

#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-monitors.h"
#ifdef SOL_PLATFORM_LINUX
#include "sol-board-detect.h"
#endif
#include "sol-platform.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#define SOL_BOARD_NAME_ENVVAR "SOL_BOARD_NAME"

SOL_LOG_INTERNAL_DECLARE(_sol_platform_log_domain, "platform");

static char *board_name = NULL;
static char *os_version = NULL;
static char *serial_number = NULL;

struct service_monitor {
    struct sol_monitors_entry base;
    char *service;
    enum sol_platform_service_state state;
};

struct ctx {
    struct sol_monitors state_monitors;
    struct sol_monitors service_monitors;
    struct sol_monitors hostname_monitors;
    struct sol_monitors system_clock_monitors;
    struct sol_monitors timezone_monitors;
    struct sol_monitors locale_monitors;
    struct sol_timeout *locale_timeout;
    char *locale_cache[SOL_PLATFORM_LOCALE_TIME + 1];
};

static struct ctx _ctx;

int sol_platform_init(void);
void sol_platform_shutdown(void);

static void service_monitor_free(const struct sol_monitors *ms, const struct sol_monitors_entry *e);

static void
locale_cache_clear(void)
{
    enum sol_platform_locale_category i;

    for (i = SOL_PLATFORM_LOCALE_LANGUAGE; i <= SOL_PLATFORM_LOCALE_TIME; i++)
        free(_ctx.locale_cache[i]);
}

int
sol_platform_init(void)
{
    int r;
    enum sol_platform_locale_category i;

    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    sol_monitors_init(&_ctx.state_monitors, NULL);
    sol_monitors_init(&_ctx.hostname_monitors, NULL);
    sol_monitors_init(&_ctx.system_clock_monitors, NULL);
    sol_monitors_init(&_ctx.timezone_monitors, NULL);
    sol_monitors_init(&_ctx.locale_monitors, NULL);
    for (i = SOL_PLATFORM_LOCALE_LANGUAGE; i <= SOL_PLATFORM_LOCALE_TIME; i++)
        _ctx.locale_cache[i] = NULL;
    r = sol_platform_impl_load_locales(_ctx.locale_cache);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    _ctx.locale_timeout = NULL;
    sol_monitors_init_custom(&_ctx.service_monitors, sizeof(struct service_monitor), service_monitor_free);

    return sol_platform_impl_init();

err_exit:
    locale_cache_clear();
    return r;
}

static void
set_locale(void)
{
    int r;

    r = sol_platform_impl_set_locale(_ctx.locale_cache);
    if (r < 0)
        SOL_WRN("Could not set the locale values!");
}

void
sol_platform_shutdown(void)
{
    free(board_name);
    board_name = NULL;
    free(os_version);
    os_version = NULL;
    free(serial_number);
    serial_number = NULL;
    sol_monitors_clear(&_ctx.state_monitors);
    sol_monitors_clear(&_ctx.service_monitors);
    sol_monitors_clear(&_ctx.hostname_monitors);
    sol_monitors_clear(&_ctx.system_clock_monitors);
    sol_monitors_clear(&_ctx.timezone_monitors);
    sol_monitors_clear(&_ctx.locale_monitors);
    locale_cache_clear();
    if (_ctx.locale_timeout) {
        sol_timeout_del(_ctx.locale_timeout);
        set_locale();
    }
    sol_platform_impl_shutdown();
}

static bool
sol_board_name_is_valid(const char *name)
{
    unsigned int i;

    if (name[0] == '\0')
        return false;

    for (i = 0; name[i] != '\0'; i++) {
        if (isalnum((uint8_t)name[i]) || name[i] == '_' || name[i] == '-')
            continue;

        return false;
    }

    return true;
}

SOL_API const char *
sol_platform_get_board_name(void)
{
    if (board_name)
        return board_name;

#ifdef SOL_PLATFORM_LINUX
    board_name = getenv(SOL_BOARD_NAME_ENVVAR);
    if (board_name && sol_board_name_is_valid(board_name)) {
        board_name = strdup(board_name);
        SOL_DBG("envvar SOL_BOARD_NAME=%s", board_name);
    } else {
        if (board_name)
            SOL_WRN("envvar SOL_BOARD_NAME=%s contains invalid chars.",
                board_name);
        board_name = NULL;
    }
#endif

#ifdef DETECT_BOARD_NAME
    if (!board_name) {
        board_name = sol_board_detect();
        if (board_name && sol_board_name_is_valid(board_name))
            SOL_DBG("detected board name=%s", board_name);
        else if (board_name) {
            SOL_WRN("detected board name=%s contains invalid chars.",
                board_name);
            free(board_name);
            board_name = NULL;
        }
    }
#endif

#ifdef BOARD_NAME
    if (!board_name && strlen(BOARD_NAME) > 0) {
        if (sol_board_name_is_valid(BOARD_NAME)) {
            board_name = strdup(BOARD_NAME);
            SOL_DBG("predefined BOARD_NAME=%s", board_name);
        } else {
            SOL_WRN("predefined BOARD_NAME=%s contains invalid chars.",
                BOARD_NAME);
        }
    }
#endif

    if (board_name)
        SOL_DBG("using board name=%s", board_name);
    else
        SOL_DBG("board name is unknown");

    return board_name;
}

SOL_API int
sol_platform_get_state(void)
{
    return sol_platform_impl_get_state();
}

static int
monitor_add(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data)
{
    struct sol_monitors_entry *e;

    SOL_NULL_CHECK(cb, -EINVAL);

    e = sol_monitors_append(monitors, cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);
    return 0;
}

static int
monitor_del(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data)
{
    int i;

    SOL_NULL_CHECK(cb, -EINVAL);

    i = sol_monitors_find(monitors, cb, data);
    if (i < 0)
        return i;

    return sol_monitors_del(monitors, i);
}

SOL_API int
sol_platform_add_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data)
{

    return monitor_add(&_ctx.state_monitors, (sol_monitors_cb_t)cb, data);
}

SOL_API int
sol_platform_del_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data)
{
    return monitor_del(&_ctx.state_monitors, (sol_monitors_cb_t)cb, data);
}

static inline struct service_monitor *
find_service_monitor(const char *service, bool duplicate)
{
    struct service_monitor *m;
    bool found = false;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&_ctx.service_monitors.entries, m, i) {
        if (!m->base.cb)
            continue;
        if (streq(service, m->service)) {
            if (!duplicate || found)
                return m;
            found = true;
        }
    }
    return NULL;
}


SOL_API enum sol_platform_service_state
sol_platform_get_service_state(const char *service)
{
    struct service_monitor *m;

    m = find_service_monitor(service, false);
    if (!m)
        return SOL_PLATFORM_SERVICE_STATE_UNKNOWN;

    return m->state;
}

static void
service_monitor_free(const struct sol_monitors *monitors, const struct sol_monitors_entry *e)
{
    struct service_monitor *m = (struct service_monitor *)e;

    /* Last monitor is gone, remove from underlying implementation.
     * Ignore error since there's nothing to do but logging */
    if (!find_service_monitor(m->service, true))
        sol_platform_impl_del_service_monitor(m->service);

    free(m->service);
}

SOL_API int
sol_platform_add_service_monitor(void (*cb)(void *data, const char *service,
    enum sol_platform_service_state state),
    const char *service,
    const void *data)
{
    struct sol_monitors *ms;
    struct service_monitor *m, *found;
    char *s = NULL;
    int r;

    SOL_NULL_CHECK(cb, -EINVAL);
    SOL_NULL_CHECK(service, -EINVAL);

    ms = &_ctx.service_monitors;

    s = strdup(service);
    SOL_NULL_CHECK(s, -ENOMEM);

    found = find_service_monitor(s, false);

    r = -ENOMEM;
    m = sol_monitors_append(ms, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK_GOTO(m, fail_append);

    m->service = s;
    m->state = SOL_PLATFORM_SERVICE_STATE_UNKNOWN;

    if (!found) {
        r = sol_platform_impl_add_service_monitor(s);
        SOL_INT_CHECK_GOTO(r, < 0, fail_add_impl);
    }


    return 0;

fail_add_impl:
    sol_monitors_del(ms, sol_monitors_count(ms) - 1);
    return r;

fail_append:
    free(s);
    return r;
}

SOL_API int
sol_platform_del_service_monitor(void (*cb)(void *data, const char *service,
    enum sol_platform_service_state state),
    const char *service,
    const void *data)
{
    struct sol_monitors *ms;
    struct service_monitor *m;
    int r = -ENOENT;
    uint16_t i;

    SOL_NULL_CHECK(cb, -EINVAL);
    SOL_NULL_CHECK(service, -EINVAL);
    ms = &_ctx.service_monitors;

    SOL_MONITORS_WALK (ms, m, i) {
        if ((m->base.cb != (sol_monitors_cb_t)cb) || (data && m->base.data != data)
            || !streq(m->service, service))
            continue;

        sol_monitors_del(ms, i);
        r = 0;
        break;
    }

    return r;
}

SOL_API int
sol_platform_start_service(const char *service)
{
    SOL_NULL_CHECK(service, -EINVAL);
    return sol_platform_impl_start_service(service);
}

SOL_API int
sol_platform_stop_service(const char *service)
{
    SOL_NULL_CHECK(service, -EINVAL);
    return sol_platform_impl_stop_service(service);
}

SOL_API int
sol_platform_restart_service(const char *service)
{
    SOL_NULL_CHECK(service, -EINVAL);
    return sol_platform_impl_restart_service(service);
}

SOL_API int
sol_platform_set_target(const char *target)
{
    SOL_NULL_CHECK(target, -EINVAL);
    return sol_platform_impl_set_target(target);
}

SOL_API const char *
sol_platform_get_machine_id(void)
{
    static char id[33];

#ifdef SOL_PLATFORM_LINUX
    char *env_id = getenv("SOL_MACHINE_ID");

    if (env_id) {
        if (strlen(env_id) > 32 || !sol_util_uuid_str_valid(env_id)) {
            SOL_WRN("Malformed UUID passed on environment variable "
                "SOL_MACHINE_ID: %s", env_id);
            return NULL;
        }
        return env_id;
    }
#endif
    if (!id[0]) {
        int r = sol_platform_impl_get_machine_id(id);
        if (r < 0) {
            id[0] = '\0';
            return NULL;
        }
    }

    return id;
}

SOL_API const char *
sol_platform_get_serial_number(void)
{
    int r;
    char *out;

#ifdef SOL_PLATFORM_LINUX
    char *env_id;
    env_id = getenv("SOL_SERIAL_NUMBER");
    if (env_id) {
        return env_id;
    }
#endif
    if (serial_number) return serial_number;
    r = sol_platform_impl_get_serial_number(&out);
    SOL_INT_CHECK(r, < 0, NULL);
    serial_number = out;
    return serial_number;
}

SOL_API const char *
sol_platform_get_sw_version(void)
{
    return VERSION;
}

SOL_API const char *
sol_platform_get_os_version(void)
{
    int r;
    char *out;

    if (os_version) return os_version;
    r = sol_platform_impl_get_os_version(&out);
    SOL_INT_CHECK(r, < 0, NULL);
    os_version = out;
    return os_version;
}

void
sol_platform_inform_state_monitors(enum sol_platform_state state)
{
    struct sol_monitors_entry *e;
    uint16_t i;

    SOL_MONITORS_WALK (&_ctx.state_monitors, e, i) {
        if (!e->cb)
            continue;
        ((void (*)(void *, enum sol_platform_state))e->cb)((void *)e->data, state);
    }
}

void
sol_platform_inform_service_monitors(const char *service,
    enum sol_platform_service_state state)
{
    struct service_monitor *m;
    uint16_t i;

    SOL_MONITORS_WALK (&_ctx.service_monitors, m, i) {
        if (!m->base.cb || !streq(m->service, service))
            continue;
        m->state = state;
        ((void (*)(void *data, const char *service, enum sol_platform_service_state state))m->base.cb)((void *)m->base.data, service, state);
    }
}

SOL_API int
sol_platform_get_mount_points(struct sol_ptr_vector *vector)
{
    SOL_NULL_CHECK(vector, -EINVAL);
    return sol_platform_impl_get_mount_points(vector);
}

SOL_API int
sol_platform_unmount(const char *mpoint, void (*cb)(void *data, const char *mpoint, int error), const void *data)
{
    SOL_NULL_CHECK(mpoint, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);
    return sol_platform_impl_umount(mpoint, cb, data);
}

SOL_API int
sol_platform_set_hostname(const char *name)
{
    SOL_NULL_CHECK(name, -EINVAL);
    return sol_platform_impl_set_hostname(name);
}

SOL_API const char *
sol_platform_get_hostname(void)
{
    return sol_platform_impl_get_hostname();
}

static int
monitor_add_and_register(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data,
    int (*register_cb)(void))
{
    int r;

    SOL_NULL_CHECK(cb, -EINVAL);
    r = monitor_add(monitors, cb, data);
    SOL_INT_CHECK(r, < 0, r);
    if (sol_monitors_count(monitors) == 1) {
        r = register_cb();
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }
    return 0;
err_exit:
    (void)monitor_del(monitors, cb, data);
    return r;
}

static int
monitor_del_and_unregister(struct sol_monitors *monitors, sol_monitors_cb_t cb, const void *data,
    int (*unregister_cb)(void))
{
    int r;

    SOL_NULL_CHECK(cb, -EINVAL);
    r = monitor_del(monitors, (sol_monitors_cb_t)cb, data);
    SOL_INT_CHECK(r, < 0, r);

    if (sol_monitors_count(monitors) == 0)
        return unregister_cb();
    return 0;
}

void
sol_platform_inform_hostname_monitors(void)
{
    const char *hostname;
    struct sol_monitors_entry *entry;
    uint16_t i;

    hostname = sol_platform_impl_get_hostname();
    SOL_NULL_CHECK(hostname);

    SOL_MONITORS_WALK (&_ctx.hostname_monitors, entry, i)
        ((void (*)(void *, const char *))entry->cb)((void *)entry->data, hostname);
}

SOL_API int
sol_platform_add_hostname_monitor(void (*cb)(void *data, const char *hostname), const void *data)
{
    return monitor_add_and_register(&_ctx.hostname_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_register_hostname_monitor);
}

SOL_API int
sol_platform_del_hostname_monitor(void (*cb)(void *data, const char *hostname), const void *data)
{
    return monitor_del_and_unregister(&_ctx.hostname_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_unregister_hostname_monitor);
}

SOL_API int
sol_platform_set_system_clock(int64_t timestamp)
{
    return sol_platform_impl_set_system_clock(timestamp);
}

SOL_API int64_t
sol_platform_get_system_clock(void)
{
    return sol_platform_impl_get_system_clock();
}

void
sol_platform_inform_system_clock_changed(void)
{
    int64_t timestamp;
    struct sol_monitors_entry *entry;
    uint16_t i;

    timestamp = sol_platform_impl_get_system_clock();
    SOL_INT_CHECK(timestamp, < 0);

    SOL_MONITORS_WALK (&_ctx.system_clock_monitors, entry, i)
        ((void (*)(void *, int64_t))entry->cb)((void *)entry->data, timestamp);
}

SOL_API int
sol_platform_add_system_clock_monitor(void (*cb)(void *data, int64_t timestamp), const void *data)
{
    return monitor_add_and_register(&_ctx.system_clock_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_register_system_clock_monitor);
}

SOL_API int
sol_platform_del_system_clock_monitor(void (*cb)(void *data, int64_t timestamp), const void *data)
{
    return monitor_del_and_unregister(&_ctx.system_clock_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_unregister_system_clock_monitor);
}

void
sol_platform_inform_timezone_changed(void)
{
    const char *timezone;
    struct sol_monitors_entry *entry;
    uint16_t i;

    timezone = sol_platform_impl_get_timezone();
    SOL_NULL_CHECK(timezone);

    SOL_MONITORS_WALK (&_ctx.timezone_monitors, entry, i)
        ((void (*)(void *, const char *))entry->cb)((void *)entry->data, timezone);
}

SOL_API int
sol_platform_set_timezone(const char *timezone)
{
    SOL_NULL_CHECK(timezone, -EINVAL);
    return sol_platform_impl_set_timezone(timezone);
}

SOL_API const char *
sol_platform_get_timezone(void)
{
    return sol_platform_impl_get_timezone();
}

SOL_API int
sol_platform_add_timezone_monitor(void (*cb)(void *data, const char *timezone), const void *data)
{
    return monitor_add_and_register(&_ctx.timezone_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_register_timezone_monitor);
}

SOL_API int
sol_platform_del_timezone_monitor(void (*cb)(void *data, const char *timezone), const void *data)
{
    return monitor_del_and_unregister(&_ctx.timezone_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_unregister_timezone_monitor);
}

void
sol_platform_inform_locale_changed(void)
{
    struct sol_monitors_entry *entry;
    uint16_t j;
    int r;
    enum sol_platform_locale_category i;

    r = sol_platform_impl_load_locales(_ctx.locale_cache);
    SOL_INT_CHECK(r, < 0);
    for (i = SOL_PLATFORM_LOCALE_LANGUAGE; i <= SOL_PLATFORM_LOCALE_TIME; i++) {
        SOL_MONITORS_WALK (&_ctx.locale_monitors, entry, j) {
            ((void (*)(void *, enum sol_platform_locale_category, const char *))
            entry->cb)((void *)entry->data, i, _ctx.locale_cache[i] ? : "C");
        }
    }
}

static bool
locale_timeout_cb(void *data)
{
    _ctx.locale_timeout = NULL;
    set_locale();
    return false;
}

SOL_API int
sol_platform_set_locale(enum sol_platform_locale_category category, const char *locale)
{
    int r;

    SOL_INT_CHECK(category, == SOL_PLATFORM_LOCALE_UNKNOWN, -EINVAL);
    SOL_NULL_CHECK(locale, -EINVAL);
    r = sol_util_replace_str_if_changed(&_ctx.locale_cache[category], locale);
    SOL_INT_CHECK(r, < 0, r);

    if (!_ctx.locale_timeout) {
        _ctx.locale_timeout = sol_timeout_add(1, locale_timeout_cb, NULL);
        SOL_NULL_CHECK(_ctx.locale_timeout, -ENOMEM);
    }

    return 0;
}

SOL_API const char *
sol_platform_get_locale(enum sol_platform_locale_category category)
{
    SOL_INT_CHECK(category, == SOL_PLATFORM_LOCALE_UNKNOWN, NULL);
    if (category == SOL_PLATFORM_LOCALE_LANGUAGE)
        return _ctx.locale_cache[category] ? : "C";
    return sol_platform_impl_get_locale(category);
}

SOL_API int
sol_platform_add_locale_monitor(void (*cb)(void *data,
    enum sol_platform_locale_category category, const char *locale), const void *data)
{
    return monitor_add_and_register(&_ctx.locale_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_register_locale_monitor);
}

SOL_API int
sol_platform_del_locale_monitor(void (*cb)(void *data,
    enum sol_platform_locale_category category, const char *locale), const void *data)
{
    return monitor_del_and_unregister(&_ctx.locale_monitors, (sol_monitors_cb_t)cb, data,
        sol_platform_unregister_locale_monitor);
}

SOL_API int
sol_platform_apply_locale(enum sol_platform_locale_category category)
{
    SOL_INT_CHECK(category, == SOL_PLATFORM_LOCALE_UNKNOWN, -EINVAL);
    return sol_platform_impl_apply_locale(category, _ctx.locale_cache[category] ? : "C");
}

int
sol_platform_locale_to_c_category(enum sol_platform_locale_category category)
{
    switch (category) {
    case SOL_PLATFORM_LOCALE_LANGUAGE:
        return LC_ALL;
    case SOL_PLATFORM_LOCALE_COLLATE:
        return LC_COLLATE;
    case SOL_PLATFORM_LOCALE_CTYPE:
        return LC_CTYPE;
    case SOL_PLATFORM_LOCALE_MONETARY:
        return LC_MONETARY;
    case SOL_PLATFORM_LOCALE_NUMERIC:
        return LC_NUMERIC;
    case SOL_PLATFORM_LOCALE_TIME:
        return LC_TIME;
    default:
        return sol_platform_impl_locale_to_c_category(category);
    }
}

const char *
sol_platform_locale_to_c_str_category(enum sol_platform_locale_category category)
{
    switch (category) {
    case SOL_PLATFORM_LOCALE_LANGUAGE:
        return "LANG";
    case SOL_PLATFORM_LOCALE_COLLATE:
        return "LC_COLLATE";
    case SOL_PLATFORM_LOCALE_CTYPE:
        return "LC_CTYPE";
    case SOL_PLATFORM_LOCALE_MONETARY:
        return "LC_MONETARY";
    case SOL_PLATFORM_LOCALE_NUMERIC:
        return "LC_NUMERIC";
    case SOL_PLATFORM_LOCALE_TIME:
        return "LC_TIME";
    default:
        return sol_platform_impl_locale_to_c_str_category(category);
    }
}
