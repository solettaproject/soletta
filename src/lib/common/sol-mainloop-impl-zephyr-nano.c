/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
