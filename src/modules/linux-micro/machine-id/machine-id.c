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
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-machine-id");

#include "sol-mainloop.h"
#include "sol-platform-linux-micro.h"
#include "sol-util.h"

static struct sol_platform_linux_fork_run *fork_run;
static const char *name;

static void on_fstab_service_state_changed(void *data,
    const char *service,
    enum sol_platform_service_state state);

static int
validate_machine_id(char id[37])
{
    /* The stored machine-UUID we expect is at non-hyphenated form */
    if (id[32] != '\n')
        return -EINVAL;

    id[32] = 0;

    if (!sol_util_uuid_str_valid(id))
        return -EINVAL;

    id[32] = '\n';
    id[33] = 0;

    return 0;
}

static int
write_machine_id(const char *path, char id[37])
{
    SOL_NULL_CHECK(path, -EINVAL);

    /* add trailing '\n' */
    return sol_util_write_file(path, "%s\n", id);
}

static void
on_fork(void *data)
{
    const char *etc_path, *run_path;
    char id[37];
    int r;

    etc_path = "/etc/machine-id";
    run_path = "/run/machine-id";

    r = sol_util_read_file(etc_path, "%37c", id);
    if (r < 0) {
        /* We can only tolerate the file not existing or being
         * malformed on /etc/, otherwise it's got more serious
         * problems and it's better to fail */
        if (r != -ENOENT && r != EOF)
            goto err;
    } else {
        r = validate_machine_id(id);
        /* return if OK here */
        SOL_INT_CHECK(r, == 0);
    }

    r = sol_util_uuid_gen(false, true, id);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    if (write_machine_id(etc_path, id) >= 0)
        return;

    /* fallback to /run/ */
    r = write_machine_id(run_path, id);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return;

err:
    sol_platform_linux_fork_run_exit(EXIT_FAILURE);
}

static void
on_fork_exit(void *data, uint64_t pid, int status)
{
    SOL_DBG("machine_id-daemon pid=%" PRIu64 " exited with status=%d",
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

    SOL_DBG("machine-id-service started as pid=%" PRIu64,
        (uint64_t)sol_platform_linux_fork_run_get_pid(fork_run));

    sol_platform_linux_micro_inform_service_state
        (name, SOL_PLATFORM_SERVICE_STATE_ACTIVE);

    return 0;
}

static int
machine_id_stop(const struct sol_platform_linux_micro_module *mod,
    const char *service,
    bool force_immediate)
{
    static const char *FSTAB = "fstab";
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
        (on_fstab_service_state_changed, FSTAB, NULL);

    return err;
}

static void
on_fstab_service_state_changed(void *data,
    const char *service,
    enum sol_platform_service_state state)
{
    if (state == SOL_PLATFORM_SERVICE_STATE_ACTIVE)
        fork_run_do();
    else if (state == SOL_PLATFORM_SERVICE_STATE_INACTIVE
        || state == SOL_PLATFORM_SERVICE_STATE_FAILED)
        machine_id_stop(data, name, true);
}

static int
machine_id_start(const struct sol_platform_linux_micro_module *mod,
    const char *service)
{
    int ret = 0;
    static const char *FSTAB = "fstab";
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_ACTIVE;

    name = service;

    if (fork_run)
        return 0;

    ret = sol_platform_add_service_monitor
            (on_fstab_service_state_changed, FSTAB, mod);
    SOL_INT_CHECK_GOTO(ret, < 0, err);

    ret = sol_platform_start_service(FSTAB);
    if (ret < 0) {
        SOL_WRN("fstab service is a dependency for machine-id and could"
            " not be started");
        sol_platform_del_service_monitor
            (on_fstab_service_state_changed, FSTAB, NULL);
        goto err;
    }

    state = sol_platform_get_service_state(FSTAB);
    if (state == SOL_PLATFORM_SERVICE_STATE_ACTIVE)
        return fork_run_do();

    return 0; /* wait for dep to activate */

err:
    sol_platform_linux_micro_inform_service_state
        (service, SOL_PLATFORM_SERVICE_STATE_FAILED);
    return ret;
}

static int
machine_id_restart(const struct sol_platform_linux_micro_module *mod,
    const char *service)
{
    if (!fork_run)
        return machine_id_start(mod, service);

    return sol_platform_linux_fork_run_send_signal(fork_run, SIGHUP);
}

static int
machine_id_init(const struct sol_platform_linux_micro_module *module,
    const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(MACHINE_ID,
    .name = "machine_id",
    .init = machine_id_init,
    .start = machine_id_start,
    .stop = machine_id_stop,
    .restart = machine_id_restart,
    );
