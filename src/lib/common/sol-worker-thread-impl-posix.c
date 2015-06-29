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

#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "sol-mainloop.h"
#include "sol-worker-thread-impl.h"

struct sol_worker_thread_posix {
    const void *data;
    bool (*setup)(void *data);
    void (*cleanup)(void *data);
    bool (*iterate)(void *data);
    void (*cancel)(void *data);
    void (*finished)(void *data);
    void (*feedback)(void *data);
    struct sol_idle *idler;
    pthread_mutex_t lock;
    pthread_t thread;
};

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

    if (thread->thread) {
        pthread_join(thread->thread, NULL);
        thread->thread = 0;
    }
    pthread_mutex_destroy(&thread->lock);

    /* no locks since thread is now dead */
    thread->idler = NULL;

    SOL_DBG("worker thread %p finished", thread);

    if (thread->finished)
        thread->finished((void *)thread->data);

    free(thread);
    return false;
}

static void *
sol_worker_thread_do(void *data)
{
    struct sol_worker_thread_posix *thread = data;

    SOL_DBG("worker thread %p started", thread);

    if (thread->setup) {
        if (!thread->setup((void *)thread->data))
            goto end;
    }

    while (thread->thread) {
        if (!thread->iterate((void *)thread->data))
            break;
    }

    if (thread->cleanup)
        thread->cleanup((void *)thread->data);

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
sol_worker_thread_impl_new(bool (*setup)(void *data),
    void (*cleanup)(void *data),
    bool (*iterate)(void *data),
    void (*cancel)(void *data),
    void (*finished)(void *data),
    void (*feedback)(void *data),
    const void *data)
{
    pthread_mutexattr_t attrs;
    struct sol_worker_thread_posix *thread;
    int r;

    SOL_NULL_CHECK(iterate, NULL);

    thread = calloc(1, sizeof(*thread));
    SOL_NULL_CHECK(thread, NULL);

    thread->data = data;
    thread->setup = setup;
    thread->cleanup = cleanup;
    thread->iterate = iterate;
    thread->cancel = cancel;
    thread->finished = finished;
    thread->feedback = feedback;

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
    pthread_t tid;
    int r;

    SOL_NULL_CHECK(thread);

    tid = thread->thread;
    if (!tid) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread == pthread_self()) {
        SOL_WRN("trying to cancel from worker thread %p.", thread);
        return;
    }

    if (thread->cancel)
        thread->cancel((void *)thread->data);

    thread->thread = 0;
    r = pthread_join(tid, NULL);
    SOL_INT_CHECK(r, != 0);

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

    thread->feedback((void *)thread->data);
    return false;
}

void
sol_worker_thread_impl_feedback(void *handle)
{
    struct sol_worker_thread_posix *thread = handle;

    SOL_NULL_CHECK(thread);
    SOL_NULL_CHECK(thread->feedback);

    if (!thread->thread) {
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
