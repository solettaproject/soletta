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

/**
 * @file
 * @brief Useful general routines.
 */

/**
 * @defgroup Utilities
 *
 * @brief Contains helpers to manipulate time and error code/string.
 *
 * @{
 */

/**
 * @brief number of nanoseconds in a second: 1,000,000,000
 */
#define SOL_NSEC_PER_SEC  1000000000ULL

/**
 * @brief number of milliseconds in a second: 1,000
 */
#define SOL_MSEC_PER_SEC  1000ULL

/**
 * @brief number of microseconds in a second: 1,000,000
 */
#define SOL_USEC_PER_SEC  1000000ULL

/**
 * @brief number of nanoseconds in a milliseconds: 1,000,000,000 / 1,000 = 1,000,000
 */
#define SOL_NSEC_PER_MSEC 1000000ULL

/**
 * @brief  number of nanoseconds in a microsecond: 1,000,000,000 / 1,000,000 = 1,000
 */
#define SOL_NSEC_PER_USEC 1000ULL

/**
 * @brief Gets the current time (Monotonic)
 *
 * @return The current time represented in @c struct timespec
 */
struct timespec sol_util_timespec_get_current(void);

/**
 * @brief Gets the current time (System-wide clock)
 *
 * @param t Variable used to store the time.
 *
 * @return 0 for success, or -1 for failure (in which case errno
       is set appropriately).
 */
int sol_util_timespec_get_realtime(struct timespec *t);

/**
 * @brief Sum two times
 *
 * @param t1 First time value used on operation.
 * @param t2 Second time value used on operation.
 * @param result Variable used to store the sum's result.
 */
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

/**
 * @brief Subtracts two times
 *
 * @param t1 First time value used on operation.
 * @param t2 Second time value used on operation.
 * @param result Variable used to store the subtraction's result.
 */
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

/**
 * @brief Compare two times
 *
 * Function to compare two times. It returns an integer less than,
 * equal to, or greater than zero.
 *
 * @param t1 First time value used on operation.
 * @param t2 Second time value used on operation.
 *
 * @result 0 if equal, -1 if t2 is greater or 1 otherwise.
 */
static inline int
sol_util_timespec_compare(const struct timespec *t1, const struct timespec *t2)
{
    int retval = (t1->tv_sec > t2->tv_sec) - (t1->tv_sec < t2->tv_sec);

    if (retval != 0)
        return retval;
    return (t1->tv_nsec > t2->tv_nsec) - (t1->tv_nsec < t2->tv_nsec);
}

/**
 * @brief Create a @c struct timespec from milliseconds.
 *
 * @param msec The number of milliseconds.
 *
 * @result a @c struct timespec representing @c msec milliseconds.
 */
static inline struct timespec
sol_util_timespec_from_msec(const int msec)
{
    struct timespec ts;

    ts.tv_sec = msec / SOL_MSEC_PER_SEC;
    ts.tv_nsec = (msec % SOL_MSEC_PER_SEC) * SOL_NSEC_PER_MSEC;
    return ts;
}

/**
 * @brief Gets the number of milliseconds for given time.
 *
 * @param ts The struct timespec to get the milliseconds.
 *
 * @result the number of milliseconds on @c ts.
 */
static inline int
sol_util_msec_from_timespec(const struct timespec *ts)
{
    return ts->tv_sec * SOL_MSEC_PER_SEC + ts->tv_nsec / SOL_NSEC_PER_MSEC;
}

/**
 * @brief Gets a string from a given error.
 *
 * The function returns a pointer to a string that describes the error
 * code passed in the argument @c errnum.
 *
 * @param errnum The error code
 * @param buf Buffer used to store the store the string.
 * @param buflen The size of @c buf
 *
 * @result return the appropriate error description string.
 *
 * @see sol_util_strerrora
 */
char *sol_util_strerror(int errnum, char *buf, size_t buflen);

/**
 * @brief Gets a string from a given error using the stack.
 *
 * The function returns a pointer to a string (created using the
 * stack) that describes the error code passed in the argument @c
 * errnum.
 *
 * @param errnum The error code
 *
 * @result return the appropriate error description string.
 *
 * @see sol_util_strerror
 */
#define sol_util_strerrora(errnum) \
    ({ \
        char buf ## __COUNT__[512]; \
        sol_util_strerror((errnum), buf ## __COUNT__, sizeof(buf ## __COUNT__)); \
    })

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
