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

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "sol-mainloop-impl-common.h"
#include "sol-log.h"
#include "sol-vector.h"
#include "sol-util.h"

bool mainloop_common_run_loop;

bool mainloop_common_timeout_processing;
unsigned int mainloop_common_timeout_pending_deletion;
struct sol_ptr_vector mainloop_common_timeout_vector = SOL_PTR_VECTOR_INIT;

bool mainloop_common_idler_processing;
unsigned int mainloop_common_idler_pending_deletion;
struct sol_ptr_vector mainloop_common_idler_vector = SOL_PTR_VECTOR_INIT;

int
sol_mainloop_impl_timeout_compare(const void *data1, const void *data2)
{
    const struct sol_timeout_common *a = data1, *b = data2;

    return sol_util_timespec_compare(&a->expire, &b->expire);
}

void
sol_mainloop_impl_common_shutdown(void)
{
    void *ptr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&mainloop_common_timeout_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&mainloop_common_timeout_vector);

    SOL_PTR_VECTOR_FOREACH_IDX (&mainloop_common_idler_vector, ptr, i) {
        free(ptr);
    }
    sol_ptr_vector_clear(&mainloop_common_idler_vector);
}

void
sol_mainloop_impl_common_timeout_cleanup(void)
{
    struct sol_timeout_common *timeout;
    uint16_t i;

    if (!mainloop_common_timeout_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&mainloop_common_timeout_vector, timeout, i) {
        if (!timeout->remove_me)
            continue;

        sol_ptr_vector_del(&mainloop_common_timeout_vector, i);
        free(timeout);
        mainloop_common_timeout_pending_deletion--;
        if (!mainloop_common_timeout_pending_deletion)
            break;
    }
}

void *
sol_mainloop_impl_common_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data)
{
    struct timespec now;
    int ret;
    struct sol_timeout_common *timeout = malloc(sizeof(struct sol_timeout_common));

    SOL_NULL_CHECK(timeout, NULL);

    timeout->timeout = sol_util_timespec_from_msec(timeout_ms);
    timeout->cb = cb;
    timeout->data = data;
    timeout->remove_me = false;

    now = sol_util_timespec_get_current();
    sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
    ret = sol_ptr_vector_insert_sorted(&mainloop_common_timeout_vector, timeout,
                                       sol_mainloop_impl_timeout_compare);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);
    return timeout;

clean:
    free(timeout);
    return NULL;
}

void
sol_mainloop_impl_common_idler_cleanup(void)
{
    struct sol_idler_common *idler;
    uint16_t i;

    if (!mainloop_common_idler_pending_deletion)
        return;

    // Walk backwards so deletion doesn't impact the indices.
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&mainloop_common_idler_vector, idler, i) {
        if (idler->status != idler_deleted)
            continue;

        sol_ptr_vector_del(&mainloop_common_idler_vector, i);
        free(idler);
        mainloop_common_idler_pending_deletion--;
        if (!mainloop_common_idler_pending_deletion)
            break;
    }
}

void
sol_mainloop_impl_common_idler_process(void)
{
    uint16_t i;
    struct sol_idler_common *idler;

    mainloop_common_idler_processing = true;
    SOL_PTR_VECTOR_FOREACH_IDX (&mainloop_common_idler_vector, idler, i) {
        if (!mainloop_common_run_loop)
            break;
        if (idler->status != idler_ready) {
            if (idler->status == idler_ready_on_next_iteration)
                idler->status = idler_ready;
            continue;
        }
        if (!idler->cb((void *)idler->data)) {
            if (idler->status != idler_deleted) {
                idler->status = idler_deleted;
                mainloop_common_idler_pending_deletion++;
            }
        }
        sol_mainloop_impl_common_timeout_process();
    }

    sol_mainloop_impl_common_idler_cleanup();
    mainloop_common_idler_processing = false;
}

bool
sol_mainloop_impl_common_timeout_del(void *handle)
{
    struct sol_timeout_common *timeout = handle;

    timeout->remove_me = true;
    mainloop_common_timeout_pending_deletion++;
    if (!mainloop_common_timeout_processing)
        sol_mainloop_impl_common_timeout_cleanup();

    return true;
}

void *
sol_mainloop_impl_common_idle_add(bool (*cb)(void *data), const void *data)
{
    int ret;
    struct sol_idler_common *idler = malloc(sizeof(struct sol_idler_common));

    SOL_NULL_CHECK(idler, NULL);
    idler->cb = cb;
    idler->data = data;

    idler->status = mainloop_common_idler_processing ? idler_ready_on_next_iteration : idler_ready;
    ret = sol_ptr_vector_append(&mainloop_common_idler_vector, idler);
    SOL_INT_CHECK_GOTO(ret, != 0, clean);
    return idler;

clean:
    free(idler);
    return NULL;
}

bool
sol_mainloop_impl_common_idle_del(void *handle)
{
    struct sol_idler_common *idler = handle;

    idler->status = idler_deleted;
    mainloop_common_idler_pending_deletion++;
    if (!mainloop_common_idler_processing)
        sol_mainloop_impl_common_idler_cleanup();

    return true;
}

void
sol_mainloop_impl_common_timeout_process(void)
{
    struct timespec now;
    unsigned int i;

    mainloop_common_timeout_processing = true;
    now = sol_util_timespec_get_current();
    for (i = 0; i < mainloop_common_timeout_vector.base.len; i++) {
        struct sol_timeout_common *timeout = sol_ptr_vector_get(&mainloop_common_timeout_vector, i);
        if (!mainloop_common_run_loop)
            break;
        if (timeout->remove_me)
            continue;
        if (sol_util_timespec_compare(&timeout->expire, &now) > 0)
            break;

        if (!timeout->cb((void *)timeout->data)) {
            if (!timeout->remove_me) {
                timeout->remove_me = true;
                mainloop_common_timeout_pending_deletion++;
            }
            continue;
        }

        sol_util_timespec_sum(&now, &timeout->timeout, &timeout->expire);
        sol_ptr_vector_del(&mainloop_common_timeout_vector, i);
        sol_ptr_vector_insert_sorted(&mainloop_common_timeout_vector, timeout,
                                     sol_mainloop_impl_timeout_compare);
        i--;
    }

    sol_mainloop_impl_common_timeout_cleanup();
    mainloop_common_timeout_processing = false;
}
