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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>


#ifdef __cplusplus
extern "C" {
#endif

/* number of nanoseconds in a second: 1,000,000,000 */
#define SOL_NSEC_PER_SEC  1000000000ULL
/* number of milliseconds in a second: 1,000 */
#define SOL_MSEC_PER_SEC  1000ULL
/* number of microseconds in a second: 1,000,000 */
#define SOL_USEC_PER_SEC  1000000ULL
/* number of nanoseconds in a milliseconds: 1,000,000,000 / 1,000 = 1,000,000 */
#define SOL_NSEC_PER_MSEC 1000000ULL
/* number of nanoseconds in a microsecond: 1,000,000,000 / 1,000,000 = 1,000 */
#define SOL_NSEC_PER_USEC 1000ULL

struct timespec sol_util_timespec_get_current(void);

int sol_util_timespec_get_realtime(struct timespec *t);

static inline void
sol_util_timespec_sum(const struct timespec *t1, const struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec + t2->tv_nsec;
    result->tv_sec = t1->tv_sec + t2->tv_sec;
    if ((unsigned long long)result->tv_nsec >= SOL_NSEC_PER_SEC) {
        result->tv_nsec -= SOL_NSEC_PER_SEC;
        result->tv_sec++;
    }
}

static inline void
sol_util_timespec_sub(const struct timespec *t1, const struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec - t2->tv_nsec;
    result->tv_sec = t1->tv_sec - t2->tv_sec;
    if (result->tv_nsec < 0) {
        result->tv_nsec += SOL_NSEC_PER_SEC;
        result->tv_sec--;
    }
}

static inline int
sol_util_timespec_compare(const struct timespec *t1, const struct timespec *t2)
{
    int retval = (t1->tv_sec > t2->tv_sec) - (t1->tv_sec < t2->tv_sec);

    if (retval != 0)
        return retval;
    return (t1->tv_nsec > t2->tv_nsec) - (t1->tv_nsec < t2->tv_nsec);
}

static inline struct timespec
sol_util_timespec_from_msec(const int msec)
{
    struct timespec ts;

    ts.tv_sec = msec / SOL_MSEC_PER_SEC;
    ts.tv_nsec = (msec % SOL_MSEC_PER_SEC) * SOL_NSEC_PER_MSEC;
    return ts;
}

static inline int
sol_util_msec_from_timespec(const struct timespec *ts)
{
    return ts->tv_sec * SOL_MSEC_PER_SEC + ts->tv_nsec / SOL_NSEC_PER_MSEC;
}

char *sol_util_strerror(int errnum, char *buf, size_t buflen);

#define sol_util_strerrora(errnum) \
    ({ \
        char buf ## __COUNT__[512]; \
        sol_util_strerror((errnum), buf ## __COUNT__, sizeof(buf ## __COUNT__)); \
    })

#ifdef __cplusplus
}
#endif
