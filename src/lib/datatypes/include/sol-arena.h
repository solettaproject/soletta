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

#include <sol-str-slice.h>
#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its arena implementation.
 */

/**
 * @defgroup Arena Arena
 * @ingroup Datatypes
 *
 * An arena is an object that does allocation on user behalf and can
 * deallocate all at once.
 *
 * @{
 */

/**
 * @struct sol_arena
 *
 * An arena is an object that does allocation on user behalf and can
 * deallocate all at once.
 *
 * See also sol-buffer.h if you just need a single resizable buffer.
 */
struct sol_arena;

struct sol_arena *sol_arena_new(void);
void sol_arena_del(struct sol_arena *arena);
void sol_arena_clear(struct sol_arena *arena);

int sol_arena_slice_dup_str(struct sol_arena *arena, struct sol_str_slice *dst, const char *str);
int sol_arena_slice_dup_str_n(struct sol_arena *arena, struct sol_str_slice *dst, const char *str, size_t n);
int sol_arena_slice_dup(struct sol_arena *arena, struct sol_str_slice *dst, struct sol_str_slice slice);
int sol_arena_slice_sprintf(struct sol_arena *arena, struct sol_str_slice *dst, const char *fmt, ...);

char *sol_arena_strdup(struct sol_arena *arena, const char *str);
char *sol_arena_strndup(struct sol_arena *arena, const char *str, size_t n);
char *sol_arena_strdup_slice(struct sol_arena *arena, const struct sol_str_slice slice);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
