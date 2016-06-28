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

#pragma once

#include <stdbool.h>

#define SOL_LOG_DOMAIN &_sol_worker_thread_log_domain
extern struct sol_log_domain _sol_worker_thread_log_domain;
#include "sol-log-internal.h"
#include "sol-worker-thread.h"

void *sol_worker_thread_impl_new(const struct sol_worker_thread_config *config);
void sol_worker_thread_impl_cancel(void *handle);
bool sol_worker_thread_impl_cancel_check(const void *handle);
void sol_worker_thread_impl_feedback(void *handle);
