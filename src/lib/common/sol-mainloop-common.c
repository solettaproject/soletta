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

#include "sol-vector.h"
#include "sol-mainloop-impl.h"
#include "sol-mainloop-common.h"

struct sol_mainloop_source_common {
    const struct sol_mainloop_source_type *type;
    const void *data;
    bool ready;
    bool remove_me;
};

static bool source_processing;
static unsigned source_pending_deletion;
static struct sol_ptr_vector source_vector = SOL_PTR_VECTOR_INIT;

static bool run_loop;

static bool timeout_processing;
static unsigned int timeout_pending_deletion;
static struct sol_ptr_vector timeout_vector = SOL_PTR_VECTOR_INIT;

static bool idler_processing;
static unsigned int idler_pending_deletion;
static struct sol_ptr_vector idler_vector = SOL_PTR_VECTOR_INIT;

static int
timeout_compare(const void *data1, const void *data2)
{
    const struct sol_timeout_common *a = data1, *b = data2;

    return sol_util_timespec_compare(&a->expire, &b->expire);
}

#ifdef PTHREAD

static struct sol_ptr_vector source_v_process = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector timeout_v_process = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector idler_v_process = SOL_PTR_VECTOR_INIT;

#define SOURCE_PROCESS source_v_process
#define SOURCE_ACUM source_vector
#define TIMEOUT_PROCESS timeout_v_process
#define TIMEOUT_ACUM timeout_vector
#define IDLER_PROCESS idler_v_process
#define IDLER_ACUM idler_vector

static inline void
timeout_vector_update(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
    struct sol_timeout_common *itr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (from, itr, i)
        sol_ptr_vector_insert_sorted(to, itr, timeout_compare);
    sol_ptr_vector_clear(from);
}

#else  /* !PTHREAD */

#define SOURCE_PROCESS source_vector
#define SOURCE_ACUM source_vector
#define TIMEOUT_PROCESS timeout_vector
#define TIMEOUT_ACUM timeout_vector
#define IDLER_PROCESS idler_vector
#define IDLER_ACUM idler_vector

#define timeout_vector_update(...) do { } while (0)

#endif

bool
sol_mainloop_common_loop_check(void)
{
#ifdef PTHREAD
    return __atomic_load_n(&run_loop, __ATOMIC_SEQ_CST);
#else
    return run_loop;
#endif
}

void
sol_mainloop_common_loop_set(bool val)
{
#ifdef PTHREAD
    __atomic_store_n(&run_loop, (val), __ATOMIC_SEQ_CST);
#else
    run_loop = val;
#endif
}

int
sol_mainloop_impl_init(void)
{
    return sol_mainloop_impl_platform_init();
}

void
sol_mainloop_impl_shutdown(void)
{
    void *ptr;
    uint16_t i;

    sol_mainloop_impl_platform_shutdown();

    SOL_PTR_VECTOR_FOREACH_IDX (&source_vector, ptr, i) {
        struct sol_mainloop_source_common *source = ptr;
        source->remove_me = true;
        if (source->type->dispose)
            source->type->dispose((void *)source->data);
        free(ptr);
    }
    sol_ptr_vector_clear(&source_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&timeout_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&timeout_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&idler_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&idler_vector);
}

/* must be called with mainloop lock HELD */
static inline void
timeout_cleanup(void)
{
    struct sol_timeout_common *timeout;
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

void
sol_mainloop_common_timeout_process(void)
{
    struct timespec now;
    unsigned int i;

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&TIMEOUT_PROCESS, &TIMEOUT_ACUM);
    timeout_processing = true;
    sol_mainloop_impl_unlock();

    now = sol_util_timespec_get_current();
    for (i = 0; i < TIMEOUT_PROCESS.base.len; i++) {
        struct sol_timeout_common *timeout = sol_ptr_vector_get(&TIMEOUT_PROCESS, i);
        if (!sol_mainloop_common_loop_check())
            break;
        if (timeout->remove_me)
            continue;
        if (sol_util_timespec_compare(&timeout->expire, &now) > 0)
            break;

        if (!timeout->cb((void *)timeout->data)) {
            sol_mainloop_impl_lock();
            if (!timeout->remove_me) {
                timeout->remove_me = true;
                timeout_pending_deletion++;
            }
            sol_mainloop_impl_unlock();
            continue;
        }

        sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
        sol_ptr_vector_del(&TIMEOUT_PROCESS, i);
        sol_ptr_vector_insert_sorted(&TIMEOUT_PROCESS, timeout, timeout_compare);
        i--;
    }

    sol_mainloop_impl_lock();
    timeout_vector_update(&TIMEOUT_ACUM, &TIMEOUT_PROCESS);
    timeout_cleanup();
    timeout_processing = false;
    sol_mainloop_impl_unlock();
}

/* called with mainloop lock HELD */
static inline void
idler_cleanup(void)
{
    struct sol_idler_common *idler;
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

void
sol_mainloop_common_idler_process(void)
{
    struct sol_idler_common *idler;
    uint16_t i;

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&IDLER_PROCESS, &IDLER_ACUM);
    idler_processing = true;
    sol_mainloop_impl_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&IDLER_PROCESS, idler, i) {
        if (!sol_mainloop_common_loop_check())
            break;
        if (idler->status != idler_ready) {
            if (idler->status == idler_ready_on_next_iteration)
                idler->status = idler_ready;
            continue;
        }
        if (!idler->cb((void *)idler->data)) {
            sol_mainloop_impl_lock();
            if (idler->status != idler_deleted) {
                idler->status = idler_deleted;
                idler_pending_deletion++;
            }
            sol_mainloop_impl_unlock();
        }
        sol_mainloop_common_timeout_process();
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&IDLER_PROCESS, idler, i) {
        if (idler->status == idler_ready_on_next_iteration) {
            idler->status = idler_ready;
        }
    }

    sol_mainloop_impl_lock();
    sol_ptr_vector_update(&IDLER_ACUM, &IDLER_PROCESS);
    idler_cleanup();
    idler_processing = false;
    sol_mainloop_impl_unlock();
}

/* must be called with mainloop lock HELD */
static struct sol_timeout_common *
sol_mainloop_common_timeout_first(void)
{
    struct sol_timeout_common *timeout;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&timeout_vector, timeout, i) {
        if (timeout->remove_me)
            continue;
        return timeout;
    }
    return NULL;
}

static bool sol_mainloop_common_source_get_next_timeout_locked(struct timespec *timeout);

/* must be called with mainloop lock HELD */
bool
sol_mainloop_common_timespec_first(struct timespec *ts)
{
    struct sol_timeout_common *timeout = sol_mainloop_common_timeout_first();
    bool r = sol_mainloop_common_source_get_next_timeout_locked(ts);

    if (timeout) {
        struct timespec now = sol_util_timespec_get_current();
        struct timespec diff;
        sol_util_timespec_sub(&timeout->expire, &now, &diff);
        if (!r || sol_util_timespec_compare(&diff, ts) < 0) {
            if (diff.tv_sec < 0) {
                diff.tv_sec = 0;
                diff.tv_nsec = 0;
            }
            *ts = diff;
            r = true;
        }
    }

    return r;
}

/* must be called with mainloop lock HELD */
struct sol_idler_common *
sol_mainloop_common_idler_first(void)
{
    struct sol_idler_common *idler;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&idler_vector, idler, i) {
        if (idler->status == idler_deleted)
            continue;
        return idler;
    }
    return NULL;
}

/* must be called with mainloop lock HELD */
static inline void
source_cleanup(void)
{
    struct sol_mainloop_source_common *source;
    uint16_t i;

    if (!source_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&source_vector, source, i) {
        if (!source->remove_me)
            continue;

        sol_ptr_vector_del(&source_vector, i);
        sol_mainloop_impl_unlock();

        if (source->type->dispose)
            source->type->dispose((void *)source->data);
        free(source);

        sol_mainloop_impl_lock();
        source_pending_deletion--;
        if (!source_pending_deletion)
            break;
    }
}

bool
sol_mainloop_common_source_prepare(void)
{
    struct sol_mainloop_source_common *source;
    uint16_t i;
    bool ready = false;

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&SOURCE_PROCESS, &SOURCE_ACUM);
    source_processing = true;
    sol_mainloop_impl_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&SOURCE_PROCESS, source, i) {
        if (!sol_mainloop_common_loop_check())
            break;
        if (source->remove_me)
            continue;
        if (source->type->prepare) {
            source->ready = source->type->prepare((void *)source->data);
            ready |= source->ready;
        } else
            source->ready = false;
    }

    sol_mainloop_impl_lock();
    sol_ptr_vector_update(&SOURCE_ACUM, &SOURCE_PROCESS);
    source_cleanup();
    source_processing = false;
    sol_mainloop_impl_unlock();

    return ready;
}

/* must be called with mainloop lock HELD */
static bool
sol_mainloop_common_source_get_next_timeout_locked(struct timespec *timeout)
{
    bool found = false;
    struct sol_mainloop_source_common *source;
    uint16_t i;

    source_processing = true;

    SOL_PTR_VECTOR_FOREACH_IDX (&SOURCE_PROCESS, source, i) {
        struct timespec cur;
        bool r;

        if (source->remove_me)
            continue;

        if (!source->type->get_next_timeout)
            continue;

        r = source->type->get_next_timeout((void *)source->data, &cur);
        if (r) {
            if (!found || sol_util_timespec_compare(&cur, timeout) < 0) {
                if (cur.tv_sec < 0) {
                    cur.tv_sec = 0;
                    cur.tv_nsec = 0;
                }
                *timeout = cur;
            }
            found = true;
        }
    }

    source_processing = false;

    return found;
}

bool
sol_mainloop_common_source_get_next_timeout(struct timespec *timeout)
{
    bool r;

    sol_mainloop_impl_lock();
    r = sol_mainloop_common_source_get_next_timeout_locked(timeout);
    sol_mainloop_impl_unlock();

    return r;
}

bool
sol_mainloop_common_source_check(void)
{
    struct sol_mainloop_source_common *source;
    uint16_t i;
    bool ready = false;

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&SOURCE_PROCESS, &SOURCE_ACUM);
    source_processing = true;
    sol_mainloop_impl_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&SOURCE_PROCESS, source, i) {
        if (!sol_mainloop_common_loop_check())
            break;
        if (source->remove_me)
            continue;
        source->ready |= source->type->check((void *)source->data);
        ready |= source->ready;
    }

    sol_mainloop_impl_lock();
    sol_ptr_vector_update(&SOURCE_ACUM, &SOURCE_PROCESS);
    source_cleanup();
    source_processing = false;
    sol_mainloop_impl_unlock();

    return ready;
}

void
sol_mainloop_common_source_dispatch(void)
{
    struct sol_mainloop_source_common *source;
    uint16_t i;

    sol_mainloop_impl_lock();
    sol_ptr_vector_steal(&SOURCE_PROCESS, &SOURCE_ACUM);
    source_processing = true;
    sol_mainloop_impl_unlock();

    SOL_PTR_VECTOR_FOREACH_IDX (&SOURCE_PROCESS, source, i) {
        if (!sol_mainloop_common_loop_check())
            break;
        if (source->remove_me)
            continue;
        if (source->ready) {
            source->type->dispatch((void *)source->data);
            source->ready = false;
        }
    }

    sol_mainloop_impl_lock();
    sol_ptr_vector_update(&SOURCE_ACUM, &SOURCE_PROCESS);
    source_cleanup();
    source_processing = false;
    sol_mainloop_impl_unlock();
}

#ifndef SOL_PLATFORM_CONTIKI
void
sol_mainloop_impl_run(void)
{
    if (!sol_mainloop_impl_main_thread_check()) {
        SOL_ERR("sol_run() called on different thread than sol_init()");
        return;
    }

    sol_mainloop_common_loop_set(true);
    while (sol_mainloop_common_loop_check())
        sol_mainloop_impl_iter();
}
#endif

void
sol_mainloop_impl_quit(void)
{
    sol_mainloop_common_loop_set(false);
    sol_mainloop_impl_main_thread_notify();
}

void *
sol_mainloop_impl_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data)
{
    struct timespec now;
    int ret;
    struct sol_timeout_common *timeout = malloc(sizeof(struct sol_timeout_common));

    SOL_NULL_CHECK(timeout, NULL);

    sol_mainloop_impl_lock();

    timeout->timeout.tv_sec = timeout_ms / MSEC_PER_SEC;
    timeout->timeout.tv_nsec = (timeout_ms % MSEC_PER_SEC) * NSEC_PER_MSEC;
    timeout->cb = cb;
    timeout->data = data;
    timeout->remove_me = false;

    now = sol_util_timespec_get_current();
    sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
    ret = sol_ptr_vector_insert_sorted(&timeout_vector, timeout, timeout_compare);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    sol_mainloop_common_main_thread_check_notify();
    sol_mainloop_impl_unlock();

    return timeout;

clean:
    sol_mainloop_impl_unlock();
    free(timeout);
    return NULL;
}

bool
sol_mainloop_impl_timeout_del(void *handle)
{
    struct sol_timeout_common *timeout = handle;

    SOL_EXP_CHECK(timeout->remove_me == true, false);

    sol_mainloop_impl_lock();

    timeout->remove_me = true;
    timeout_pending_deletion++;
    if (!timeout_processing)
        timeout_cleanup();

    sol_mainloop_impl_unlock();

    return true;
}

void *
sol_mainloop_impl_idle_add(bool (*cb)(void *data), const void *data)
{
    int ret;
    struct sol_idler_common *idler = malloc(sizeof(struct sol_idler_common));

    SOL_NULL_CHECK(idler, NULL);

    sol_mainloop_impl_lock();

    idler->cb = cb;
    idler->data = data;

    idler->status = idler_processing ? idler_ready_on_next_iteration : idler_ready;
    ret = sol_ptr_vector_append(&idler_vector, idler);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    sol_mainloop_common_main_thread_check_notify();
    sol_mainloop_impl_unlock();

    return idler;

clean:
    sol_mainloop_impl_unlock();
    free(idler);
    return NULL;
}

bool
sol_mainloop_impl_idle_del(void *handle)
{
    struct sol_idler_common *idler = handle;

    SOL_INT_CHECK(idler->status, == idler_deleted, false);

    sol_mainloop_impl_lock();

    idler->status = idler_deleted;
    idler_pending_deletion++;
    if (!idler_processing)
        idler_cleanup();

    sol_mainloop_impl_unlock();

    return true;
}

void *
sol_mainloop_impl_source_new(const struct sol_mainloop_source_type *type, const void *data)
{
    int ret;
    struct sol_mainloop_source_common *source = malloc(sizeof(struct sol_mainloop_source_common));

    SOL_NULL_CHECK(source, NULL);

    sol_mainloop_impl_lock();

    source->type = type;
    source->data = data;
    source->remove_me = false;
    source->ready = false;

    ret = sol_ptr_vector_append(&source_vector, source);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);

    sol_mainloop_common_main_thread_check_notify();
    sol_mainloop_impl_unlock();

    return source;

clean:
    sol_mainloop_impl_unlock();
    free(source);
    return NULL;
}

void
sol_mainloop_impl_source_del(void *handle)
{
    struct sol_mainloop_source_common *source = handle;

    SOL_EXP_CHECK(source->remove_me == true);

    sol_mainloop_impl_lock();

    source->remove_me = true;
    source_pending_deletion++;
    if (!source_processing)
        source_cleanup();

    sol_mainloop_impl_unlock();
}

void *
sol_mainloop_impl_source_get_data(const void *handle)
{
    const struct sol_mainloop_source_common *source = handle;

    return (void *)source->data;
}
