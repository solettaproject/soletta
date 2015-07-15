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
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-bluetooth");

#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-util.h"

static struct sol_platform_linux_fork_run *fork_run;
static const char *name;

static void on_dbus_service_state_changed(void *data,
    const char *service,
    enum sol_platform_service_state state);

static void
on_fork(void *data)
{
    unsigned i;

    static const char *daemon_possible_paths[] = {
        "/usr/libexec/bluetooth/bluetoothd", // fedora/yocto-style
        "/usr/lib/bluetooth/bluetoothd", // arch-style
        "/usr/sbin/bluetoothd" // debian-style
    };

    const char *argv[] = {
        NULL, // waiting to be set
        "--nodetach",
        NULL
    };

    static const char *envp[] = {
        "BLUETOOTH_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket",
        NULL
    };

    for (i = 0; i < ARRAY_SIZE(daemon_possible_paths); i++) {
        argv[0] = daemon_possible_paths[i];
        SOL_INF("attempting to exec %s", daemon_possible_paths[i]);
        execvpe(argv[0], (char *const *)argv, (char *const *)envp);
    }

    SOL_INF("bluetooth daemon executable not found, aborting");
    _exit(EXIT_FAILURE);
}

static void
on_fork_exit(void *data, uint64_t pid, int status)
{
    SOL_DBG("bluetooth-daemon pid=%" PRIu64 " exited with status=%d",
        pid, status);

    if (status)
        sol_platform_linux_micro_inform_service_state
            (name, SOL_PLATFORM_SERVICE_STATE_FAILED);
    else
        sol_platform_linux_micro_inform_service_state
            (name, SOL_PLATFORM_SERVICE_STATE_INACTIVE);
}

static int
fork_run_do(void)
{
    if (fork_run)
        return 0;

    fork_run = sol_platform_linux_fork_run(on_fork, on_fork_exit, NULL);
    if (!fork_run) {
        sol_platform_linux_micro_inform_service_state
            (name, SOL_PLATFORM_SERVICE_STATE_FAILED);
        return -errno;
    }

    SOL_DBG("bluetooth-daemon started as pid=%" PRIu64,
        (uint64_t)sol_platform_linux_fork_run_get_pid(fork_run));

    sol_platform_linux_micro_inform_service_state
        (name, SOL_PLATFORM_SERVICE_STATE_ACTIVE);

    return 0;
}

static int
send_sig(int sig)
{
    pid_t pid = sol_platform_linux_fork_run_get_pid(fork_run);

    if (pid > 0) {
        if (kill(pid, sig) == 0)
            return 0;
    }
    return -errno;
}

static int
bluetooth_stop(const struct sol_platform_linux_micro_module *mod,
    const char *service,
    bool force_immediate)
{
    static const char *DBUS = "dbus";
    int err = 0;

    if (!fork_run)
        return 0;

    if (!force_immediate)
        err = send_sig(SIGTERM);
    else {
        sol_platform_linux_fork_run_stop(fork_run);
        fork_run = NULL;
    }

    sol_platform_del_service_monitor
        (on_dbus_service_state_changed, DBUS, NULL);

    return err;
}

static void
on_dbus_service_state_changed(void *data,
    const char *service,
    enum sol_platform_service_state state)
{
    if (state == SOL_PLATFORM_SERVICE_STATE_ACTIVE)
        fork_run_do();
    else if (state == SOL_PLATFORM_SERVICE_STATE_INACTIVE
        || state == SOL_PLATFORM_SERVICE_STATE_FAILED)
        bluetooth_stop(data, name, true);
}

static int
bluetooth_start(const struct sol_platform_linux_micro_module *mod,
    const char *service)
{
    int ret = 0;
    static const char *DBUS = "dbus";
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_ACTIVE;

    name = service;

    if (fork_run)
        return 0;

    ret = sol_platform_add_service_monitor
            (on_dbus_service_state_changed, DBUS, mod);
    if (ret >= 0)
        goto err;

    ret = sol_platform_start_service(DBUS);
    if (ret < 0) {
        SOL_WRN("D-Bus service is a dependency for bluetooth and could"
            " not be started");
        sol_platform_del_service_monitor
            (on_dbus_service_state_changed, DBUS, NULL);
        goto err;
    }

    state = sol_platform_get_service_state(DBUS);
    if (state == SOL_PLATFORM_SERVICE_STATE_ACTIVE)
        return fork_run_do();

err:
    sol_platform_linux_micro_inform_service_state
        (service, SOL_PLATFORM_SERVICE_STATE_FAILED);
    return ret;
}

static int
bluetooth_restart(const struct sol_platform_linux_micro_module *mod,
    const char *service)
{
    if (!fork_run)
        return bluetooth_start(mod, service);

    return send_sig(SIGHUP);
}

static int
bluetooth_init(const struct sol_platform_linux_micro_module *module,
    const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(BLUETOOTH,
    .name = "bluetooth",
    .init = bluetooth_init,
    .start = bluetooth_start,
    .stop = bluetooth_stop,
    .restart = bluetooth_restart,
    );
