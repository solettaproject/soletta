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

#include <fcntl.h>
#include <unistd.h>

#include <glib-unix.h>

#include "sol-glib-integration.h"
#include "sol-platform-linux.h"

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
