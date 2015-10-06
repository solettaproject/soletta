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
#include <pthread.h>

#define MAGIC0 0x1234
#define MAGIC1 0xdead

static int read_magic_count;
static int read_magic[2];
static bool thrs_run = true;
static pthread_t main_thread;

static bool
timeout_dn(void *data)
{
    bool *val = data;

    ASSERT(pthread_self() == main_thread);
    return *val;
}

static bool
idler_dn(void *data)
{
    bool *val = data;

    ASSERT(pthread_self() == main_thread);
    return *val;
}

static bool
fd_watch_dn(void *data, int fd, unsigned int flags)
{
    bool *val = data;

    ASSERT(pthread_self() == main_thread);
    return *val;
}

static bool
stop_all(void *data)
{
    thrs_run = false;
    sol_quit();
    return false;
}

static void
ops_test_loop(void)
{
    struct sol_timeout *dt = NULL;
    struct sol_idle *di = NULL;
    struct sol_fd *df = NULL;
    bool tv, fv;
    int fds[2], cnt;

    cnt = 0;
    tv = true;
    fv = false;
    while (thrs_run) {
        sol_timeout_add(100, timeout_dn, &fv);
        sol_idle_add(idler_dn, &fv);
        if (cnt++ % 2 == 0) {
            dt = sol_timeout_add(5000, timeout_dn, &tv);
            di = sol_idle_add(idler_dn, &tv);
            pipe(fds);
            df = sol_fd_add(fds[0], SOL_FD_FLAGS_IN, fd_watch_dn, &tv);
        } else {
            if (dt)
                sol_timeout_del(dt);
            if (di)
                sol_idle_del(di);
            if (df)
                sol_fd_del(df);
            close(fds[0]);
            close(fds[1]);
        }
        usleep(500);
    }
}

#define THR_GEN(name)                                                   \
    static void *name(void *data)                                       \
    {                                                                   \
        ops_test_loop();                                                \
        return NULL;                                                    \
    }

THR_GEN(thr1_run);
THR_GEN(thr2_run);
THR_GEN(thr3_run);
THR_GEN(thr4_run);

#undef THR_GEN

static void *
thr5_run(void *data)
{
    int *fd = data;

    while (thrs_run) {
        int err, magic;

        usleep(100);
        magic = MAGIC0;
        err = write(*fd, &magic, sizeof(int));
        ASSERT_INT_EQ(err, sizeof(int));

        usleep(100);
        magic = MAGIC1;
        err = write(*fd, &magic, sizeof(int));
        ASSERT_INT_EQ(err, sizeof(int));

        usleep(1000);
    }
    return NULL;
}

static bool
on_fd(void *data, int fd, unsigned int active_flags)
{
    if (active_flags & SOL_FD_FLAGS_IN) {
        int err;
        err = read(fd, read_magic + read_magic_count, sizeof(int));
        ASSERT_INT_EQ(err, sizeof(int));
        read_magic_count = !read_magic_count;
    } else if (active_flags & SOL_FD_FLAGS_ERR) {
        fputs("fd error.\n", stderr);
    }
    return true;
}

int
main(int argc, char *argv[])
{
    pthread_t thr1, thr2, thr3, thr4, thr5;
    int fds[2];

    ASSERT(sol_init() == 0);

    pipe(fds);
    sol_fd_add(fds[0], SOL_FD_FLAGS_IN, on_fd, NULL);

    main_thread = pthread_self();
    pthread_create(&thr1, NULL, thr1_run, NULL);
    pthread_create(&thr2, NULL, thr2_run, NULL);
    pthread_create(&thr3, NULL, thr3_run, NULL);
    pthread_create(&thr4, NULL, thr4_run, NULL);

    pthread_create(&thr5, NULL, thr5_run, &fds[1]);

    ASSERT(sol_timeout_add(3000, stop_all, NULL));
    sol_run();

    pthread_join(thr1, NULL);
    pthread_join(thr2, NULL);
    pthread_join(thr3, NULL);
    pthread_join(thr4, NULL);
    pthread_join(thr5, NULL);

    sol_shutdown();

    return 0;
}
