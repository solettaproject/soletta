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
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-rc-d");

#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-util.h"
#include "sol-vector.h"

/* interval in milliseconds to poll for service status, when there are services to be polled */
#define SERVICE_MONITOR_INTERVAL 5000

struct pending {
    const char *service; /* will be alive during whole execution */
    const char *arg; /* will be alive during whole execution */
    struct sol_platform_linux_micro_fork_run *fork_run;
    void (*cb)(void *data, const char *service, const char *arg, int status);
    const void *data;
};

static struct sol_ptr_vector monitors = SOL_PTR_VECTOR_INIT;
static struct sol_timeout *monitor_timer;
static struct sol_vector pendings = SOL_VECTOR_INIT(struct pending);

static void
find_exec(const char *service, const char *arg)
{
    const char **itr, *dirs[] = {
        "/etc/init.d",
        "/etc/rc.d"
    };

    for (itr = dirs; itr < dirs + ARRAY_SIZE(dirs); itr++) {
        char path[PATH_MAX];
        int r;

        r = snprintf(path, sizeof(path), "%s/%s", *itr, service);
        if (r > 0 && r < (int)sizeof(path) && access(path, R_OK | X_OK) == 0) {
            SOL_DBG("exec %s %s", path, arg);
            execl(path, path, path, arg, NULL);
        }
    }

    SOL_WRN("service not found: %s", service);
    exit(EXIT_FAILURE);
}

static void
on_fork_run(void *data)
{
    struct pending *p = data;

    find_exec(p->service, p->arg);
}

static void
on_fork_run_exit(void *data, uint64_t pid, int status)
{
    struct pending *p = data;
    struct pending *itr;
    uint16_t i;

    SOL_DBG("pending pid=%" PRIu64 " (%s %s) terminated with status=%d",
        pid, p->service, p->arg, status);

    p->cb((void *)p->data, p->service, p->arg, status);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&pendings, itr, i) {
        if (itr == p) {
            sol_vector_del(&pendings, i);
            break;
        }
    }
}

static int
rc_d_run(const char *service, const char *arg, void (*cb)(void *data, const char *service, const char *arg, int status), const void *data)
{
    struct pending *p;
    int err;

    p = sol_vector_append(&pendings);
    SOL_NULL_CHECK(p, -errno);
    p->service = service;
    p->arg = arg;
    p->cb = cb;
    p->data = data;
    p->fork_run = sol_platform_linux_micro_fork_run(on_fork_run, on_fork_run_exit, p);
    SOL_NULL_CHECK_GOTO(p->fork_run, error_fork_run);
    SOL_DBG("run '%s %s' as pid=%" PRIu64,
        service, arg,
        (uint64_t)sol_platform_linux_micro_fork_run_get_pid(p->fork_run));

    return 0;

error_fork_run:
    err = -errno;
    sol_vector_del(&pendings, pendings.len - 1);
    return err;
}

static void
on_start(void *data, const char *service, const char *arg, int status)
{
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_ACTIVE;

    if (status)
        state = SOL_PLATFORM_SERVICE_STATE_FAILED;
    sol_platform_linux_micro_inform_service_state(service, state);
}

static int
rc_d_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    return rc_d_run(service, "start", on_start, NULL);
}

static void
on_stop(void *data, const char *service, const char *arg, int status)
{
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_INACTIVE;

    if (status)
        state = SOL_PLATFORM_SERVICE_STATE_FAILED;
    sol_platform_linux_micro_inform_service_state(service, state);
}

static int
rc_d_stop(const struct sol_platform_linux_micro_module *mod, const char *service, bool force_immediate)
{
    if (!force_immediate)
        return rc_d_run(service, "stop", on_stop, NULL);
    else {
        pid_t pid = fork();
        if (pid == 0) {
            sigset_t emptyset;
            sigemptyset(&emptyset);
            sigprocmask(SIG_SETMASK, &emptyset, NULL);
            find_exec(service, "stop");
            return -errno;
        } else if (pid < 0)
            return -errno;
        else {
            int status = 0;
            if (waitpid(pid, &status, 0) < 0)
                return -errno;
            on_stop(NULL, service, "stop", status);
            return 0;
        }
    }
}

static void
on_restart(void *data, const char *service, const char *arg, int status)
{
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_ACTIVE;

    if (status)
        state = SOL_PLATFORM_SERVICE_STATE_FAILED;
    sol_platform_linux_micro_inform_service_state(service, state);
}

static int
rc_d_restart(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    return rc_d_run(service, "restart", on_restart, NULL);
}

static void
on_status(void *data, const char *service, const char *arg, int status)
{
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_ACTIVE;

    if (status) {
        state = sol_platform_get_service_state(service);
        if (state != SOL_PLATFORM_SERVICE_STATE_FAILED)
            state = SOL_PLATFORM_SERVICE_STATE_INACTIVE;
    }
    sol_platform_linux_micro_inform_service_state(service, state);
}

static bool
on_monitor_timeout(void *data)
{
    const char *service;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&monitors, service, i)
        rc_d_run(service, "status", on_status, NULL);

    return true;
}

static int
rc_d_start_monitor(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    const char *itr;
    int r;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&monitors, itr, i) {
        if (itr == service)
            return 0;
    }

    /* TODO: vector_append should take a const! */
    r = sol_ptr_vector_append(&monitors, (void *)service);
    SOL_INT_CHECK(r, < 0, r);

    if (!monitor_timer) {
        monitor_timer = sol_timeout_add(SERVICE_MONITOR_INTERVAL, on_monitor_timeout, NULL);
        SOL_NULL_CHECK(monitor_timer, -errno);
    }

    return 0;
}

static int
rc_d_stop_monitor(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    const char *itr = NULL;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&monitors, itr, i) {
        if (itr == service) {
            sol_ptr_vector_del(&monitors, i);
            break;
        }
    }

    if (itr != service)
        return -ENOENT;

    if (sol_ptr_vector_get_len(&monitors) == 0 && monitor_timer) {
        sol_timeout_del(monitor_timer);
        monitor_timer = NULL;
    }

    return 0;
}

static int
rc_d_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

static void
rc_d_shutdown(const struct sol_platform_linux_micro_module *module, const char *service)
{
    struct pending *itr;
    uint16_t i;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&pendings, itr, i) {
        if (itr->service == service)
            sol_platform_linux_micro_fork_run_stop(itr->fork_run);
    }

    rc_d_stop_monitor(module, service);
}

SOL_PLATFORM_LINUX_MICRO_MODULE(RC_D,
    .name = "rc-d",
    .init = rc_d_init,
    .shutdown = rc_d_shutdown,
    .start = rc_d_start,
    .stop = rc_d_stop,
    .restart = rc_d_restart,
    .start_monitor = rc_d_start_monitor,
    .stop_monitor = rc_d_stop_monitor,
    );
