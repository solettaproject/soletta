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

#include <nanokernel.h>

#include "sol-mainloop-common.h"
#include "sol-mainloop-impl.h"
#include "sol-mainloop-zephyr.h"
#include "sol-util.h"

struct me_fifo_entry {
    void *reserved_for_fifo;
    struct mainloop_event me;
};

static struct nano_fifo _sol_mainloop_pending_events;
static struct nano_fifo _sol_mainloop_free_events;

#define MAX_QUEUED_EVENTS 8
#define EVENTS_BUF_SIZE (MAX_QUEUED_EVENTS * sizeof(struct me_fifo_entry))
static struct me_fifo_entry _events[EVENTS_BUF_SIZE];

static nano_thread_id_t main_thread_id;
static struct nano_sem _sol_mainloop_lock;

int
sol_mainloop_impl_platform_init(void)
{
    int i;

    main_thread_id = sys_thread_self_get();
    nano_sem_init(&_sol_mainloop_lock);
    nano_sem_give(&_sol_mainloop_lock);

    nano_fifo_init(&_sol_mainloop_pending_events);
    nano_fifo_init(&_sol_mainloop_free_events);
    for (i = 0; i < sol_util_array_size(_events); i++) {
        struct me_fifo_entry *mfe;

        mfe = &_events[i];
        nano_fifo_put(&_sol_mainloop_free_events, mfe);
    }

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

int
sol_mainloop_event_post(const struct mainloop_event *me)
{
    struct me_fifo_entry *mfe;

    mfe = nano_fifo_get(&_sol_mainloop_free_events, TICKS_NONE);
    SOL_NULL_CHECK(mfe, -ENOMEM);

    mfe->me = *me;
    nano_fifo_put(&_sol_mainloop_pending_events, mfe);

    return 0;
}

void
sol_mainloop_events_process(int32_t sleeptime)
{
    struct me_fifo_entry *mfe;

    mfe = nano_task_fifo_get(&_sol_mainloop_pending_events, sleeptime);
    if (!mfe)
        return;

    do {
        if (mfe->me.cb)
            mfe->me.cb((void *)mfe->me.data);
        nano_task_fifo_put(&_sol_mainloop_free_events, mfe);
    } while ((mfe = nano_task_fifo_get(&_sol_mainloop_pending_events, TICKS_NONE)));
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
