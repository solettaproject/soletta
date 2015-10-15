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

#include <errno.h>
#include <mutex.h>
#include <thread.h>

#include "sol-mainloop.h"
#include "sol-worker-thread-impl.h"

struct sol_worker_thread_riot {
    struct sol_worker_thread_spec spec;
    struct sol_idle *idler;
    char *stack;
    mutex_t lock;
    kernel_pid_t thread;
    kernel_pid_t waiting_join;
    bool cancel;
    bool finished;
};

bool
sol_worker_thread_impl_cancel_check(const void *handle)
{
    const struct sol_worker_thread_riot *thread = handle;

    thread_yield();

    return __atomic_load_n(&thread->cancel, __ATOMIC_SEQ_CST);
}

static inline void
cancel_set(struct sol_worker_thread_riot *thread)
{
    __atomic_store_n(&thread->cancel, true, __ATOMIC_SEQ_CST);

    thread_yield();
}

static bool
sol_worker_thread_lock(struct sol_worker_thread_riot *thread)
{
    mutex_lock(&thread->lock);

    return true;
}

static void
sol_worker_thread_unlock(struct sol_worker_thread_riot *thread)
{
    mutex_unlock(&thread->lock);
}

static void
sol_worker_thread_join(struct sol_worker_thread_riot *thread)
{
    bool status;

    status = __atomic_load_n(&thread->finished, __ATOMIC_SEQ_CST);
    if (!status) {
        thread->waiting_join = thread_getpid();
        thread_sleep();
    }
}

static bool
sol_worker_thread_finished(void *data)
{
    struct sol_worker_thread_riot *thread = data;

    if (!sol_worker_thread_impl_cancel_check(thread)) {
        /* no need to set cancel, the thread has finished */
        sol_worker_thread_join(thread);
        thread->thread = KERNEL_PID_UNDEF;
    }

    /* no locks since thread is now dead */
    thread->idler = NULL;

    SOL_DBG("worker thread %p finished", thread);

    if (thread->spec.finished)
        thread->spec.finished((void *)thread->spec.data);

    free(thread->stack);
    free(thread);
    return false;
}

static void *
sol_worker_thread_do(void *data)
{
    struct sol_worker_thread_riot *thread = data;
    struct sol_worker_thread_spec *spec = &thread->spec;

    SOL_DBG("worker thread %p started", thread);

    if (spec->setup) {
        if (!spec->setup((void *)spec->data))
            goto end;
    }

    while (!sol_worker_thread_impl_cancel_check(thread)) {
        if (!spec->iterate((void *)spec->data))
            break;
    }

    if (spec->cleanup)
        spec->cleanup((void *)spec->data);

end:
    if (sol_worker_thread_lock(thread)) {
        if (thread->idler)
            sol_idle_del(thread->idler);
        thread->idler = sol_idle_add(sol_worker_thread_finished, thread);
        sol_worker_thread_unlock(thread);
    }

    SOL_DBG("worker thread %p stopped", thread);
    /* From this point forward, we can't allow a context switch. IRQ will be
     * re-enabled by the scheduler once this function returns */
    disableIRQ();
    __atomic_store_n(&thread->finished, true, __ATOMIC_SEQ_CST);
    if (thread->waiting_join != KERNEL_PID_UNDEF)
        sched_set_status((tcb_t *)sched_threads[thread->waiting_join], STATUS_PENDING);

    return thread;
}

void *
sol_worker_thread_impl_new(const struct sol_worker_thread_spec *spec)
{
    struct sol_worker_thread_riot *thread;
    int r = -ENOMEM, stacksize = THREAD_STACKSIZE_DEFAULT;
    char prio = THREAD_PRIORITY_MIN - 1;

    thread = calloc(1, sizeof(*thread));
    SOL_NULL_CHECK(thread, NULL);

    thread->spec = *spec;

    mutex_init(&thread->lock);

    thread->stack = malloc(stacksize);
    SOL_NULL_CHECK_GOTO(thread->stack, error_stack);

    r = thread_create(thread->stack, stacksize, prio, CREATE_STACKTEST, sol_worker_thread_do, thread, "worker-thread");
    SOL_INT_CHECK_GOTO(r, < 0, error_thread);

    thread->thread = r;

    return thread;

error_thread:
    free(thread->stack);
error_stack:
    free(thread);
    errno = -r;
    return NULL;
}

void
sol_worker_thread_impl_cancel(void *handle)
{
    struct sol_worker_thread_riot *thread = handle;

    SOL_NULL_CHECK(thread);

    if (sol_worker_thread_impl_cancel_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread == thread_getpid()) {
        SOL_WRN("trying to cancel from worker thread %p.", thread);
        return;
    }

    cancel_set(thread);

    if (thread->spec.cancel)
        thread->spec.cancel((void *)thread->spec.data);

    sol_worker_thread_join(thread);
    thread->thread = KERNEL_PID_UNDEF;

    /* no locks since thread is now dead */
    sol_idle_del(thread->idler);
    sol_worker_thread_finished(thread);
}

static bool
sol_worker_thread_feedback_dispatch(void *data)
{
    struct sol_worker_thread_riot *thread = data;

    if (sol_worker_thread_lock(thread)) {
        thread->idler = NULL;
        sol_worker_thread_unlock(thread);
    }

    thread->spec.feedback((void *)thread->spec.data);
    return false;
}

void
sol_worker_thread_impl_feedback(void *handle)
{
    struct sol_worker_thread_riot *thread = handle;

    SOL_NULL_CHECK(thread);
    SOL_NULL_CHECK(thread->spec.feedback);

    if (sol_worker_thread_impl_cancel_check(thread)) {
        SOL_WRN("worker thread %p is not running.", thread);
        return;
    }
    if (thread->thread != thread_getpid()) {
        SOL_WRN("trying to feedback from different worker thread %p.", thread);
        return;
    }
    if (sol_worker_thread_lock(thread)) {
        if (!thread->idler)
            thread->idler = sol_idle_add(sol_worker_thread_feedback_dispatch, thread);
        sol_worker_thread_unlock(thread);
    }
}
