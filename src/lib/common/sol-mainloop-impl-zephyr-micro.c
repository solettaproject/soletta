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
