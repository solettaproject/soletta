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
#include <riotos/sched.h>
#include <vtimer.h>

#include "sol-mainloop-common.h"
#include "sol-interrupt_scheduler_riot.h"
#include "sol-mainloop-impl.h"
#include "sol-vector.h"

#define DEFAULT_USLEEP_TIME 10000

#define MSG_BUFFER_SIZE 32
static msg_t msg_buffer[MSG_BUFFER_SIZE];

void
sol_mainloop_impl_lock(void)
{
}

void
sol_mainloop_impl_unlock(void)
{
}

bool
sol_mainloop_impl_main_thread_check(void)
{
    return true;
}

void
sol_mainloop_impl_main_thread_notify(void)
{
}

int
sol_mainloop_impl_init(void)
{
    sol_mainloop_common_init();
    sol_interrupt_scheduler_set_pid(sched_active_pid);
    msg_init_queue(msg_buffer, MSG_BUFFER_SIZE);
    return 0;
}

void
sol_mainloop_impl_shutdown(void)
{
    sol_mainloop_common_shutdown();
}

static inline void
timex_set_until_next_timeout(timex_t *timex)
{
    struct sol_timeout_common *timeout;
    struct timespec now;
    struct timespec diff;

    timeout = sol_mainloop_common_timeout_first();
    if (!timeout) {
        *timex = timex_set(0, DEFAULT_USLEEP_TIME);
        return;
    }

    now = sol_util_timespec_get_current();
    sol_util_timespec_sub(&timeout->expire, &now, &diff);

    if (diff.tv_sec < 0)
        *timex = timex_set(0, 0);
    else
        *timex = timex_set(diff.tv_sec, diff.tv_nsec / NSEC_PER_USEC);
}

void
sol_mainloop_impl_iter(void)
{
    msg_t msg;
    timex_t timex;

    sol_mainloop_common_timeout_process();
    sol_mainloop_common_idler_process();
    sol_mainloop_common_timeout_process();

    if (!sol_mainloop_common_loop_check())
        return;

    timex_set_until_next_timeout(&timex);
    if (vtimer_msg_receive_timeout(&msg, timex) > 0)
        sol_interrupt_scheduler_process(&msg);
}
