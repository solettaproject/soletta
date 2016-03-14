/*
 * This file is part of the Soletta Project
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

#include <nanokernel.h>

#include "sol-mainloop-common.h"
#include "sol-mainloop-impl.h"
#include "sol-mainloop-zephyr.h"

static nano_thread_id_t main_thread_id;
static struct nano_sem _sol_mainloop_lock;

int
sol_mainloop_zephyr_common_init(void)
{
    main_thread_id = sys_thread_self_get();
    nano_sem_init(&_sol_mainloop_lock);
    nano_sem_give(&_sol_mainloop_lock);

    return 0;
}

void
sol_mainloop_impl_lock(void)
{
    nano_sem_take(&_sol_mainloop_lock, TICKS_UNLIMITED);
}

void
sol_mainloop_impl_unlock(void)
{
    nano_sem_give(&_sol_mainloop_lock);
}

bool
sol_mainloop_impl_main_thread_check(void)
{
    return main_thread_id == sys_thread_self_get();
}

void
sol_mainloop_impl_main_thread_notify(void)
{
    static const struct mainloop_event me = {
        .cb = NULL,
        .data = NULL
    };

    sol_mainloop_event_post(&me);
}

void
sol_mainloop_impl_platform_shutdown(void)
{
    sol_mainloop_common_source_shutdown();
}

static inline int32_t
ticks_until_next_timeout(void)
{
    struct timespec ts;
    bool ret;

    sol_mainloop_impl_lock();
    ret = (sol_mainloop_common_idler_first() != NULL);
    sol_mainloop_impl_unlock();
    if (ret)
        return TICKS_NONE;

    sol_mainloop_impl_lock();
    ret = sol_mainloop_common_timespec_first(&ts);
    sol_mainloop_impl_unlock();
    if (!ret)
        return TICKS_UNLIMITED;

    return ts.tv_sec * sys_clock_ticks_per_sec +
        ((long long)sys_clock_ticks_per_sec * ts.tv_nsec) / NSEC_PER_SEC;
}

void
sol_mainloop_impl_iter(void)
{
    int32_t sleeptime;

    sol_mainloop_common_timeout_process();

    sleeptime = ticks_until_next_timeout();
    sol_mainloop_events_process(sleeptime);

    sol_mainloop_common_idler_process();
}
