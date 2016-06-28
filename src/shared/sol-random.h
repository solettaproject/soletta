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

#include <limits.h>
#include <sol-buffer.h>
#include <stdint.h>

struct sol_random;
struct sol_random_impl;

extern const struct sol_random_impl *SOL_RANDOM_MT19937;
extern const struct sol_random_impl *SOL_RANDOM_URANDOM;
extern const struct sol_random_impl *SOL_RANDOM_RANDOMR;
extern const struct sol_random_impl *SOL_RANDOM_DEFAULT;

struct sol_random *sol_random_new(const struct sol_random_impl *impl,
    uint64_t seed);
void sol_random_del(struct sol_random *random);

ssize_t sol_random_fill_buffer(struct sol_random *random,
    struct sol_buffer *buffer, size_t len);

static inline int
sol_random_get_int32(struct sol_random *engine, int32_t *value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(value, sizeof(*value),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    ssize_t r = sol_random_fill_buffer(engine, &buf, sizeof(*value));

    sol_buffer_fini(&buf);

    return r == (ssize_t)sizeof(*value) ? 0 : (int)r;
}

static inline int
sol_random_get_int64(struct sol_random *engine, int64_t *value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(value, sizeof(*value),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    ssize_t r = sol_random_fill_buffer(engine, &buf, sizeof(*value));

    sol_buffer_fini(&buf);

    return r == (ssize_t)sizeof(*value) ? 0 : (int)r;
}

static inline int
sol_random_get_double(struct sol_random *engine, double *value)
{
    int32_t num, den;
    int r;

    r = sol_random_get_int32(engine, &num);
    if (r < 0)
        return r;
    r = sol_random_get_int32(engine, &den);
    if (r < 0)
        return r;

    *value = num * ((double)(INT32_MAX - 1) / INT32_MAX) +
        (double)den / INT32_MAX;

    return r;
}

static inline int
sol_random_get_bool(struct sol_random *engine, bool *value)
{
    int32_t i;
    int r;

    r = sol_random_get_int32(engine, &i);
    if (r >= 0)
        *value = i & 1;

    return r;
}

static inline int
sol_random_get_byte(struct sol_random *engine, uint8_t *value)
{
    int32_t i;
    int r;

    r = sol_random_get_int32(engine, &i);
    if (r >= 0)
        *value = i & 0xff;

    return r;
}
