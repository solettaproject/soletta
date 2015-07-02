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

#include <errno.h>
#include <stdio.h>

#include "sol-log.h"
#include "sol-arena.h"
#include "sol-util.h"

/* TODO: check if it's worthwhile to implement this as a single
 * growing buffer. */

struct sol_arena {
    struct sol_ptr_vector str_vector;
};

SOL_API struct sol_arena *
sol_arena_new(void)
{
    struct sol_arena *arena;

    arena = calloc(1, sizeof(struct sol_arena));
    SOL_NULL_CHECK(arena, NULL);

    sol_ptr_vector_init(&arena->str_vector);
    return arena;
}

SOL_API void
sol_arena_del(struct sol_arena *arena)
{
    char *s;
    uint16_t i;

    SOL_NULL_CHECK(arena);

    SOL_PTR_VECTOR_FOREACH_IDX (&arena->str_vector, s, i)
        free(s);

    sol_ptr_vector_clear(&arena->str_vector);
    free(arena);
}

SOL_API int
sol_arena_slice_dup_str_n(struct sol_arena *arena, struct sol_str_slice *dst, const char *str, size_t n)
{
    struct sol_str_slice slice;
    int r;

    SOL_NULL_CHECK(arena, -EINVAL);
    SOL_NULL_CHECK(str, -EINVAL);
    SOL_INT_CHECK(n, <= 0, -EINVAL);

    slice.data = strndup(str, n);
    SOL_NULL_CHECK(slice.data, -errno);

    slice.len = n;

    r = sol_ptr_vector_append(&arena->str_vector, (char *)slice.data);
    if (r < 0) {
        free((char *)slice.data);
        return r;
    }

    *dst = slice;
    return 0;
}

SOL_API int
sol_arena_slice_dup_str(struct sol_arena *arena, struct sol_str_slice *dst, const char *str)
{
    SOL_NULL_CHECK(str, -EINVAL);
    return sol_arena_slice_dup_str_n(arena, dst, str, strlen(str));
}

SOL_API int
sol_arena_slice_dup(struct sol_arena *arena, struct sol_str_slice *dst, struct sol_str_slice slice)
{
    return sol_arena_slice_dup_str_n(arena, dst, slice.data, slice.len);
}

SOL_API int
sol_arena_slice_sprintf(struct sol_arena *arena, struct sol_str_slice *dst, const char *fmt, ...)
{
    va_list ap;
    char *str;
    int r;

    va_start(ap, fmt);
    r = vasprintf(&str, fmt, ap);
    va_end(ap);
    SOL_INT_CHECK(r, < 0, r);

    dst->data = str;
    dst->len = r;

    r = sol_ptr_vector_append(&arena->str_vector, str);
    if (r < 0) {
        free(str);
        return r;
    }

    return 0;
}

SOL_API char *
sol_arena_strdup(struct sol_arena *arena, const char *str)
{
    SOL_NULL_CHECK(str, NULL);
    return sol_arena_strndup(arena, str, strlen(str));
}

SOL_API char *
sol_arena_strndup(struct sol_arena *arena, const char *str, size_t n)
{
    char *result;
    int r;

    SOL_NULL_CHECK(arena, NULL);
    SOL_NULL_CHECK(str, NULL);
    SOL_INT_CHECK(n, <= 0, NULL);

    result = strndup(str, n);
    SOL_NULL_CHECK(result, NULL);

    r = sol_ptr_vector_append(&arena->str_vector, result);
    if (r < 0) {
        free(result);
        return NULL;
    }

    return result;
}

SOL_API char *
sol_arena_strdup_slice(struct sol_arena *arena, const struct sol_str_slice slice)
{
    return sol_arena_strndup(arena, slice.data, slice.len);
}
