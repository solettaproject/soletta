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

#include <contiki.h>
#include <lib/sensors.h>

#include "sol-event-handler-contiki.h"
#include "sol-mainloop-common.h"
#include "sol-mainloop-contiki.h"
#include "sol-mainloop-impl.h"
#include "sol-vector.h"

#define DEFAULT_USLEEP_TIME 10000
#define DEFAULT_USLEEP_TIME_TICKS (CLOCK_SECOND * DEFAULT_USLEEP_TIME) / NSEC_PER_SEC

static process_event_t event;
static process_data_t event_data;
static struct etimer et;

static struct sol_ptr_vector event_handler_vector = SOL_PTR_VECTOR_INIT;
static bool event_handling_processing;
static unsigned int event_handler_pending_deletion;

struct sol_event_handler_contiki
{
    process_event_t *ev;
    process_data_t ev_data;
    void (*cb)(process_event_t ev, process_data_t ev_data, void *user_data);
    void *data;
    bool delete_me;
};

static void event_dispatch(void);

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
    return 0;
}

void
sol_mainloop_impl_shutdown(void)
{
    void *ptr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&event_handler_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&event_handler_vector);

    sol_mainloop_common_shutdown();
}

static inline clock_time_t
ticks_until_next_timeout(void)
{
    struct sol_timeout_common *timeout;
    struct timespec now, diff;

    timeout = sol_mainloop_common_timeout_first();
    if (!timeout)
        return DEFAULT_USLEEP_TIME_TICKS;

    now = sol_util_timespec_get_current();
    sol_util_timespec_sub(&timeout->expire, &now, &diff);

    if (diff.tv_sec < 0)
        return 0;

    return diff.tv_sec * CLOCK_SECOND +
                (CLOCK_SECOND / NSEC_PER_SEC) * diff.tv_nsec;
}

void
sol_mainloop_impl_run(void)
{
    if (!sol_mainloop_impl_main_thread_check()) {
        SOL_ERR("sol_run() called on different thread than sol_init()");
        return;
    }

    sol_mainloop_common_loop_set(true);
}

bool
sol_mainloop_contiki_iter(void)
{
    // Another event could make process wakeup
    etimer_stop(&et);

    event_dispatch();

    sol_mainloop_common_timeout_process();
    sol_mainloop_common_idler_process();
    sol_mainloop_common_timeout_process();

    if (!sol_mainloop_common_loop_check())
        return false;

    etimer_set(&et, ticks_until_next_timeout());
    return true;
}

void
sol_mainloop_impl_iter(void)
{
    // empty
}

void *
sol_mainloop_impl_fd_add(int fd, unsigned int flags, bool (*cb)(void *data, int fd, unsigned int active_flags), const void *data)
{
    SOL_CRI("Unsupported");
    return NULL;
}

bool
sol_mainloop_impl_fd_del(void *handle)
{
    SOL_CRI("Unsupported");
    return true;
}

void *
sol_mainloop_impl_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data)
{
    SOL_CRI("Unsupported");
    return NULL;
}

bool
sol_mainloop_impl_child_watch_del(void *handle)
{
    SOL_CRI("Unsupported");
    return true;
}

void
sol_mainloop_contiki_event_set(process_event_t ev, process_data_t data)
{
    event = ev;
    event_data = data;
}

bool
sol_mainloop_contiki_event_handler_add(const process_event_t *ev, const process_data_t ev_data, void (*cb)(process_event_t ev, process_data_t ev_data, void *user_data), const void *data)
{
    struct sol_event_handler_contiki *event_handler =
                malloc(sizeof(struct sol_event_handler_contiki));
    if (!event_handler)
        return false;
    event_handler->ev = ev;
    event_handler->ev_data = ev_data;
    event_handler->cb = cb;
    event_handler->data = (void*) data;
    event_handler->delete_me = false;

    if (sol_ptr_vector_append(&event_handler_vector, event_handler)) {
        free(event_handler);
        return false;
    }

    return true;
}

bool
sol_mainloop_contiki_event_handler_del(const process_event_t *ev, const process_data_t ev_data, void (*cb)(process_event_t ev, process_data_t ev_data, void *user_data), const void *data)
{
    int i;
    struct sol_event_handler_contiki *event_handler;

    SOL_PTR_VECTOR_FOREACH_IDX(&event_handler_vector, event_handler, i) {
        if (event_handler->ev != ev)
            continue;
        if (event_handler->ev_data != ev_data)
            continue;
        if (event_handler->cb != cb)
            continue;
        if (event_handler->data != data)
            continue;

        if (event_handling_processing) {
            event_handler->delete_me = true;
            event_handler_pending_deletion++;
        } else {
            sol_ptr_vector_del(&event_handler_vector, i);
            free(event_handler);
        }
        return true;
    }
    return false;
}

static void
event_handler_cleanup(void)
{
    int i;
    struct sol_event_handler_contiki *event_handler;

    if (!event_handler_pending_deletion)
        return;
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX(&event_handler_vector, event_handler, i) {
        if (!event_handler->delete_me)
            continue;
        sol_ptr_vector_del(&event_handler_vector, i);
        free(event_handler);
        event_handler_pending_deletion--;
        if (!event_handler_pending_deletion)
            break;
    }
}

static void
event_dispatch(void)
{
    int i;
    struct sol_event_handler_contiki *event_handler;

    if (event != sensors_event)
        return;

    event_handling_processing = true;
    SOL_PTR_VECTOR_FOREACH_IDX(&event_handler_vector, event_handler, i) {
        if (event_handler->delete_me)
            continue;
        if (*event_handler->ev != event)
            continue;
        if (event_handler->ev_data != NULL &&
                    event_handler->ev_data != event_data)
            continue;
        event_handler->cb(event, event_data, event_handler->data);
    }
    event_handling_processing = false;
    event_handler_cleanup();
}
