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

#pragma once

#include <time.h>

#include "sol-util.h"
#include "sol-vector.h"

struct sol_timeout_common {
    struct timespec timeout;
    struct timespec expire;
    const void *data;
    bool (*cb)(void *data);
    bool remove_me;
};

struct sol_idler_common {
    const void *data;
    bool (*cb)(void *data);
    enum { idler_ready, idler_deleted, idler_ready_on_next_iteration } status;
};

/* must be called with mainloop lock held */
static inline void
sol_ptr_vector_steal(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
#ifdef PTHREAD
    *to = *from;
    sol_ptr_vector_init(from);
#endif
}

/* must be called with mainloop lock held */
static inline void
sol_ptr_vector_update(struct sol_ptr_vector *to, struct sol_ptr_vector *from)
{
#ifdef PTHREAD
    void *itr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (from, itr, i)
        sol_ptr_vector_append(to, itr);
    sol_ptr_vector_clear(from);
#endif
}

bool sol_mainloop_common_loop_check(void);
void sol_mainloop_common_loop_set(bool val);
void sol_mainloop_common_timeout_process(void);
void sol_mainloop_common_idler_process(void);
struct sol_timeout_common *sol_mainloop_common_timeout_first(void);
struct sol_idler_common *sol_mainloop_common_idler_first(void);

/* thread-related platform specific functions */
bool sol_mainloop_impl_main_thread_check(void);
void sol_mainloop_impl_main_thread_notify(void);
void sol_mainloop_impl_lock(void);
void sol_mainloop_impl_unlock(void);

/* platform specific mainloop functions */
int sol_mainloop_impl_platform_init(void);
void sol_mainloop_impl_platform_shutdown(void);
void sol_mainloop_impl_iter(void);

static inline void
sol_mainloop_common_main_thread_check_notify(void)
{
#ifdef PTHREAD
    if (!sol_mainloop_impl_main_thread_check())
        sol_mainloop_impl_main_thread_notify();
#endif
}
