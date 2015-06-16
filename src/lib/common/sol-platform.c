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

#include "sol-platform-impl.h"

#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-monitors.h"
#include "sol-platform-detect.h"
#include "sol-platform.h"
#include "sol-util.h"

#define SOL_PLATFORM_NAME_ENVVAR "SOL_PLATFORM_NAME"

SOL_LOG_INTERNAL_DECLARE(_sol_platform_log_domain, "platform");

static char *platform_name = NULL;

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
    free(platform_name);
    sol_monitors_clear(&_ctx.state_monitors);
    sol_monitors_clear(&_ctx.service_monitors);
    sol_platform_impl_shutdown();
}

SOL_API const char *
sol_platform_get_name(void)
{
    if (platform_name)
        return platform_name;

#ifdef SOL_PLATFORM_LINUX
    platform_name = getenv(SOL_PLATFORM_NAME_ENVVAR);
    if (platform_name && *platform_name != '\0')
        platform_name = strdup(platform_name);
    else
        platform_name = sol_platform_detect();
#endif

#ifdef PLATFORM_NAME
    if (!platform_name)
        platform_name = strdup(PLATFORM_NAME);
#endif

    return platform_name;
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

    if (!found) {
        r = sol_platform_impl_add_service_monitor(s);
        SOL_INT_CHECK_GOTO(r, < 0, fail_add_impl);
    }

    m->service = s;
    m->state = SOL_PLATFORM_SERVICE_STATE_UNKNOWN;

    return 0;

fail_add_impl:
    sol_monitors_del(ms, sol_monitors_count(ms) - 1);
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
