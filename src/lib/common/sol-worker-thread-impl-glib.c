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
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "sol-mainloop.h"
#include "sol-worker-thread-impl.h"

struct sol_worker_thread_glib {
    struct sol_worker_thread_spec spec;
    struct sol_idle *idler;
    GMutex lock;
    GThread *thread;
    bool loop;
};

static inline bool
loop_check(const struct sol_worker_thread_glib *thread)
{
    return __atomic_load_n(&thread->loop, __ATOMIC_SEQ_CST);
}

static inline void
loop_set(struct sol_worker_thread_glib *thread, bool val)
{
    __atomic_store_n(&thread->loop, val, __ATOMIC_SEQ_CST);
}

static bool
sol_worker_thread_finished(void *data)
{
    struct sol_worker_thread_glib *thread = data;

    if (loop_check(thread)) {
        loop_set(thread, false);
        g_thread_join(thread->thread);
        thread->thread = NULL;
    }
    g_mutex_clear(&thread->lock);

    /* no locks since thread is now dead */
    thread->idler = NULL;

    SOL_DBG("worker thread %p finished", thread);

    if (thread->spec.finished)
        thread->spec.finished((void *)thread->spec.data);

    free(thread);
    return false;
}

static gpointer
sol_worker_thread_do(gpointer data)
{
    struct sol_worker_thread_glib *thread = data;
    struct sol_worker_thread_spec *spec = &thread->spec;

    SOL_DBG("worker thread %p started", thread);

    loop_set(thread, true);

    if (spec->setup) {
        if (!spec->setup((void *)spec->data))
            goto end;
    }

    while (loop_check(thread)) {
        if (!spec->iterate((void *)spec->data))
            break;
    }

    if (spec->cleanup)
        spec->cleanup((void *)spec->data);

end:
    g_mutex_lock(&thread->lock);
    if (thread->idler)
        sol_idle_del(thread->idler);
    thread->idler = sol_idle_add(sol_worker_thread_finished, thread);
    g_mutex_unlock(&thread->lock);

    SOL_DBG("worker thread %p stopped", thread);

    return thread;
}

void *
sol_worker_thread_impl_new(const struct sol_worker_thread_spec *spec)
{
    static uint16_t thr_cnt = 0;
    struct sol_worker_thread_glib *thread;
    char name[16];

    thread = calloc(1, sizeof(*thread));
    SOL_NULL_CHECK(thread, NULL);

    thread->spec = *spec;

    g_mutex_init(&thread->lock);

    snprintf(name, 16, "thr-%u",
        __atomic_fetch_add(&thr_cnt, 1, __ATOMIC_SEQ_CST));
    thread->thread = g_thread_new(name, sol_worker_thread_do, thread);
    SOL_NULL_CHECK_GOTO(thread->thread, error_thread);

    return thread;

error_thread:
    free(thread);
    return NULL;
}

void
sol_worker_thread_impl_cancel(void *handle)
{
    struct sol_worker_thread_glib *thread = handle;

    SOL_NULL_CHECK(thread);

    if (!loop_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread == g_thread_self()) {
        SOL_WRN("trying to cancel from worker thread %p.", thread);
        return;
    }

    if (thread->spec.cancel)
        thread->spec.cancel((void *)thread->spec.data);

    loop_set(thread, false);
    g_thread_join(thread->thread);
    thread->thread = NULL;

    /* no locks since thread is now dead */
    sol_idle_del(thread->idler);
    sol_worker_thread_finished(thread);
}

static bool
sol_worker_thread_feedback_dispatch(void *data)
{
    struct sol_worker_thread_glib *thread = data;

    g_mutex_lock(&thread->lock);
    thread->idler = NULL;
    g_mutex_unlock(&thread->lock);

    thread->spec.feedback((void *)thread->spec.data);
    return false;
}

void
sol_worker_thread_impl_feedback(void *handle)
{
    struct sol_worker_thread_glib *thread = handle;

    SOL_NULL_CHECK(thread);
    SOL_NULL_CHECK(thread->spec.feedback);

    if (!loop_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread != g_thread_self()) {
        SOL_WRN("trying to feedback from different worker thread %p.", thread);
        return;
    }

    g_mutex_lock(&thread->lock);
    if (!thread->idler)
        thread->idler = sol_idle_add(sol_worker_thread_feedback_dispatch,
            thread);
    g_mutex_unlock(&thread->lock);
}
