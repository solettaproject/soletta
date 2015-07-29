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
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-dbus");

#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-util.h"

static struct sol_platform_linux_fork_run *fork_run;
static struct sol_timeout *check_timeout;
static const char *name;

static void
on_fork(void *data)
{
    const char *argv[] = {
        "/usr/bin/dbus-daemon",
        "--config-file=/etc/dbus-1/system.conf",
        "--nofork",
        NULL
    };

    if (mkdir("/run/dbus", 0755) < 0 && errno != EEXIST) {
        SOL_WRN("could not create /run/dbus");
        goto error;
    }

    if (access("/etc/dbus-1/system.conf", R_OK) < 0) {
        FILE *fp;

        SOL_INF("/etc/dbus-1/system.conf does not exist, create one as /run/dbus/system.conf");

        fp = fopen("/run/dbus/system.conf", "we");
        if (!fp) {
            SOL_WRN("could not create /run/dbus/system.conf: %s", sol_util_strerrora(errno));
            goto error;
        }

        fputs("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
            "<busconfig>\n"
            "<type>system</type>\n"
            "<listen>unix:path=/run/dbus/system_bus_socket</listen>\n"
            "<policy context=\"default\">\n"
            "<allow user=\"*\"/>\n"
            "<allow own=\"*\"/>\n"
            "<allow send_type=\"method_call\"/>\n"
            "<allow send_type=\"signal\"/>\n"
            "<allow send_type=\"method_return\"/>\n"
            "<allow send_type=\"error\"/>\n"
            "<allow receive_type=\"method_call\"/>\n"
            "<allow receive_type=\"signal\"/>\n"
            "<allow receive_type=\"method_return\"/>\n"
            "<allow receive_type=\"error\"/>\n"
            "</policy>\n"
            "</busconfig>\n",
            fp);
        fclose(fp);
        argv[1] = "--config-file=/run/dbus/system.conf";
    }
    execv(argv[0], (char *const *)argv);

error:
    sol_platform_linux_fork_run_exit(EXIT_FAILURE);
}

static void
on_fork_exit(void *data, uint64_t pid, int status)
{
    if (check_timeout) {
        sol_timeout_del(check_timeout);
        check_timeout = NULL;
    }

    SOL_DBG("dbus-daemon pid=%" PRIu64 " exited with status=%d", pid, status);

    if (status)
        sol_platform_linux_micro_inform_service_state(name, SOL_PLATFORM_SERVICE_STATE_FAILED);
    else
        sol_platform_linux_micro_inform_service_state(name, SOL_PLATFORM_SERVICE_STATE_INACTIVE);
}

static bool
on_timeout(void *data)
{
    struct stat st;

    if (stat("/run/dbus/system_bus_socket", &st) < 0)
        return true;

    sol_platform_linux_micro_inform_service_state(name, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    check_timeout = NULL;
    return false;
}

static int
dbus_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    if (fork_run)
        return 0;

    name = service;
    fork_run = sol_platform_linux_fork_run(on_fork, on_fork_exit, NULL);
    if (!fork_run) {
        sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_FAILED);
        return -errno;
    }

    SOL_DBG("dbus-daemon started as pid=%" PRIu64,
        (uint64_t)sol_platform_linux_fork_run_get_pid(fork_run));

    /* TODO: change to use inotify */
    check_timeout = sol_timeout_add(200, on_timeout, NULL);

    return 0;
}

static int
dbus_stop(const struct sol_platform_linux_micro_module *mod, const char *service, bool force_immediate)
{
    int err = 0;

    if (!fork_run)
        return 0;

    if (!force_immediate)
        err = sol_platform_linux_fork_run_send_signal(fork_run, SIGTERM);
    else {
        sol_platform_linux_fork_run_stop(fork_run);
        fork_run = NULL;
    }

    if (check_timeout) {
        sol_timeout_del(check_timeout);
        check_timeout = NULL;
    }

    return err;
}

static int
dbus_restart(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    if (!fork_run)
        return dbus_start(mod, service);

    return sol_platform_linux_fork_run_send_signal(fork_run, SIGHUP);
}

static int
dbus_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(DBUS,
    .name = "dbus",
    .init = dbus_init,
    .start = dbus_start,
    .stop = dbus_stop,
    .restart = dbus_restart,
    );
