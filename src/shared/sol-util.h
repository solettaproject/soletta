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

#include "sol-macros.h"
#include "sol-str-slice.h"
#include "sol-vector.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#define SOL_UTIL_MAX_READ_ATTEMPTS 10

#define streq(a, b) (strcmp((a), (b)) == 0)
#define streqn(a, b, n) (strncmp((a), (b), (n)) == 0)

#define STATIC_ASSERT_LITERAL(_s) ("" _s)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define PTR_TO_INT(p) ((int)((intptr_t)(p)))
#define INT_TO_PTR(i) ((void *)((intptr_t)(i)))

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* number of nanoseconds in a second: 1,000,000,000 */
#define NSEC_PER_SEC  1000000000ULL
/* number of milliseconds in a second: 1,000 */
#define MSEC_PER_SEC  1000ULL
/* number of microseconds in a second: 1,000,000 */
#define USEC_PER_SEC  1000000ULL
/* number of nanoseconds in a milliseconds: 1,000,000,000 / 1,000 = 1,000,000 */
#define NSEC_PER_MSEC 1000000ULL
/* number of nanoseconds in a microsecond: 1,000,000,000 / 1,000,000 = 1,000 */
#define NSEC_PER_USEC 1000ULL

static inline int
sol_util_int_compare(const int a, const int b)
{
    return (a > b) - (a < b);
}

struct timespec sol_util_timespec_get_current(void);

static inline void
sol_util_timespec_sum(struct timespec *t1, struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec + t2->tv_nsec;
    result->tv_sec = t1->tv_sec + t2->tv_sec;
    if ((unsigned long long)result->tv_nsec >= NSEC_PER_SEC) {
        result->tv_nsec -= NSEC_PER_SEC;
        result->tv_sec++;
    }
}

static inline void
sol_util_timespec_sub(struct timespec *t1, struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec - t2->tv_nsec;
    result->tv_sec = t1->tv_sec - t2->tv_sec;
    if (result->tv_nsec < 0) {
        result->tv_nsec += NSEC_PER_SEC;
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

    ts.tv_sec = msec / MSEC_PER_SEC;
    ts.tv_nsec = (msec % MSEC_PER_SEC) * NSEC_PER_MSEC;
    return ts;
}

static inline int
sol_util_msec_from_timespec(const struct timespec *ts)
{
    return ts->tv_sec * MSEC_PER_SEC + ts->tv_nsec / NSEC_PER_MSEC;
}

int sol_util_write_file(const char *path, const char *fmt, ...) SOL_ATTR_PRINTF(2, 3);
int sol_util_vwrite_file(const char *path, const char *fmt, va_list args) SOL_ATTR_PRINTF(2, 0);
int sol_util_read_file(const char *path, const char *fmt, ...) SOL_ATTR_SCANF(2, 3);
int sol_util_vread_file(const char *path, const char *fmt, va_list args) SOL_ATTR_SCANF(2, 0);
void *sol_util_load_file_raw(const int fd, size_t *size);
char *sol_util_load_file_string(const char *filename, size_t *size);
void *sol_util_memdup(const void *data, size_t len);

char *sol_util_strerror(int errnum, char *buf, size_t buflen);

#define sol_util_strerrora(errnum) \
    ({ \
         char buf ## __COUNT__[512]; \
         sol_util_strerror((errnum), buf ## __COUNT__, sizeof(buf ## __COUNT__)); \
     })

static inline unsigned int
align_power2(unsigned int u)
{
    unsigned int left_zeros;

    if (u == 1)
        return 1;
    if ((left_zeros = __builtin_clz(u - 1)) < 1)
        return 0;
    return 1 << ((sizeof(u) * 8) - left_zeros);
}

/* The given slice 'slice' will be splitted in sub-strings delimited by the 'delim'
 * This function returns an vector of struct sol_str_slice.
 *
 * NOTE: Different from strtoken, it cosiderer the full string in 'delim' and split
 *       up to the number of elements specified in 'maxplit'.
 */
struct sol_vector sol_util_str_split(const struct sol_str_slice slice, const char *delim, size_t maxsplit);
