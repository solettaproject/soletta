/*
 * This file is part of the Soletta (TM) Project
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "sol-atomic.h"
#include "sol-mainloop.h"
#include "sol-worker-thread-impl.h"

struct sol_worker_thread_glib {
    struct sol_worker_thread_config config;
    struct sol_idle *idler;
    GMutex lock;
    GThread *thread;
    sol_atomic_int cancel;
};

bool
sol_worker_thread_impl_cancel_check(const void *handle)
{
    const struct sol_worker_thread_glib *thread = handle;

    return sol_atomic_load(&thread->cancel, SOL_ATOMIC_RELAXED);
}

static inline void
cancel_set(struct sol_worker_thread_glib *thread)
{
    sol_atomic_store(&thread->cancel, true, SOL_ATOMIC_RELAXED);
}

static bool
sol_worker_thread_finished(void *data)
{
    struct sol_worker_thread_glib *thread = data;

    if (!sol_worker_thread_impl_cancel_check(thread)) {
        /* no need to set cancel, the thread has finished */
        g_thread_join(thread->thread);
        thread->thread = NULL;
    }
    g_mutex_clear(&thread->lock);

    /* no locks since thread is now dead */
    thread->idler = NULL;

    SOL_DBG("worker thread %p finished", thread);

    if (thread->config.finished)
        thread->config.finished((void *)thread->config.data);

    free(thread);
    return false;
}

static gpointer
sol_worker_thread_do(gpointer data)
{
    struct sol_worker_thread_glib *thread = data;
    struct sol_worker_thread_config *config = &thread->config;

    SOL_DBG("worker thread %p started", thread);

    if (config->setup) {
        if (!config->setup((void *)config->data))
            goto end;
    }

    while (!sol_worker_thread_impl_cancel_check(thread)) {
        if (!config->iterate((void *)config->data))
            break;
    }

    if (config->cleanup)
        config->cleanup((void *)config->data);

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
sol_worker_thread_impl_new(const struct sol_worker_thread_config *config)
{
    static sol_atomic_uint thr_cnt = SOL_ATOMIC_INIT(0u);
    struct sol_worker_thread_glib *thread;
    char name[16];

    thread = calloc(1, sizeof(*thread));
    SOL_NULL_CHECK(thread, NULL);

    thread->config = *config;

    g_mutex_init(&thread->lock);

    snprintf(name, 16, "thr-%u",
        sol_atomic_fetch_add(&thr_cnt, 1, SOL_ATOMIC_RELAXED));
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

    if (sol_worker_thread_impl_cancel_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread == g_thread_self()) {
        SOL_WRN("trying to cancel from worker thread %p.", thread);
        return;
    }

    cancel_set(thread);

    if (thread->config.cancel)
        thread->config.cancel((void *)thread->config.data);

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

    thread->config.feedback((void *)thread->config.data);
    return false;
}

void
sol_worker_thread_impl_feedback(void *handle)
{
    struct sol_worker_thread_glib *thread = handle;

    SOL_NULL_CHECK(thread);
    SOL_NULL_CHECK(thread->config.feedback);

    if (sol_worker_thread_impl_cancel_check(thread)) {
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
