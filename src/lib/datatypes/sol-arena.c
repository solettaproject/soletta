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

#include <errno.h>
#include <stdio.h>

#include "sol-log.h"
#include "sol-arena.h"
#include "sol-util-internal.h"

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
sol_arena_slice_dup_str_n(struct sol_arena *arena, struct sol_str_slice *dst, const char *src, size_t n)
{
    struct sol_str_slice slice;
    int r;

    SOL_NULL_CHECK(arena, -EINVAL);
    SOL_NULL_CHECK(src, -EINVAL);
    SOL_INT_CHECK(n, <= 0, -EINVAL);

    slice.data = strndup(src, n);
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
sol_arena_slice_dup_str(struct sol_arena *arena, struct sol_str_slice *dst, const char *src)
{
    SOL_NULL_CHECK(src, -EINVAL);
    return sol_arena_slice_dup_str_n(arena, dst, src, strlen(src));
}

SOL_API int
sol_arena_slice_dup(struct sol_arena *arena, struct sol_str_slice *dst, struct sol_str_slice src)
{
    return sol_arena_slice_dup_str_n(arena, dst, src.data, src.len);
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
    return sol_arena_str_dup_n(arena, str, strlen(str));
}

SOL_API char *
sol_arena_str_dup_n(struct sol_arena *arena, const char *str, size_t n)
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
    return sol_arena_str_dup_n(arena, slice.data, slice.len);
}
