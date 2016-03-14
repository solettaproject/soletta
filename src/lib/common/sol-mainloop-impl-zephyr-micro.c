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

#include <errno.h>

#include <microkernel.h>

#include "sol-log.h"
#include "sol-mainloop-zephyr.h"

#define MAX_QUEUED_EVENTS 8
#define PIPE_BUFFER_SIZE (MAX_QUEUED_EVENTS * sizeof(struct mainloop_event))
DEFINE_PIPE(_sol_mainloop_pipe, PIPE_BUFFER_SIZE)

int
sol_mainloop_impl_platform_init(void)
{
    return sol_mainloop_zephyr_common_init();
}

int
sol_mainloop_event_post(const struct mainloop_event *me)
{
    int bytes_written, ret;

    ret = task_pipe_put(_sol_mainloop_pipe, (void *)me, sizeof(*me), &bytes_written, 0, TICKS_NONE);
    SOL_INT_CHECK(ret, != RC_OK, -ENOMEM);

    return 0;
}

void
sol_mainloop_events_process(int32_t sleeptime)
{
    char buf[PIPE_BUFFER_SIZE];
    struct mainloop_event *p;
    int bytes_read, count, ret;

    ret = task_pipe_get(_sol_mainloop_pipe, buf, PIPE_BUFFER_SIZE, &bytes_read,
        0, sleeptime);

    if (ret == RC_OK) {
        p = (struct mainloop_event *)buf;
        count = bytes_read / sizeof(*p);
        while (count) {
            if (p->cb)
                p->cb((void *)p->data);
            count--;
            p++;
        }
    }
}
