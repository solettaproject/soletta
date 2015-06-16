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
#include <time.h>

// Riot includes
#include <sched.h>
#include <vtimer.h>

#include "sol-interrupt_scheduler_riot.h"
#include "sol-mainloop-impl.h"
#include "sol-vector.h"
#include "sol-util.h"

#define DEFAULT_USLEEP_TIME 10000

static bool run_loop;

static bool timeout_processing;
static unsigned int timeout_pending_deletion;
static struct sol_ptr_vector timeout_vector = SOL_PTR_VECTOR_INIT;

static bool idler_processing;
static unsigned int idler_pending_deletion;
static struct sol_ptr_vector idler_vector = SOL_PTR_VECTOR_INIT;

#define MSG_BUFFER_SIZE 32
static msg_t msg_buffer[MSG_BUFFER_SIZE];

struct sol_timeout_riot {
    struct timespec timeout;
    struct timespec expire;
    const void *data;
    bool (*cb)(void *data);
    bool remove_me;
};

struct sol_idler_riot {
    const void *data;
    bool (*cb)(void *data);
    enum { idler_ready, idler_deleted, idler_ready_on_next_iteration } status;
};

int
sol_mainloop_impl_init(void)
{
    sol_interrupt_scheduler_set_pid(sched_active_pid);
    msg_init_queue(msg_buffer, MSG_BUFFER_SIZE);
    return 0;
}

void
sol_mainloop_impl_shutdown(void)
{
    void *ptr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&timeout_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&timeout_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&idler_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&idler_vector);
}

static int
timeout_compare(const void *data1, const void *data2)
{
    const struct sol_timeout_riot *a = data1, *b = data2;

    return sol_util_timespec_compare(&a->expire, &b->expire);
}

static inline void
timeout_cleanup(void)
{
    struct sol_timeout_riot *timeout;
    uint16_t i;

    if (!timeout_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&timeout_vector, timeout, i) {
        if (!timeout->remove_me)
            continue;

        sol_ptr_vector_del(&timeout_vector, i);
        free(timeout);
        timeout_pending_deletion--;
        if (!timeout_pending_deletion)
            break;
    }
}

static inline void
timeout_process(void)
{
    struct timespec now;
    unsigned int i;

    timeout_processing = true;
    now = sol_util_timespec_get_current();
    for (i = 0; i < timeout_vector.base.len; i++) {
        struct sol_timeout_riot *timeout = sol_ptr_vector_get(&timeout_vector, i);
        if (!run_loop)
            break;
        if (timeout->remove_me)
            continue;
        if (sol_util_timespec_compare(&timeout->expire, &now) > 0)
            break;

        if (!timeout->cb((void *)timeout->data)) {
            if (!timeout->remove_me) {
                timeout->remove_me = true;
                timeout_pending_deletion++;
            }
            continue;
        }

        sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
        sol_ptr_vector_del(&timeout_vector, i);
        sol_ptr_vector_insert_sorted(&timeout_vector, timeout, timeout_compare);
        i--;
    }

    timeout_cleanup();
    timeout_processing = false;
}

static inline void
idler_cleanup(void)
{
    struct sol_idler_riot *idler;
    uint16_t i;

    if (!idler_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&idler_vector, idler, i) {
        if (idler->status != idler_deleted)
            continue;

        sol_ptr_vector_del(&idler_vector, i);
        free(idler);
        idler_pending_deletion--;
        if (!idler_pending_deletion)
            break;
    }
}

static inline void
idler_process(void)
{
    uint16_t i;
    struct sol_idler_riot *idler;

    idler_processing = true;
    SOL_PTR_VECTOR_FOREACH_IDX (&idler_vector, idler, i) {
        if (!run_loop)
            break;
        if (idler->status != idler_ready) {
            if (idler->status == idler_ready_on_next_iteration)
                idler->status = idler_ready;
            continue;
        }
        if (!idler->cb((void *)idler->data)) {
            if (idler->status != idler_deleted) {
                idler->status = idler_deleted;
                idler_pending_deletion++;
            }
        }
        timeout_process();
    }

    idler_cleanup();
    idler_processing = false;
}

static inline void
timex_set_until_next_timeout(timex_t *timex)
{
    struct sol_timeout_riot *timeout;
    struct timespec now;
    struct timespec diff;

    if (!timeout_vector.base.len) {
        *timex = timex_set(0, DEFAULT_USLEEP_TIME);
        return;
    }

    timeout = sol_ptr_vector_get(&timeout_vector, 0);
    now = sol_util_timespec_get_current();

    sol_util_timespec_sub(&timeout->expire, &now, &diff);
    if (diff.tv_sec < 0)
        *timex = timex_set(0, 0);
    else
        *timex = timex_set(diff.tv_sec, diff.tv_nsec / NSEC_PER_USEC);
}

void
sol_mainloop_impl_run(void)
{
    run_loop = true;
    while (run_loop) {
        msg_t msg;
        timex_t timex;

        timeout_process();
        idler_process();
        timeout_process();

        if (!run_loop)
            return;

        timex_set_until_next_timeout(&timex);
        if (vtimer_msg_receive_timeout(&msg, timex) > 0)
            sol_interrupt_scheduler_process(&msg);
    }
}

void
sol_mainloop_impl_quit(void)
{
    run_loop = false;
}

void *
sol_mainloop_impl_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data)
{
    struct timespec now;
    int ret;
    struct sol_timeout_riot *timeout = malloc(sizeof(struct sol_timeout_riot));

    SOL_NULL_CHECK(timeout, NULL);

    timeout->timeout sol_util_timespec_from_msec(timeout_ms);
    timeout->cb = cb;
    timeout->data = data;
    timeout->remove_me = false;

    now = sol_util_timespec_get_current();
    sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
    ret = sol_ptr_vector_insert_sorted(&timeout_vector, timeout, timeout_compare);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);
    return timeout;

clean:
    free(timeout);
    return NULL;
}

bool
sol_mainloop_impl_timeout_del(void *handle)
{
    struct sol_timeout_riot *timeout = handle;

    timeout->remove_me = true;
    timeout_pending_deletion++;
    if (!timeout_processing)
        timeout_cleanup();

    return true;
}

void *
sol_mainloop_impl_idle_add(bool (*cb)(void *data), const void *data)
{
    int ret;
    struct sol_idler_riot *idler = malloc(sizeof(struct sol_idler_riot));

    SOL_NULL_CHECK(idler, NULL);
    idler->cb = cb;
    idler->data = data;

    idler->status = idler_processing ? idler_ready_on_next_iteration : idler_ready;
    ret = sol_ptr_vector_append(&idler_vector, idler);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);
    return idler;

clean:
    free(idler);
    return NULL;
}

bool
sol_mainloop_impl_idle_del(void *handle)
{
    struct sol_idler_riot *idler = handle;

    idler->status = idler_deleted;
    idler_pending_deletion++;
    if (!idler_processing)
        idler_cleanup();

    return true;
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
