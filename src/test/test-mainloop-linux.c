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

#include <stdbool.h>
#include <inttypes.h>

#include "sol-mainloop.h"

#include "test.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define MAGIC0 0x1234
#define MAGIC1 0xdead

static int read_magic_count;
static int read_magic[2];
static int timeout_count;
static int idler_count1, idler_count2;

static int sigterm_fds[2];

static void
request_sigterm_if_complete(void)
{
    if (timeout_count == 2 && idler_count1 == 2 && idler_count2 == 2) {
        /* Signal the child process by closing the write end of the pipe. */
        close(sigterm_fds[1]);
    }
}

static bool
on_idle_renew_twice(void *data)
{
    int *count = data;

    (*count)++;
    request_sigterm_if_complete();
    return (*count) < 2;
}

static bool
on_timeout_renew_twice(void *data)
{
    timeout_count++;
    if (timeout_count == 1)
        sol_idle_add(on_idle_renew_twice, &idler_count2);

    request_sigterm_if_complete();
    return timeout_count < 2;
}

static bool
watchdog(void *data)
{
    fputs("should never reach here. failing to catch SIGTERM?\n", stderr);
    abort();
    return false;
}

static bool
on_fd(void *data, int fd, uint32_t active_flags)
{
    if (active_flags & SOL_FD_FLAGS_IN) {
        int err;

        ASSERT(read_magic_count < 2);

        err = read(fd, read_magic + read_magic_count, sizeof(int));
        ASSERT_INT_EQ(err, sizeof(int));
        read_magic_count++;
    } else if (active_flags & SOL_FD_FLAGS_ERR) {
        fputs("fd error.\n", stderr);
    }
    return true;
}

int
main(int argc, char *argv[])
{
    int err, fds[2];
    pid_t pid;

    err = pipe(fds);
    ASSERT_INT_EQ(err, 0);

    err = pipe(sigterm_fds);
    ASSERT_INT_EQ(err, 0);

    /* test fd by writing from another process */
    pid = fork();
    if (pid == 0) {
        int magic;

        usleep(100);
        magic = MAGIC0;
        err = write(fds[1], &magic, sizeof(int));
        ASSERT_INT_EQ(err, sizeof(int));

        usleep(100);
        magic = MAGIC1;
        err = write(fds[1], &magic, sizeof(int));
        ASSERT_INT_EQ(err, sizeof(int));

        usleep(100);
        close(fds[1]);
        _exit(EXIT_SUCCESS);
    }

    /* test if we handle graceful termination with SIGTERM */
    pid = fork();
    if (pid == 0) {
        int ignored;
        close(sigterm_fds[1]);
        /* Blocks until the parent close the write end of the pipe. */
        read(sigterm_fds[0], &ignored, sizeof(ignored));
        kill(getppid(), SIGTERM);
        _exit(EXIT_SUCCESS);
    }

    err = sol_init();
    ASSERT_INT_EQ(err, 0);

    sol_fd_add(fds[0], SOL_FD_FLAGS_IN, on_fd, NULL);
    sol_timeout_add(1, on_timeout_renew_twice, NULL);
    sol_timeout_add(10000, watchdog, NULL);
    sol_idle_add(on_idle_renew_twice, &idler_count1);
    sol_run();

    ASSERT_INT_EQ(timeout_count, 2);
    ASSERT_INT_EQ(idler_count1, 2);
    ASSERT_INT_EQ(idler_count2, 2);
    ASSERT_INT_EQ(read_magic[0], MAGIC0);
    ASSERT_INT_EQ(read_magic[1], MAGIC1);

    /* all children must be collected by the library, so -1 should be
     * returned.  But GLib doesn't collect PIDs not created with their
     * API, then just print a warning.
     */
    pid = waitpid(-1, NULL, WNOHANG);
    if (pid > -1)
        fprintf(stderr, "uncollected child process: %" PRIu64 "\n", (uint64_t)pid);

    sol_shutdown();

    return 0;
}
