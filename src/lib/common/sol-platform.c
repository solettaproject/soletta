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
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>

#include "sol-platform-impl.h"

#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-monitors.h"
#ifdef SOL_PLATFORM_LINUX
#include "sol-board-detect.h"
#endif
#include "sol-platform.h"
#include "sol-util.h"

#define SOL_BOARD_NAME_ENVVAR "SOL_BOARD_NAME"

SOL_LOG_INTERNAL_DECLARE(_sol_platform_log_domain, "platform");

static char *board_name = NULL;
static char *os_version = NULL;

struct service_monitor {
    struct sol_monitors_entry base;
    char *service;
    enum sol_platform_service_state state;
};

struct ctx {
    struct sol_monitors state_monitors;
    struct sol_monitors service_monitors;
};

static struct ctx _ctx;

int sol_platform_init(void);
void sol_platform_shutdown(void);

static void service_monitor_free(const struct sol_monitors *ms, const struct sol_monitors_entry *e);

int
sol_platform_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    sol_monitors_init(&_ctx.state_monitors, NULL);
    sol_monitors_init_custom(&_ctx.service_monitors, sizeof(struct service_monitor), service_monitor_free);

    return sol_platform_impl_init();
}

void
sol_platform_shutdown(void)
{
    free(board_name);
    free(os_version);
    sol_monitors_clear(&_ctx.state_monitors);
    sol_monitors_clear(&_ctx.service_monitors);
    sol_platform_impl_shutdown();
}

static bool
sol_board_name_is_valid(const char *name)
{
    unsigned int i;

    if (name[0] == '\0')
        return false;

    for (i = 0; name[i] != '\0'; i++) {
        if (isalnum(name[i]) || name[i] == '_' || name[i] == '-')
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
            SOL_DBG("pre-defined BOARD_NAME=%s", board_name);
        } else {
            SOL_WRN("pre-defined BOARD_NAME=%s contains invalid chars.",
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

SOL_API int
sol_platform_add_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data)
{
    struct sol_monitors_entry *e;

    SOL_NULL_CHECK(cb, -EINVAL);

    e = sol_monitors_append(&_ctx.state_monitors, (sol_monitors_cb_t)cb, data);
    SOL_NULL_CHECK(e, -ENOMEM);

    return 0;
}

SOL_API int
sol_platform_del_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data)
{
    int i;

    SOL_NULL_CHECK(cb, -EINVAL);

    i = sol_monitors_find(&_ctx.state_monitors, (sol_monitors_cb_t)cb, data);
    if (i < 0)
        return i;

    return sol_monitors_del(&_ctx.state_monitors, i);
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

SOL_API int
sol_platform_get_machine_id(char id[static 33])
{
#ifdef SOL_PLATFORM_LINUX
    char *env_id = getenv("SOL_MACHINE_ID");

    if (env_id) {
        if (strlen(env_id) > 32 || !sol_util_uuid_str_valid(env_id)) {
            SOL_WRN("Malformed UUID passed on environment variable "
                "SOL_MACHINE_ID: %s", env_id);
            return -EINVAL;
        }
        memcpy(id, env_id, 33);

        return 0;
    }
#endif
    return sol_platform_impl_get_machine_id(id);
}

SOL_API int
sol_platform_get_serial_number(char **number)
{
#ifdef SOL_PLATFORM_LINUX
    char *env_id;
#endif

    SOL_NULL_CHECK(number, -EINVAL);

#ifdef SOL_PLATFORM_LINUX
    env_id = getenv("SOL_SERIAL_NUMBER");
    if (env_id) {
        *number = strdup(env_id);
        SOL_NULL_CHECK(*number, -errno);

        return 0;
    }
#endif
    return sol_platform_impl_get_serial_number(number);
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
