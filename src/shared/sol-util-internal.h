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
#include "sol-util.h"
#include "sol-vector.h"
#include "sol-buffer.h"

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
#define OVERFLOW_INT64 OVERFLOW_TYPE(int64_t)
#define OVERFLOW_INT32 OVERFLOW_TYPE(int32_t)
#define OVERFLOW_UINT32 OVERFLOW_TYPE(uint32_t)

/**
 * Extracted from Hacker's Delight, 2nd edition, chapter 2-13
 * (Overflow Dectection), table 2-2
 */
#define OVERFLOW_SSIZE_T_POS (ssize_t)(SIZE_MAX / 2)
#define OVERFLOW_SSIZE_T_NEG (ssize_t)(((SIZE_MAX / 2) * -1) - 1)

#define streq(a, b) (strcmp((a), (b)) == 0)
#define streqn(a, b, n) (strncmp((a), (b), (n)) == 0)
#define strstartswith(a, b) streqn((a), (b), strlen(b))

#define STATIC_ASSERT_LITERAL(_s) ("" _s)

#define PTR_TO_INT(p) ((int)((intptr_t)(p)))
#define INT_TO_PTR(i) ((void *)((intptr_t)(i)))

static inline int
sol_util_int_compare(const int a, const int b)
{
    return (a > b) - (a < b);
}

void *sol_util_memdup(const void *data, size_t len);

/* Power of 2 alignment */
#define DEFINE_ALIGN_POWER2(name_, type_, max_, clz_fn_) \
    static inline type_ \
    name_(type_ u) \
    { \
        unsigned int left_zeros; \
        if (u <= 1) /* 0 is undefined for __builtin_clz() */ \
            return u; \
        left_zeros = clz_fn_(u - 1); \
        if (SOL_UNLIKELY(left_zeros <= 1)) \
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

    return SOL_LIKELY(aligned <= USHRT_MAX) ? aligned : USHRT_MAX;
}

/* _Generic() from C11 would be better here, but it's not supported by
 * the environment in Semaphore CI. */
#define align_power2(u_) \
    ({ \
        __typeof__((u_) + 0)pow2__aligned; /* + 0 to remove const */ \
        if (__builtin_types_compatible_p(__typeof__(u_), size_t)) { \
            pow2__aligned = align_power2_size(u_); \
        } else if (__builtin_types_compatible_p(__typeof__(u_), unsigned int)) { \
            pow2__aligned = align_power2_uint(u_); \
        } else if (__builtin_types_compatible_p(__typeof__(u_), short unsigned int)) { \
            pow2__aligned = align_power2_short_uint(u_); \
        } else { \
            __builtin_unreachable(); \
        } \
        pow2__aligned; \
    })

/* These two macros will generate shadowing warnings if the same kind
 * is nested, but the warnings on type mismatch are a fair benefit.
 * Use temporary variables to address nested call cases. */
#define sol_min(x, y) \
    ({ \
        __typeof__(x)_min1 = (x); \
        __typeof__(y)_min2 = (y); \
        (void)(&_min1 == &_min2); \
        _min1 < _min2 ? _min1 : _min2; \
    })

#define sol_max(x, y) \
    ({ \
        __typeof__(x)_max1 = (x); \
        __typeof__(y)_max2 = (y); \
        (void)(&_max1 == &_max2); \
        _max1 > _max2 ? _max1 : _max2; \
    })

#define sol_abs(x) ((x) < 0 ? (-x) : (x))
