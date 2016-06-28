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
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "sol-atomic.h"
#include "sol-mainloop.h"
#include "sol-worker-thread-impl.h"

struct sol_worker_thread_posix {
    struct sol_worker_thread_config config;
    struct sol_idle *idler;
    pthread_mutex_t lock;
    pthread_t thread;
    sol_atomic_int cancel;
};

bool
sol_worker_thread_impl_cancel_check(const void *handle)
{
    const struct sol_worker_thread_posix *thread = handle;

    return sol_atomic_load(&thread->cancel, SOL_ATOMIC_RELAXED);
}

static inline void
cancel_set(struct sol_worker_thread_posix *thread)
{
    sol_atomic_store(&thread->cancel, true, SOL_ATOMIC_RELAXED);
}

static bool
sol_worker_thread_lock(struct sol_worker_thread_posix *thread)
{
    int error = pthread_mutex_lock(&thread->lock);

    if (!error)
        return true;
    if (error == EDEADLK)
        abort();

    return false;
}

static void
sol_worker_thread_unlock(struct sol_worker_thread_posix *thread)
{
    pthread_mutex_unlock(&thread->lock);
}

static bool
sol_worker_thread_finished(void *data)
{
    struct sol_worker_thread_posix *thread = data;

    if (!sol_worker_thread_impl_cancel_check(thread)) {
        /* no need to set cancel, the thread has finished */
        pthread_join(thread->thread, NULL);
        thread->thread = 0;
    }
    pthread_mutex_destroy(&thread->lock);

    /* no locks since thread is now dead */
    thread->idler = NULL;

    SOL_DBG("worker thread %p finished", thread);

    if (thread->config.finished)
        thread->config.finished((void *)thread->config.data);

    free(thread);
    return false;
}

static void *
sol_worker_thread_do(void *data)
{
    struct sol_worker_thread_posix *thread = data;
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
    if (sol_worker_thread_lock(thread)) {
        if (thread->idler)
            sol_idle_del(thread->idler);
        thread->idler = sol_idle_add(sol_worker_thread_finished, thread);
        sol_worker_thread_unlock(thread);
    }

    SOL_DBG("worker thread %p stopped", thread);

    return thread;
}

extern void sol_mainloop_posix_signals_block(void);
extern void sol_mainloop_posix_signals_unblock(void);

void *
sol_worker_thread_impl_new(const struct sol_worker_thread_config *config)
{
    pthread_mutexattr_t attrs;
    struct sol_worker_thread_posix *thread;
    int r;

    thread = calloc(1, sizeof(*thread));
    SOL_NULL_CHECK(thread, NULL);

    thread->config = *config;

    r = pthread_mutexattr_init(&attrs);
    SOL_INT_CHECK_GOTO(r, != 0, error_mutex);

    pthread_mutexattr_setpshared(&attrs, PTHREAD_PROCESS_SHARED);
#if defined(PTHREAD_MUTEX_ADAPTIVE_NP)
    pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif

    r = pthread_mutex_init(&thread->lock, &attrs);
    pthread_mutexattr_destroy(&attrs);
    SOL_INT_CHECK_GOTO(r, != 0, error_mutex);

    sol_mainloop_posix_signals_block();
    r = pthread_create(&thread->thread, NULL,
        sol_worker_thread_do, thread);
    sol_mainloop_posix_signals_unblock();
    SOL_INT_CHECK_GOTO(r, != 0, error_thread);

    return thread;

error_thread:
    pthread_mutex_destroy(&thread->lock);

error_mutex:
    free(thread);
    errno = r;
    return NULL;
}

void
sol_worker_thread_impl_cancel(void *handle)
{
    struct sol_worker_thread_posix *thread = handle;

    SOL_NULL_CHECK(thread);

    if (sol_worker_thread_impl_cancel_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread == pthread_self()) {
        SOL_WRN("trying to cancel from worker thread %p.", thread);
        return;
    }

    cancel_set(thread);

    if (thread->config.cancel)
        thread->config.cancel((void *)thread->config.data);

    pthread_join(thread->thread, NULL);
    thread->thread = 0;

    /* no locks since thread is now dead */
    sol_idle_del(thread->idler);
    sol_worker_thread_finished(thread);
}

static bool
sol_worker_thread_feedback_dispatch(void *data)
{
    struct sol_worker_thread_posix *thread = data;

    if (sol_worker_thread_lock(thread)) {
        thread->idler = NULL;
        sol_worker_thread_unlock(thread);
    }

    thread->config.feedback((void *)thread->config.data);
    return false;
}

void
sol_worker_thread_impl_feedback(void *handle)
{
    struct sol_worker_thread_posix *thread = handle;

    SOL_NULL_CHECK(thread);
    SOL_NULL_CHECK(thread->config.feedback);

    if (sol_worker_thread_impl_cancel_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread != pthread_self()) {
        SOL_WRN("trying to feedback from different worker thread %p.", thread);
        return;
    }
    if (sol_worker_thread_lock(thread)) {
        if (!thread->idler)
            thread->idler = sol_idle_add(sol_worker_thread_feedback_dispatch, thread);
        sol_worker_thread_unlock(thread);
    }
}
