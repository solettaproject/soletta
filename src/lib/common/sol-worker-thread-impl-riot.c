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

#include <errno.h>
#include <irq.h>
#include <mutex.h>
#include <thread.h>

#include "sol-atomic.h"
#include "sol-mainloop.h"
#include "sol-worker-thread-impl.h"

struct sol_worker_thread_riot {
    struct sol_worker_thread_config config;
    struct sol_idle *idler;
    char *stack;
    mutex_t lock;
    kernel_pid_t thread;
    kernel_pid_t waiting_join;
    sol_atomic_int cancel;
    sol_atomic_int finished;
};

bool
sol_worker_thread_impl_cancel_check(const void *handle)
{
    const struct sol_worker_thread_riot *thread = handle;

    thread_yield();

    return sol_atomic_load(&thread->cancel, SOL_ATOMIC_RELAXED);
}

static inline void
cancel_set(struct sol_worker_thread_riot *thread)
{
    sol_atomic_store(&thread->cancel, true, SOL_ATOMIC_RELAXED);

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

    status = sol_atomic_load(&thread->finished, SOL_ATOMIC_ACQUIRE);
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

    if (thread->config.finished)
        thread->config.finished((void *)thread->config.data);

    free(thread->stack);
    free(thread);
    return false;
}

static void *
sol_worker_thread_do(void *data)
{
    struct sol_worker_thread_riot *thread = data;
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
    /* From this point forward, we can't allow a context switch. IRQ will be
     * re-enabled by the scheduler once this function returns */
    irq_disable();
    sol_atomic_store(&thread->finished, true, SOL_ATOMIC_RELEASE);
    if (thread->waiting_join != KERNEL_PID_UNDEF)
        sched_set_status((thread_t *)sched_threads[thread->waiting_join], STATUS_PENDING);

    return thread;
}

void *
sol_worker_thread_impl_new(const struct sol_worker_thread_config *config)
{
    struct sol_worker_thread_riot *thread;
    int r = -ENOMEM, stacksize = THREAD_STACKSIZE_DEFAULT;
    char prio = THREAD_PRIORITY_MIN - 1;

    thread = calloc(1, sizeof(*thread));
    SOL_NULL_CHECK(thread, NULL);

    thread->config = *config;

    mutex_init(&thread->lock);

    thread->stack = malloc(stacksize);
    SOL_NULL_CHECK_GOTO(thread->stack, error_stack);

    r = thread_create(thread->stack, stacksize, prio, THREAD_CREATE_STACKTEST, sol_worker_thread_do, thread, "worker-thread");
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

    if (thread->config.cancel)
        thread->config.cancel((void *)thread->config.data);

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

    thread->config.feedback((void *)thread->config.data);
    return false;
}

void
sol_worker_thread_impl_feedback(void *handle)
{
    struct sol_worker_thread_riot *thread = handle;

    SOL_NULL_CHECK(thread);
    SOL_NULL_CHECK(thread->config.feedback);

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
