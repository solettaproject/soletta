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

/* Zephyr includes */
#include "nanokernel.h"

#include "sol-util.h"

struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec ret;
    int64_t ticks;

    ticks = sys_tick_get();
    ret.tv_sec = ticks / sys_clock_ticks_per_sec;
    ticks -= ret.tv_sec * sys_clock_ticks_per_sec;
    ret.tv_nsec = (ticks * NSEC_PER_SEC) / sys_clock_ticks_per_sec;

    return ret;
}

int
sol_util_timespec_get_realtime(struct timespec *t)
{
    errno = ENOSYS;
    return -1;
}
