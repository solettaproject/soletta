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

#define DEFAULT_USLEEP_TIME 10000

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

static inline uint32_t
sleeptime_until_next_timeout(void)
{
    struct timespec ts;
    uint32_t sleeptime = DEFAULT_USLEEP_TIME;
    bool ret;

    sol_mainloop_impl_lock();
    ret = sol_mainloop_common_timespec_first(&ts);
    sol_mainloop_impl_unlock();

    if (ret) {
        if (ts.tv_sec < 0)
            sleeptime = 0;
        else
            sleeptime = ts.tv_sec * SOL_USEC_PER_SEC + ts.tv_nsec / SOL_NSEC_PER_USEC;
    }

    return sleeptime;
}

void
sol_mainloop_impl_iter(void)
{
    msg_t msg;
    uint32_t sleeptime;

    sol_mainloop_common_timeout_process();
    sol_mainloop_common_idler_process();
    sol_mainloop_common_timeout_process();

    if (!sol_mainloop_common_loop_check())
        return;

    sleeptime = sleeptime_until_next_timeout();
    if (xtimer_msg_receive_timeout(&msg, sleeptime) > 0)
        sol_interrupt_scheduler_process(&msg);
}
