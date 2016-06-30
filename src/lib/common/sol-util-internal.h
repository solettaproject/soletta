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
#define strendswith(a, b) (strlen(b) < strlen(a) && streq(a + strlen(a) - strlen(b), b))

#define STATIC_ASSERT_LITERAL(_s) ("" _s)

#define PTR_TO_INT(p) ((int)((intptr_t)(p)))
#define INT_TO_PTR(i) ((void *)((intptr_t)(i)))

static inline int
sol_util_int_compare(int a, int b)
{
    return (a > b) - (a < b);
}

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

/* abort() may not always be completely available on small systems */
#ifdef SOL_PLATFORM_LINUX
#define sol_abort() abort()
#else
#define sol_abort() exit(EXIT_FAILURE)
#endif

/**
 * @brief Amount of bytes to read in a single call
 */
#define CHUNK_READ_SIZE 1024

/**
 * @brief Allow reading loop to take up to this amount of bytes
 *
 * When this amount is reached, stops the chunk reading and allow mainloop to run again.
 * This keeps memory usage low.
 */
#define CHUNK_READ_MAX (10 * (CHUNK_READ_SIZE))

/**
 * @brief Allow reading/writing loop to take up to this nanoseconds.
 *
 * When this time is reached, stops the * chunk reading and allow mainloop to run again.
 * This keeps interactivity.
 */
#define CHUNK_MAX_TIME_NS (20 * 1000000)
