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
