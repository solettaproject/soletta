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

#include "sol-util-internal.h"

/* Contiki headers */
#include <contiki.h>

SOL_API struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec ret;
    clock_time_t ticks;

    ticks = clock_time();
    ret.tv_sec = ticks / CLOCK_SECOND;
    ticks -= ret.tv_sec * CLOCK_SECOND;
    ret.tv_nsec = (ticks * SOL_UTIL_NSEC_PER_SEC) / CLOCK_SECOND;
    return ret;
}

SOL_API int
sol_util_timespec_get_realtime(struct timespec *t)
{
    errno = ENOSYS;
    return -1;
}
