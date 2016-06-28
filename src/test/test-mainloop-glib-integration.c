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

#include <fcntl.h>
#include <unistd.h>

#include <glib-unix.h>

#include "sol-glib-integration.h"
#include "sol-platform-linux.h"
#include "soletta.h"

#include "test.h"

static bool did_idle = false;
static bool did_timeout = false;
static bool did_fd = false;
static int pfd[2];
static struct sol_platform_linux_fork_run *fork_run;

static void
check_done(void)
{
    if (did_idle && did_timeout && did_fd)
        sol_quit();
}

static gboolean
on_idle(gpointer data)
{
    did_idle = true;
    SOL_DBG("did idle");
    check_done();
    return false;
}

static gboolean
on_timeout(gpointer data)
{
    did_timeout = true;
    SOL_DBG("did timeout");
    check_done();
    return false;
}

static gboolean
on_fd(gint fd, GIOCondition cond, gpointer data)
{
    did_fd = true;
    SOL_DBG("did fd=%d, cond=%#x", fd, cond);
    check_done();
    return false;
}

static bool
on_watchdog(void *data)
{
    SOL_WRN("watchdog expired - mainloop integration failed");
    sol_quit_with_code(EXIT_FAILURE);
    return false;
}

static void
on_fork(void *data)
{
    char c = 0xff;

    while (write(pfd[1], &c, 1) < 0)
        ;
}

static void
on_child_exit(void *data, uint64_t pid, int status)
{
    fork_run = NULL;
}

static void
startup(void)
{
    guint id;

    if (pipe2(pfd, O_CLOEXEC) < 0) {
        SOL_WRN("pipe()");
        goto error;
    }

    if (!sol_glib_integration()) {
        SOL_WRN("sol_glib_integration()");
        goto error;
    }

    fork_run = sol_platform_linux_fork_run(on_fork, on_child_exit, NULL);
    if (!fork_run) {
        SOL_WRN("sol_platform_linux_fork_run()");
        goto error;
    }

    id = g_idle_add(on_idle, NULL);
    if (id == 0) {
        SOL_WRN("g_idle_add()");
        goto error;
    }

    id = g_timeout_add(100, on_timeout, NULL);
    if (id == 0) {
        SOL_WRN("g_timeout_add()");
        goto error;
    }

    id = g_unix_fd_add(pfd[0], G_IO_IN, on_fd, NULL);
    if (id == 0) {
        SOL_WRN("g_unix_fd_add()");
        goto error;
    }

    sol_timeout_add(5000, on_watchdog, NULL);
    return;

error:
    sol_quit_with_code(EXIT_FAILURE);
    return;
}

static void
shutdown(void)
{
    if (!did_idle)
        SOL_WRN("failed to do idle");
    if (!did_timeout)
        SOL_WRN("failed to do timeout");
    if (!did_fd)
        SOL_WRN("failed to do fd");

    if (fork_run)
        sol_platform_linux_fork_run_stop(fork_run);

    if (pfd[0] > 0) {
        close(pfd[0]);
        close(pfd[1]);
    }

    if (!did_idle || !did_timeout || !did_fd)
        exit(EXIT_FAILURE);
}

SOL_MAIN_DEFAULT(startup, shutdown);
