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

struct sol_event_handler_contiki {
    const process_event_t *ev;
    process_data_t ev_data;
    void (*cb)(void *user_data, process_event_t ev, process_data_t ev_data);
    const void *user_data;
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
sol_mainloop_impl_platform_init(void)
{
    return 0;
}

void
sol_mainloop_impl_platform_shutdown(void)
{
    void *ptr;
    uint16_t i;

    sol_mainloop_common_source_shutdown();

    SOL_PTR_VECTOR_FOREACH_IDX (&event_handler_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&event_handler_vector);
}

static inline clock_time_t
ticks_until_next_timeout(void)
{
    struct timespec ts;

    if (!sol_mainloop_common_timespec_first(&ts))
        return 0;

    if (ts.tv_sec < 0)
        return 0;

    return ts.tv_sec * CLOCK_SECOND +
           (CLOCK_SECOND / NSEC_PER_SEC) * ts.tv_nsec;
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

void
sol_mainloop_contiki_event_set(process_event_t ev, process_data_t data)
{
    event = ev;
    event_data = data;
}

bool
sol_mainloop_contiki_event_handler_add(const process_event_t *ev, const process_data_t ev_data, void (*cb)(void *user_data, process_event_t ev, process_data_t ev_data), const void *data)
{
    struct sol_event_handler_contiki *event_handler =
        malloc(sizeof(struct sol_event_handler_contiki));

    if (!event_handler)
        return false;
    event_handler->ev = ev;
    event_handler->ev_data = ev_data;
    event_handler->cb = cb;
    event_handler->user_data = data;
    event_handler->delete_me = false;

    if (sol_ptr_vector_append(&event_handler_vector, event_handler)) {
        free(event_handler);
        return false;
    }

    return true;
}

bool
sol_mainloop_contiki_event_handler_del(const process_event_t *ev, const process_data_t ev_data, void (*cb)(void *user_data, process_event_t ev, process_data_t ev_data), const void *data)
{
    int i;
    struct sol_event_handler_contiki *event_handler;

    SOL_PTR_VECTOR_FOREACH_IDX (&event_handler_vector, event_handler, i) {
        if (event_handler->ev != ev)
            continue;
        if (event_handler->ev_data != ev_data)
            continue;
        if (event_handler->cb != cb)
            continue;
        if (event_handler->user_data != data)
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
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&event_handler_vector, event_handler, i) {
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
    SOL_PTR_VECTOR_FOREACH_IDX (&event_handler_vector, event_handler, i) {
        if (event_handler->delete_me)
            continue;
        if (*event_handler->ev != event)
            continue;
        if (event_handler->ev_data != NULL &&
            event_handler->ev_data != event_data)
            continue;
        event_handler->cb((void *)event_handler->user_data, event, event_data);
    }
    event_handling_processing = false;
    event_handler_cleanup();
}
