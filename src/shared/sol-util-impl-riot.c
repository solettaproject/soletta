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

/* RIOT headers */
#include <xtimer.h>

#if FEATURE_PERIPH_RTC
#include <periph/rtc.h>
#endif

SOL_API struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec tp;
    timex_t t;

    xtimer_now_timex(&t);
    tp.tv_sec = t.seconds;
    tp.tv_nsec = t.microseconds * 1000;
    return tp;
}

SOL_API int
sol_util_timespec_get_realtime(struct timespec *t)
{
#if FEATURE_PERITH_RTC
    struct tm rtc;
    if (rtc_get_time(&rtc) != 0) {
        errno = EINVAL;
        return -1;
    }
    t.tv_sec = mktime(&rtc);
    t.tv_nsec = 0;
    return 0;
#else
    errno = ENOSYS;
    return -1;
#endif
}
