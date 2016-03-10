/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <nanokernel.h>

#include "sol-log.h"
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

int
sol_mainloop_impl_platform_init(void)
{
    int i;

    sol_mainloop_zephyr_common_init();

    nano_fifo_init(&_sol_mainloop_pending_events);
    nano_fifo_init(&_sol_mainloop_free_events);
    for (i = 0; i < SOL_UTIL_ARRAY_SIZE(_events); i++) {
        struct me_fifo_entry *mfe;

        mfe = &_events[i];
        nano_fifo_put(&_sol_mainloop_free_events, mfe);
    }

    return 0;
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
