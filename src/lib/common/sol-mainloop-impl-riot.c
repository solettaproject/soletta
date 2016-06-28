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

/* TODO: add thread support?? */

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Riot includes
#include <sched.h>
#include <xtimer.h>

#ifdef THREADS
#include <mutex.h>
#include <thread.h>
#endif

#include "sol-mainloop-common.h"
#include "sol-interrupt_scheduler_riot.h"
#include "sol-mainloop-impl.h"
#include "sol-vector.h"

#define MSG_BUFFER_SIZE 32
static msg_t msg_buffer[MSG_BUFFER_SIZE];

#ifdef THREADS
static mutex_t _lock;
static kernel_pid_t _main_pid;
#endif

void
sol_mainloop_impl_lock(void)
{
#ifdef THREADS
    mutex_lock(&_lock);
#endif
}

void
sol_mainloop_impl_unlock(void)
{
#ifdef THREADS
    mutex_unlock(&_lock);
#endif
}

bool
sol_mainloop_impl_main_thread_check(void)
{
#ifdef THREADS
    return thread_getpid() == _main_pid;
#else
    return true;
#endif
}

void
sol_mainloop_impl_main_thread_notify(void)
{
}

int
sol_mainloop_impl_platform_init(void)
{
#ifdef THREADS
    mutex_init(&_lock);
    _main_pid = thread_getpid();
#endif
    sol_interrupt_scheduler_set_pid(sched_active_pid);
    msg_init_queue(msg_buffer, MSG_BUFFER_SIZE);
    return 0;
}

void
sol_mainloop_impl_platform_shutdown(void)
{
#ifdef THREADS
    _main_pid = KERNEL_PID_UNDEF;
#endif
    sol_mainloop_common_source_shutdown();
}

static inline bool
sleeptime_until_next_timeout(uint32_t *sleeptime)
{
    struct timespec ts;
    bool ret;

    sol_mainloop_impl_lock();
    ret = sol_mainloop_common_idler_first() != NULL;
    sol_mainloop_impl_unlock();
    if (ret) {
        *sleeptime = 0;
        return true;
    }

    sol_mainloop_impl_lock();
    ret = sol_mainloop_common_timespec_first(&ts);
    sol_mainloop_impl_unlock();
    if (!ret)
        return false;

    *sleeptime = ts.tv_sec * SOL_UTIL_USEC_PER_SEC + ts.tv_nsec / SOL_UTIL_NSEC_PER_USEC;
    return true;
}

void
sol_mainloop_impl_iter(void)
{
    msg_t msg;
    uint32_t sleeptime;
    int msg_count;
    bool ret;

    sol_mainloop_common_timeout_process();

    /* Process all available msgs first */
    msg_count = msg_avail();
    while (msg_count--) {
        msg_receive(&msg);
        sol_interrupt_scheduler_process(&msg);
    }

    /* Then we check if we need to sleep for a time or indefinetely */
    ret = sleeptime_until_next_timeout(&sleeptime);
    if (ret) {
        if (xtimer_msg_receive_timeout(&msg, sleeptime) > 0)
            sol_interrupt_scheduler_process(&msg);
    } else {
        msg_receive(&msg);
        sol_interrupt_scheduler_process(&msg);
    }

    sol_mainloop_common_idler_process();
}
