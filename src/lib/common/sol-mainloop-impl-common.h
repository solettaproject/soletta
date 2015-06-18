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

#include "sol-vector.h"

#define DEFAULT_USLEEP_TIME 10000

extern bool run_loop;

extern bool timeout_processing;
extern unsigned int timeout_pending_deletion;
extern struct sol_ptr_vector timeout_vector;

extern bool idler_processing;
extern unsigned int idler_pending_deletion;
extern struct sol_ptr_vector idler_vector;

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

void sol_mainloop_impl_common_shutdown(void);

void sol_mainloop_impl_common_timeout_cleanup(void);
void sol_mainloop_impl_common_timeout_process(void);
void *sol_mainloop_impl_common_timeout_add(unsigned int timeout_ms, bool (*cb)(void *data), const void *data);
bool sol_mainloop_impl_common_timeout_del(void *handle);

void sol_mainloop_impl_common_idler_cleanup(void);
void sol_mainloop_impl_common_idler_process(void);
void *sol_mainloop_impl_common_idle_add(bool (*cb)(void *data), const void *data);
bool sol_mainloop_impl_common_idle_del(void *handle);
