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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sol-macros.h>
#include <sol-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its string slice implementation.
 */

/**
 * @defgroup Str_Slice String slice
 * @ingroup Datatypes
 *
 * It's a slice of a string with explicit length. It doesn't necessarily ends
 * with NUL byte like C strings. This representation is convenient for
 * referencing to substrings of a larger string without having to
 * duplicate them.
 *
 * @{
 */

#define SOL_STR_STATIC_ASSERT_LITERAL(_s) ("" _s)

#define SOL_STR_SLICE_LITERAL(_s) { (sizeof(SOL_STR_STATIC_ASSERT_LITERAL(_s)) - 1), (_s) }

#define SOL_STR_SLICE_STR(_s, _len) (struct sol_str_slice){.len = (_len), .data = (_s) }

#define SOL_STR_SLICE_EMPTY { .len = 0, .data = "" }

/* To be used together with "%.*s" formatting in printf family of functions. */
#define SOL_STR_SLICE_PRINT(_s) (int)(_s).len, (_s).data

/**
 * @struct sol_str_slice
 *
 * Slice of a string with explicit length. It doesn't necessarily ends
 * with NUL byte like C strings. This representation is convenient for
 * referencing to substrings of a larger string without having to
 * duplicate them.
 */
struct sol_str_slice {
    size_t len;
    const char *data;
};

static inline bool
sol_str_slice_str_eq(const struct sol_str_slice a, const char *b)
{
    return b && a.len == strlen(b) && (memcmp(a.data, b, a.len) == 0);
}

static inline bool
sol_str_slice_eq(const struct sol_str_slice a, const struct sol_str_slice b)
{
    return a.len == b.len && (memcmp(a.data, b.data, a.len) == 0);
}

static inline void
sol_str_slice_copy(char *dst, const struct sol_str_slice src)
{
    memcpy(dst, src.data, src.len);
    dst[src.len] = 0;
}

static SOL_ATTR_NONNULL(1) inline struct sol_str_slice
sol_str_slice_from_str(const char *s)
{
    return SOL_STR_SLICE_STR(s, strlen(s));
}

static SOL_ATTR_NONNULL(1) inline struct sol_str_slice
sol_str_slice_from_blob(const struct sol_blob *blob)
{
    return SOL_STR_SLICE_STR(blob->mem, blob->size);
}

int sol_str_slice_to_int(const struct sol_str_slice s, int *value);

struct sol_str_slice sol_str_slice_trim(struct sol_str_slice s);

static inline char *
sol_str_slice_to_string(const struct sol_str_slice slice)
{
    return strndup(slice.data, slice.len);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
