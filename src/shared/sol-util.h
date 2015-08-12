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

#include "sol-common-buildopts.h"
#include "sol-macros.h"
#include "sol-str-slice.h"
#include "sol-vector.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>

#ifdef SOL_PLATFORM_LINUX
#include "sol-util-linux.h"
#endif

/** constants used to calculate mul/add operations overflow */
#define OVERFLOW_TYPE(type) ((type)1 << (sizeof(type) * 4))
#define OVERFLOW_UINT64 OVERFLOW_TYPE(uint64_t)
#define OVERFLOW_SIZE_T OVERFLOW_TYPE(size_t)

#define streq(a, b) (strcmp((a), (b)) == 0)
#define streqn(a, b, n) (strncmp((a), (b), (n)) == 0)
#define strstartswith(a, b) streqn((a), (b), strlen(b))

/**
 * Wrapper over strtod() that consumes up to @c len bytes and may not
 * use a locale.
 *
 * This variation of strtod() will work with buffers that are not
 * null-terminated.
 *
 * It also offers a way to skip the currently set locale, forcing
 * plain "C". This is required to parse numbers in formats that
 * require '.' as the decimal point while the current locale may use
 * ',' such as in pt_BR.
 *
 * All the formats accepted by strtod() are accepted and the behavior
 * should be the same, including using information from @c LC_NUMERIC
 * if locale is configured and @a use_locale is true.
 *
 * @param nptr the string containing the number to convert.
 * @param endptr if non-NULL, it will contain the last character used
 *        in the conversion. If no conversion was done, endptr is @a nptr.
 * @param use_locale if true, then current locale is used, if false
 *        then "C" locale is forced.
 *
 * @param len use at most this amount of bytes of @a nptr. If -1, assumes
 *        nptr has a trailing NUL and calculate the string length.
 *
 * @return the converted value, if any. The converted value may be @c
 *         NAN, @c INF (positive or negative). See the strtod(3)
 *         documentation for the details.
 */
double sol_util_strtodn(const char *nptr, char **endptr, ssize_t len, bool use_locale);

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
int sol_util_timespec_get_realtime(struct timespec *t);

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

/**
 * Return a list of the words in a given string slice, using a given
 * delimiter string. If @a maxsplit is given, at most that number of
 * splits are done (thus, the list will have at most @c maxsplit+1
 * elements). If @c maxsplit is zero, then there is no limit on the
 * number of splits (all possible splits are made).
 *
 * @param slice The string slice to divide in sub-strings (in array of
 *              slices form)
 * @param delim The delimiter string to divide @a slice based on
 * @param maxsplit The maximum number of splits to make on @a slice.
 *                 If it's 0, than make as many splits as it can.
 *
 * @return A vector of string slices with the splitted words, on
 *         success, or @c NULL, otherwise.
 */
struct sol_vector sol_util_str_split(const struct sol_str_slice slice, const char *delim, size_t maxsplit);

static inline int
sol_util_size_mul(size_t elem_size, size_t num_elems, size_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(elem_size, num_elems, out))
        return -EOVERFLOW;
#else
    if ((elem_size >= OVERFLOW_SIZE_T || num_elems >= OVERFLOW_SIZE_T) &&
        elem_size > 0 && SIZE_MAX / elem_size < num_elems)
        return -EOVERFLOW;
    *out = elem_size * num_elems;
#endif
    return 0;
}

static inline int
sol_util_uint64_mul(const uint64_t a, const uint64_t b, uint64_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if ((a >= OVERFLOW_UINT64 || b >= OVERFLOW_UINT64) &&
        a > 0 && UINT64_MAX / a < b)
        return -EOVERFLOW;
    *out = a * b;
#endif
    return 0;
}

static inline int
sol_util_uint64_add(const uint64_t a, const uint64_t b, uint64_t *out)
{
#ifdef HAVE_BUILTIN_ADD_OVERFLOW
    if (__builtin_add_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if (a > 0 && b > UINT64_MAX - a)
        return -EOVERFLOW;
    *out = a + b;
#endif
    return 0;
}
