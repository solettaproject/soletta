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

#include "sol-mainloop.h"

#include "test.h"

#define TEST_MAINLOOP_MAIN_FN test_mainloop_main
int TEST_MAINLOOP_MAIN_FN(int argc, char *argv[]);
#include "test-mainloop.c"

#ifdef SOL_PLATFORM_LINUX
#define TEST_MAINLOOP_LINUX_MAIN_FN test_mainloop_linux_main
int TEST_MAINLOOP_LINUX_MAIN_FN(int argc, char *argv[]);
#include "test-mainloop-linux.c"
#endif

/* implementations may create timers, fds and others internally */
#ifdef SOL_PLATFORM_LINUX
/* sol-mainloop-impl-posix.c adds pipe to notify main thread */
#ifdef MAINLOOP_POSIX
#define BASE_CALL_COUNT_FD_ADD 1
#define BASE_CALL_COUNT_FD_DEL 1
#else
#define BASE_CALL_COUNT_FD_ADD 0
#define BASE_CALL_COUNT_FD_DEL 0
#endif
#else
#define BASE_CALL_COUNT_FD_DEL 0
#endif

static int _call_count_init;
static int _call_count_shutdown;
static int _call_count_run;
static int _call_count_quit;
static int _call_count_timeout_add;
static int _call_count_timeout_del;
static int _call_count_idle_add;
static int _call_count_idle_del;

#ifdef SOL_MAINLOOP_FD_ENABLED
static int _call_count_fd_add;
static int _call_count_fd_del;
static int _call_count_fd_set_flags;
static int _call_count_fd_get_flags;
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
static int _call_count_child_watch_add;
static int _call_count_child_watch_del;
#endif

static int _call_count_source_add;
static int _call_count_source_del;
static int _call_count_source_get_data;

static void
call_count_reset(void)
{
    _call_count_init = 0;
    _call_count_shutdown = 0;
    _call_count_run = 0;
    _call_count_quit = 0;
    _call_count_timeout_add = 0;
    _call_count_timeout_del = 0;
    _call_count_idle_add = 0;
    _call_count_idle_del = 0;

#ifdef SOL_MAINLOOP_FD_ENABLED
    _call_count_fd_add = 0;
    _call_count_fd_del = 0;
    _call_count_fd_set_flags = 0;
    _call_count_fd_get_flags = 0;
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    _call_count_child_watch_add = 0;
    _call_count_child_watch_del = 0;
#endif

    _call_count_source_add = 0;
    _call_count_source_del = 0;
    _call_count_source_get_data = 0;
}

static int
wrapper_ml_init(void)
{
    _call_count_init++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->init();
}

static void
wrapper_ml_shutdown(void)
{
    _call_count_shutdown++;
    SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->shutdown();
}

static void
wrapper_ml_run(void)
{
    _call_count_run++;
    SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->run();
}

static void
wrapper_ml_quit(void)
{
    _call_count_quit++;
    SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->quit();
}

static void *
wrapper_ml_timeout_add(uint32_t timeout_ms, bool (*cb)(void *data), const void *data)
{
    _call_count_timeout_add++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->timeout_add(timeout_ms, cb, data);
}

static bool
wrapper_ml_timeout_del(void *handle)
{
    _call_count_timeout_del++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->timeout_del(handle);
}

static void *
wrapper_ml_idle_add(bool (*cb)(void *data), const void *data)
{
    _call_count_idle_add++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->idle_add(cb, data);
}

static bool
wrapper_ml_idle_del(void *handle)
{
    _call_count_idle_del++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->idle_del(handle);
}

#ifdef SOL_MAINLOOP_FD_ENABLED
static void *
wrapper_ml_fd_add(int fd, uint32_t flags, bool (*cb)(void *data, int fd, uint32_t active_flags), const void *data)
{
    _call_count_fd_add++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->fd_add(fd, flags, cb, data);
}

static bool
wrapper_ml_fd_del(void *handle)
{
    _call_count_fd_del++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->fd_del(handle);
}

static bool
wrapper_ml_fd_set_flags(void *handle, uint32_t flags)
{
    _call_count_fd_set_flags++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->fd_set_flags(handle, flags);
}

static uint32_t
wrapper_ml_fd_get_flags(const void *handle)
{
    _call_count_fd_get_flags++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->fd_get_flags(handle);
}
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
static void *
wrapper_ml_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data)
{
    _call_count_child_watch_add++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->child_watch_add(pid, cb, data);
}

static bool
wrapper_ml_child_watch_del(void *handle)
{
    _call_count_child_watch_del++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->child_watch_del(handle);
}
#endif

static void *
wrapper_ml_source_add(const struct sol_mainloop_source_type *type, const void *data)
{
    _call_count_source_add++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->source_add(type, data);
}

static void
wrapper_ml_source_del(void *handle)
{
    _call_count_source_del++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->source_del(handle);
}

static void *
wrapper_ml_source_get_data(const void *handle)
{
    _call_count_source_get_data++;
    return SOL_MAINLOOP_IMPLEMENTATION_DEFAULT->source_get_data(handle);
}

static const struct sol_mainloop_implementation wrapper_ml = {
    SOL_SET_API_VERSION(.api_version = SOL_MAINLOOP_IMPLEMENTATION_API_VERSION, )
    .init = wrapper_ml_init,
    .shutdown =  wrapper_ml_shutdown,
    .run =  wrapper_ml_run,
    .quit =  wrapper_ml_quit,
    .timeout_add =  wrapper_ml_timeout_add,
    .timeout_del =  wrapper_ml_timeout_del,
    .idle_add =  wrapper_ml_idle_add,
    .idle_del =  wrapper_ml_idle_del,

#ifdef SOL_MAINLOOP_FD_ENABLED
    .fd_add =  wrapper_ml_fd_add,
    .fd_del =  wrapper_ml_fd_del,
    .fd_set_flags =  wrapper_ml_fd_set_flags,
    .fd_get_flags =  wrapper_ml_fd_get_flags,
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    .child_watch_add =  wrapper_ml_child_watch_add,
    .child_watch_del =  wrapper_ml_child_watch_del,
#endif

    .source_add =  wrapper_ml_source_add,
    .source_del =  wrapper_ml_source_del,
    .source_get_data =  wrapper_ml_source_get_data,
};

int
main(int argc, char *argv[])
{
    int r;

    ASSERT(sol_mainloop_set_implementation(&wrapper_ml));

    /* test-mainloop.c */
    call_count_reset();

    r = test_mainloop_main(argc, argv);
    ASSERT_INT_EQ(r, 0);

    ASSERT_INT_EQ(_call_count_init, 1);
    ASSERT_INT_EQ(_call_count_shutdown, 1);
    ASSERT_INT_EQ(_call_count_run, 1);
    ASSERT_INT_EQ(_call_count_quit, 1);
    ASSERT_INT_EQ(_call_count_timeout_add, 5);
    ASSERT_INT_EQ(_call_count_timeout_del, 1);
    ASSERT_INT_EQ(_call_count_idle_add, 13);
    ASSERT_INT_EQ(_call_count_idle_del, 1);

#ifdef SOL_MAINLOOP_FD_ENABLED
    ASSERT_INT_EQ(_call_count_fd_add, 0 + BASE_CALL_COUNT_FD_ADD);
    ASSERT_INT_EQ(_call_count_fd_del, 0 + BASE_CALL_COUNT_FD_DEL);
    ASSERT_INT_EQ(_call_count_fd_set_flags, 0);
    ASSERT_INT_EQ(_call_count_fd_get_flags, 0);
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    ASSERT_INT_EQ(_call_count_child_watch_add, 0);
    ASSERT_INT_EQ(_call_count_child_watch_del, 0);
#endif

    ASSERT_INT_EQ(_call_count_source_add, 0);
    ASSERT_INT_EQ(_call_count_source_del, 0);
    ASSERT_INT_EQ(_call_count_source_get_data, 0);

    /* test-mainloop-linux.c */
#ifdef SOL_PLATFORM_LINUX
    call_count_reset();

    r = test_mainloop_linux_main(argc, argv);
    ASSERT_INT_EQ(r, 0);

    ASSERT_INT_EQ(_call_count_init, 1);
    ASSERT_INT_EQ(_call_count_shutdown, 1);
    ASSERT_INT_EQ(_call_count_run, 1);
    ASSERT_INT_EQ(_call_count_quit, 1);
    ASSERT_INT_EQ(_call_count_timeout_add, 2);
    ASSERT_INT_EQ(_call_count_timeout_del, 0);
    ASSERT_INT_EQ(_call_count_idle_add, 2);
    ASSERT_INT_EQ(_call_count_idle_del, 0);

#ifdef SOL_MAINLOOP_FD_ENABLED
    ASSERT_INT_EQ(_call_count_fd_add, 1 + BASE_CALL_COUNT_FD_ADD);
    ASSERT_INT_EQ(_call_count_fd_del, 0 + BASE_CALL_COUNT_FD_DEL);
    ASSERT_INT_EQ(_call_count_fd_set_flags, 0);
    ASSERT_INT_EQ(_call_count_fd_get_flags, 0);
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
    ASSERT_INT_EQ(_call_count_child_watch_add, 0);
    ASSERT_INT_EQ(_call_count_child_watch_del, 0);
#endif

    ASSERT_INT_EQ(_call_count_source_add, 0);
    ASSERT_INT_EQ(_call_count_source_del, 0);
    ASSERT_INT_EQ(_call_count_source_get_data, 0);
#endif

    return 0;
}
