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

/* implement these functions for your own system so soletta will work */

#define SOL_LOG_DOMAIN &_sol_mainloop_log_domain
extern struct sol_log_domain _sol_mainloop_log_domain;
#include "sol-log-internal.h"
#include "sol-mainloop.h"


int sol_mainloop_impl_init(void);
void sol_mainloop_impl_run(void);
void sol_mainloop_impl_quit(void);
void sol_mainloop_impl_shutdown(void);

void *sol_mainloop_impl_timeout_add(uint32_t timeout_ms, bool (*cb)(void *data), const void *data);
bool sol_mainloop_impl_timeout_del(void *handle);

void *sol_mainloop_impl_idle_add(bool (*cb)(void *data), const void *data);
bool sol_mainloop_impl_idle_del(void *handle);

#ifdef SOL_MAINLOOP_FD_ENABLED
void *sol_mainloop_impl_fd_add(int fd, uint32_t flags, bool (*cb)(void *data, int fd, uint32_t active_flags), const void *data);
bool sol_mainloop_impl_fd_del(void *handle);
bool sol_mainloop_impl_fd_set_flags(void *handle, uint32_t flags);
uint32_t sol_mainloop_impl_fd_get_flags(const void *handle);
#endif

#ifdef SOL_MAINLOOP_FORK_WATCH_ENABLED
void *sol_mainloop_impl_child_watch_add(uint64_t pid, void (*cb)(void *data, uint64_t pid, int status), const void *data);
bool sol_mainloop_impl_child_watch_del(void *handle);
#endif

void *sol_mainloop_impl_source_add(const struct sol_mainloop_source_type *type, const void *data);
void sol_mainloop_impl_source_del(void *handle);
void *sol_mainloop_impl_source_get_data(const void *handle);
