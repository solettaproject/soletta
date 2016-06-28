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

#include "sol-util-internal.h"
#include "sol-macros.h"
#include "sol-worker-thread.h"
#include "sol-worker-thread-impl.h"

SOL_LOG_INTERNAL_DECLARE(_sol_worker_thread_log_domain, "worker-thread");

SOL_API struct sol_worker_thread *
sol_worker_thread_new(const struct sol_worker_thread_config *config)
{
    SOL_NULL_CHECK(config, NULL);
    SOL_NULL_CHECK(config->iterate, NULL);

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_WORKER_THREAD_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't create worker thread with unsupported version '%" PRIu16 "', "
            "expected version is '%" PRIu16 "'",
            config->api_version, SOL_WORKER_THREAD_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    return sol_worker_thread_impl_new(config);
}

SOL_API void
sol_worker_thread_cancel(struct sol_worker_thread *thread)
{
    SOL_NULL_CHECK(thread);
    sol_worker_thread_impl_cancel(thread);
}

SOL_API bool
sol_worker_thread_is_cancelled(const struct sol_worker_thread *thread)
{
    SOL_NULL_CHECK(thread, false);
    return sol_worker_thread_impl_cancel_check(thread);
}

SOL_API void
sol_worker_thread_feedback(struct sol_worker_thread *thread)
{
    SOL_NULL_CHECK(thread);
    sol_worker_thread_impl_feedback(thread);
}
