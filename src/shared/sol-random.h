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
