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

#pragma once

#include "sol-common-buildopts.h"

#ifndef SOL_PLATFORM_ZEPHYR
#include <time.h>
#endif

#include "sol-util-internal.h"
#include "sol-vector.h"

struct sol_timeout_common {
    struct timespec timeout;
    struct timespec expire;
    const void *data;
    bool (*cb)(void *data);
    bool remove_me;
};

struct sol_idler_common {
    const void *data;
    bool (*cb)(void *data);
    enum { idler_ready, idler_deleted, idler_ready_on_next_iteration } status;
};

/* must be called with mainloop lock held */
static inline void
sol_mainloop_ptr_vector_steal(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
#ifdef THREADS
    *to = *from;
    sol_ptr_vector_init(from);
#endif
}

/* must be called with mainloop lock held */
static inline void
sol_ptr_vector_update(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
#ifdef THREADS
    void *itr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (from, itr, i) {
        /* FIXME: Handle when it fails properly */
        (void)sol_ptr_vector_append(to, itr);
    }

    sol_ptr_vector_clear(from);
#endif
}

bool sol_mainloop_common_loop_check(void);
void sol_mainloop_common_loop_set(bool val);
void sol_mainloop_common_timeout_process(void);
void sol_mainloop_common_idler_process(void);
bool sol_mainloop_common_timespec_first(struct timespec *ts);
struct sol_idler_common *sol_mainloop_common_idler_first(void);

bool sol_mainloop_common_source_prepare(void);
bool sol_mainloop_common_source_get_next_timeout(struct timespec *timeout);
bool sol_mainloop_common_source_check(void);
void sol_mainloop_common_source_dispatch(void);
void sol_mainloop_common_source_shutdown(void);

/* thread-related platform specific functions */
bool sol_mainloop_impl_main_thread_check(void);
void sol_mainloop_impl_main_thread_notify(void);
void sol_mainloop_impl_lock(void);
void sol_mainloop_impl_unlock(void);

/* platform specific mainloop functions */
int sol_mainloop_impl_platform_init(void);
void sol_mainloop_impl_platform_shutdown(void);
void sol_mainloop_impl_iter(void);

static inline void
sol_mainloop_common_main_thread_check_notify(void)
{
#ifdef THREADS
    if (!sol_mainloop_impl_main_thread_check())
        sol_mainloop_impl_main_thread_notify();
#endif
}
