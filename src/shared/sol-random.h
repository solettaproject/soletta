/*
 * This file is part of the Soletta Project
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

static inline bool
sol_random_get_int32(struct sol_random *engine, int32_t *value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(value, sizeof(*value),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    ssize_t r = sol_random_fill_buffer(engine, &buf, sizeof(*value));

    sol_buffer_fini(&buf);

    return r == (ssize_t)sizeof(*value);
}

static inline bool
sol_random_get_int64(struct sol_random *engine, int64_t *value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(value, sizeof(*value),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    ssize_t r = sol_random_fill_buffer(engine, &buf, sizeof(*value));

    sol_buffer_fini(&buf);

    return r == (ssize_t)sizeof(*value);
}

static inline bool
sol_random_get_double(struct sol_random *engine, double *value)
{
    int32_t num, den;

    if (!sol_random_get_int32(engine, &num))
        return false;
    if (!sol_random_get_int32(engine, &den))
        return false;

    *value = num * ((double)(INT32_MAX - 1) / INT32_MAX) +
        (double)den / INT32_MAX;

    return true;
}

static inline bool
sol_random_get_bool(struct sol_random *engine, bool *value)
{
    int32_t i;

    if (sol_random_get_int32(engine, &i)) {
        *value = i & 1;
        return true;
    }

    return false;
}

static inline uint8_t
sol_random_get_byte(struct sol_random *engine, uint8_t *value)
{
    int32_t i;

    if (sol_random_get_int32(engine, &i)) {
        *value = i & 0xff;
        return true;
    }

    return false;
}
