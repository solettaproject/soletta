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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef SOL_PLATFORM_LINUX
#include "sol-util-file.h"
#endif

/**
 * Constants used to calculate mul/add operations overflow
 *
 * This is sqrt(TYPE_MAX + 1), as s1 * s2 <= TYPE_MAX
 * if both s1 < MUL_NO_OVERFLOW and s2 < MUL_NO_OVERFLOW
 */
#define OVERFLOW_TYPE(type) ((type)1 << (sizeof(type) * 4))
#define OVERFLOW_UINT64 OVERFLOW_TYPE(uint64_t)
#define OVERFLOW_SIZE_T OVERFLOW_TYPE(size_t)

/**
 * Extracted from Hacker's Delight, 2nd edition, chapter 2-13
 * (Overflow Dectection), table 2-2
 */
#define OVERFLOW_SSIZE_T_POS (ssize_t)(SIZE_MAX / 2)
#define OVERFLOW_SSIZE_T_NEG (ssize_t)(((SIZE_MAX / 2) * -1) - 1)

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
sol_util_timespec_sum(const struct timespec *t1, const struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec + t2->tv_nsec;
    result->tv_sec = t1->tv_sec + t2->tv_sec;
    if ((unsigned long long)result->tv_nsec >= NSEC_PER_SEC) {
        result->tv_nsec -= NSEC_PER_SEC;
        result->tv_sec++;
    }
}

static inline void
sol_util_timespec_sub(const struct timespec *t1, const struct timespec *t2, struct timespec *result)
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

/* Power of 2 alignment */
#define DEFINE_ALIGN_POWER2(name_, type_, max_, clz_fn_) \
    static inline type_ \
    name_(type_ u) \
    { \
        unsigned int left_zeros; \
        if (u <= 1) /* 0 is undefined for __builtin_clz() */ \
            return u; \
        left_zeros = clz_fn_(u - 1); \
        if (unlikely(left_zeros <= 1)) \
            return max_; \
        return (type_)1 << ((sizeof(u) * 8) - left_zeros); \
    }

DEFINE_ALIGN_POWER2(align_power2_uint, unsigned int, UINT_MAX, __builtin_clz)
#if SIZE_MAX == ULONG_MAX
DEFINE_ALIGN_POWER2(align_power2_size, size_t, SIZE_MAX, __builtin_clzl)
#elif SIZE_MAX == ULLONG_MAX
DEFINE_ALIGN_POWER2(align_power2_size, size_t, SIZE_MAX, __builtin_clzll)
#elif SIZE_MAX == UINT_MAX
DEFINE_ALIGN_POWER2(align_power2_size, size_t, SIZE_MAX, __builtin_clz)
#else
#error Unsupported size_t size
#endif

#undef DEFINE_ALIGN_POWER2

static inline int
align_power2_short_uint(unsigned short u)
{
    unsigned int aligned = align_power2_uint(u);

    return likely(aligned <= USHRT_MAX) ? aligned : USHRT_MAX;
}

/* _Generic() from C11 would be better here, but it's not supported by
 * the environment in Semaphore CI. */
#define align_power2(u_) \
    ({ \
        typeof((u_) + 0)pow2__aligned; /* + 0 to remove const */ \
        if (__builtin_types_compatible_p(typeof(u_), size_t)) { \
            pow2__aligned = align_power2_size(u_); \
        } else if (__builtin_types_compatible_p(typeof(u_), unsigned int)) { \
            pow2__aligned = align_power2_uint(u_); \
        } else if (__builtin_types_compatible_p(typeof(u_), short unsigned int)) { \
            pow2__aligned = align_power2_short_uint(u_); \
        } else { \
            __builtin_unreachable(); \
        } \
        pow2__aligned; \
    })

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
sol_util_ssize_mul(ssize_t op1, ssize_t op2, ssize_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(op1, op2, out))
        return -EOVERFLOW;
#else
    bool overflow = false;

    if (op1 > 0 && op2 > 0) {
        overflow = op1 > OVERFLOW_SSIZE_T_POS / op2;
    } else if (op1 > 0 && op2 <= 0) {
        overflow = op2 < OVERFLOW_SSIZE_T_NEG / op1;
    } else if (op1 <= 0 && op2 > 0) {
        overflow = op1 < OVERFLOW_SSIZE_T_NEG / op2;
    } else { // op1 <= 0 && op2 <= 0
        overflow = op1 != 0 && op2 < OVERFLOW_SSIZE_T_POS / op1;
    }

    if (overflow)
        return -EOVERFLOW;

    *out = op1 * op2;
#endif
    return 0;
}

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
sol_util_size_add(const size_t a, const size_t b, size_t *out)
{
#ifdef HAVE_BUILTIN_ADD_OVERFLOW
    if (__builtin_add_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if (a > 0 && b > SIZE_MAX - a)
        return -EOVERFLOW;
    *out = a + b;
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

/**
 * Generates a new universally unique identifier (UUID), in string
 * form, which is 16 bytes-long (128 bits) long and conforms to v4
 * UUIDs (generated from random—or pseudo-random—numbers).
 *
 * @param upcase Whether to generate the UUID in upcase or not
 * @param with_hyphens Format the resulting UUID string with hyphens
 *                     (e.g. "de305d54-75b4-431b-adb2-eb6b9e546014") or
 *                     without them.
 * @param id Where to store the generated id. It's 37 bytes in lenght
 *           so it accomodates the maximum lenght case -- 2 * 16
 *           (chars) + 4 (hyphens) + 1 (\0)
 *
 * @return 0 on success, negative error code otherwise.
 */
int sol_util_uuid_gen(bool upcase, bool with_hyphens, char id[static 37]);

/**
 * Checks if a given universally unique identifier (UUID), in string
 * form, is valid (all upcase/downcase, hyphenated/non-hyphenated
 * cases included).
 *
 * @param str The given UUID
 *
 * @return true if it's valid, false otherwise.
 */
static inline bool
sol_util_uuid_str_valid(const char *str)
{
    size_t i, len;

    len = strlen(str);
    if (len == 32) {
        for (i = 0; i < len; i++) {
            if (!isxdigit(str[i]))
                return false;
        }
    } else if (len == 36) {
        char c;
        for (i = 0; i < len; i++) {
            c = str[i];

            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-')
                    return false;
            } else if (!isxdigit(c))
                return false;
        }
    } else
        return false;

    return true;
}

static inline int32_t
sol_util_int32_clamp(int32_t start, int32_t end, int32_t value)
{
    if (value < start)
        return start;
    if (value > end)
        return end;
    return value;
}

int sol_util_replace_str_if_changed(char **str, const char *new_str);

/**
 * Encode the binary slice to base64 using the given map.
 *
 * https://en.wikipedia.org/wiki/Base64
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the encoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the encoded slice, that is:
 *        (slice.len / 3 + 1) * 4
 * @param slice the slice to encode, it may contain null-bytes (\0),
 *        the whole size of the slice will be used (slice.len).
 * @param base64_map the map to use. The last char is used as the
 *        padding character if slice length is not multiple of 3 bytes.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base64_encode(void *buf, size_t buflen, const struct sol_str_slice slice, const char base64_map[static 65]);

/**
 * Decode the binary slice from base64 using the given map.
 *
 * https://en.wikipedia.org/wiki/Base64
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the decoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the decoded slice, that is:
 *        (slice.len / 4) * 3
 * @param slice the slice to decode, it must be composed solely of the
 *        base64_map characters or it will fail.
 * @param base64_map the map to use. The last char is used as the
 *        padding character if slice length is not multiple of 3 bytes.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base64_decode(void *buf, size_t buflen, const struct sol_str_slice slice, const char base64_map[static 65]);

static inline ssize_t
sol_util_base64_calculate_encoded_len(const struct sol_str_slice slice, const char base64_map[static 65])
{
    ssize_t req_len = slice.len / 3;
    int err;

    if (slice.len % 3 != 0)
        req_len++;
    err = sol_util_ssize_mul(req_len, 4, &req_len);
    if (err < 0)
        return err;
    return req_len;
}

static inline ssize_t
sol_util_base64_calculate_decoded_len(const struct sol_str_slice slice, const char base64_map[static 65])
{
    size_t req_len = (slice.len / 4) * 3;
    size_t i;

    for (i = slice.len; i > 0; i--) {
        if (slice.data[i - 1] != base64_map[64])
            break;
        req_len--;
    }
    if (req_len > SSIZE_MAX)
        return -EOVERFLOW;
    return req_len;
}

/**
 * Encode the binary slice to base16 (hexadecimal).
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the encoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the encoded slice, that is:
 *        slice.len * 2
 * @param slice the slice to encode, it may contain null-bytes (\0),
 *        the whole size of the slice will be used (slice.len).
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base16_encode(void *buf, size_t buflen, const struct sol_str_slice slice, bool uppercase);

/**
 * Decode the binary slice from base16 (hexadecimal).
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the decoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the decoded slice, that is:
 *        slice.len / 2.
 * @param slice the slice to decode, it must be a set of 0-9 or
 *        letters A-F (if uppercase) or a-f, otherwise decode fails.
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base16_decode(void *buf, size_t buflen, const struct sol_str_slice slice, bool uppercase);

static inline ssize_t
sol_util_base16_calculate_encoded_len(const struct sol_str_slice slice)
{
    ssize_t req_len;
    int err = sol_util_ssize_mul(slice.len, 2, &req_len);

    if (err < 0)
        return err;
    return req_len;
}

static inline ssize_t
sol_util_base16_calculate_decoded_len(const struct sol_str_slice slice)
{
    return slice.len / 2;
}
