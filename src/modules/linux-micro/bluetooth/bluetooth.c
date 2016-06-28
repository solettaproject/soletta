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
#include "sol-util-internal.h"

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

    for (i = 0; i < sol_util_array_size(daemon_possible_paths); i++) {
        argv[0] = daemon_possible_paths[i];
        SOL_INF("attempting to exec %s", daemon_possible_paths[i]);
        execvpe(argv[0], (char *const *)argv, (char *const *)envp);
    }

    SOL_INF("bluetooth daemon executable not found, aborting");
    sol_platform_linux_fork_run_exit(EXIT_FAILURE);
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
bluetooth_stop(const struct sol_platform_linux_micro_module *mod,
    const char *service,
    bool force_immediate)
{
    static const char *DBUS = "dbus";
    int err = 0;

    if (!fork_run)
        return 0;

    if (!force_immediate)
        err = sol_platform_linux_fork_run_send_signal(fork_run, SIGTERM);
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
    SOL_INT_CHECK_GOTO(ret, < 0, err);

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

    return 0; /* wait for dep to activate */

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

    return sol_platform_linux_fork_run_send_signal(fork_run, SIGHUP);
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
