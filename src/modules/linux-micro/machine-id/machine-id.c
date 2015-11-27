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

static bool done = false;
static const char *name;

static void on_fstab_service_state_changed(void *data,
    const char *service,
    enum sol_platform_service_state state);

static int
validate_machine_id(char id[SOL_STATIC_ARRAY_SIZE(33)])
{
    id[32] = '\0';

    if (!sol_util_uuid_str_valid(id))
        return -EINVAL;

    return 0;
}

static int
write_machine_id(const char *path, char id[SOL_STATIC_ARRAY_SIZE(33)])
{
    SOL_NULL_CHECK(path, -EINVAL);

    /* add trailing '\n' */
    return sol_util_write_file(path, "%s\n", id);
}

static int
run_do(void)
{
    static const char *etc_path = "/etc/machine-id",
    *run_path = "/run/machine-id";
    char id[33];
    int r;

    r = sol_util_read_file(etc_path, "%32s", id);
    if (r < 0) {
        /* We can only tolerate the file not existing or being
         * malformed on /etc/, otherwise it's got more serious
         * problems and it's better to fail */
        if (r != -ENOENT && r != EOF)
            goto err;
    } else {
        r = validate_machine_id(id);
        /* return if OK here */
        SOL_INT_CHECK(r, == 0, 0);
    }

    r = sol_util_uuid_gen(false, false, id);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    if (write_machine_id(etc_path, id) >= 0)
        goto end;

    /* fallback to /run/ */
    r = write_machine_id(run_path, id);
    SOL_INT_CHECK_GOTO(r, < 0, err);

end:
    sol_platform_linux_micro_inform_service_state
        (name, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    done = true;
    return 0;

err:
    sol_platform_linux_micro_inform_service_state
        (name, SOL_PLATFORM_SERVICE_STATE_FAILED);

    return r;
}

static void
on_fstab_service_state_changed(void *data,
    const char *service,
    enum sol_platform_service_state state)
{
    if (state == SOL_PLATFORM_SERVICE_STATE_ACTIVE)
        run_do();
    else if (state == SOL_PLATFORM_SERVICE_STATE_INACTIVE
        || state == SOL_PLATFORM_SERVICE_STATE_FAILED)
        sol_platform_linux_micro_inform_service_state
            (name, SOL_PLATFORM_SERVICE_STATE_FAILED);
}

static int
machine_id_start(const struct sol_platform_linux_micro_module *mod,
    const char *service)
{
    int ret = 0;
    static const char *FSTAB = "fstab";
    enum sol_platform_service_state state = SOL_PLATFORM_SERVICE_STATE_ACTIVE;

    name = service;

    if (done)
        return 0;

    /* If fstab is present and functional, wait for it to go up for
     * this to run. If it's not, try to do machine-id's business
     * nonetheless */
    ret = sol_platform_add_service_monitor
            (on_fstab_service_state_changed, FSTAB, mod);
    if (ret < 0)
        return run_do();
    ret = sol_platform_start_service(FSTAB);
    if (ret < 0) {
        sol_platform_del_service_monitor
            (on_fstab_service_state_changed, FSTAB, NULL);
        return run_do();
    }

    state = sol_platform_get_service_state(FSTAB);
    if (state == SOL_PLATFORM_SERVICE_STATE_ACTIVE)
        return run_do();

    return 0; /* wait for dep to activate */
}

static int
machine_id_stop(const struct sol_platform_linux_micro_module *mod,
    const char *service,
    bool force_immediate)
{
    static const char *FSTAB = "fstab";

    sol_platform_del_service_monitor
        (on_fstab_service_state_changed, FSTAB, NULL);

    sol_platform_linux_micro_inform_service_state
        (name, SOL_PLATFORM_SERVICE_STATE_INACTIVE);

    return 0;
}

static int
machine_id_restart(const struct sol_platform_linux_micro_module *mod,
    const char *service)
{
    if (!done)
        return machine_id_start(mod, service);

    return 0;
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
