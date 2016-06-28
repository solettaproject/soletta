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
fd_watch_dn(void *data, int fd, uint32_t flags)
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
            ASSERT(pipe(fds) == 0);
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
on_fd(void *data, int fd, uint32_t active_flags)
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

    ASSERT(pipe(fds) == 0);
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
