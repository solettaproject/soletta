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
#include <stdlib.h>
#include <unistd.h>

/* Zephyr headers */
#include <microkernel.h>

#include "sol-mainloop-common.h"
#include "sol-mainloop-impl.h"
#include "sol-mainloop-zephyr.h"
#include "sol-vector.h"

#define PIPE_BUFFER_SIZE 32
DEFINE_PIPE(_sol_mainloop_pipe, PIPE_BUFFER_SIZE)

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
sol_mainloop_impl_platform_init(void)
{
    return 0;
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

    if (sol_mainloop_common_idler_first())
        return TICKS_NONE;

    if (!sol_mainloop_common_timespec_first(&ts))
        return TICKS_UNLIMITED;

    return ts.tv_sec * sys_clock_ticks_per_sec +
        ((long long)sys_clock_ticks_per_sec * ts.tv_nsec) / NSEC_PER_SEC;
}

void
sol_mainloop_impl_iter(void)
{
    char buf[PIPE_BUFFER_SIZE];
    struct mainloop_wake_data *p;
    int32_t sleeptime;
    int bytes_read, count, ret;

    sol_mainloop_common_timeout_process();

    sleeptime = ticks_until_next_timeout();
    ret = task_pipe_get(_sol_mainloop_pipe, buf, PIPE_BUFFER_SIZE, &bytes_read,
            0, sleeptime);

    if (ret == RC_OK) {
        p = (struct mainloop_wake_data *)buf;
        count = bytes_read / sizeof(*p);
        while (count) {
            p->cb((void *)p->data);
            count--;
            p++;
        }
    }

    sol_mainloop_common_idler_process();
}

int
sol_mainloop_wakeup(const struct mainloop_wake_data *mwd)
{
    int bytes_written;
    return task_pipe_put(_sol_mainloop_pipe, (void *)mwd, sizeof(*mwd), &bytes_written, 0, TICKS_NONE);
}
