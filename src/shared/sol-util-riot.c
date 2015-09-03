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

#include "sol-util.h"

/* RIOT headers */
#include <vtimer.h>

#if FEATURE_PERIPH_RTC
#include <periph/rtc.h>
#endif

struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec tp;
    timex_t t;

    vtimer_now(&t);
    tp.tv_sec = t.seconds;
    tp.tv_nsec = t.microseconds * 1000;
    return tp;
}

int
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

int
sol_util_uuid_gen(bool upcase,
    bool with_hyphens,
    char id[static 37])
{
    //FIXME: use whatever source there is of pseudo-random numbers on
    //Riot
    SOL_WRN("Not implemented");
    return -ENOSYS;
}
