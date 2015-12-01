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
 * @brief An arena is an object that does allocation on user behalf and can
 * deallocate all at once.
 *
 * @see Str_Slice
 *
 * @{
 */

/**
 * @struct sol_arena
 *
 * @brief Sol Arena type.
 *
 * See also @ref sol_buffer if you just need a single re-sizable buffer.
 */
struct sol_arena;

/**
 * @brief Creates an Arena.
 *
 * @return The new arena, @c NULL in case of error
 */
struct sol_arena *sol_arena_new(void);

/**
 * @brief Delete the Arena.
 *
 * Delete the arena and all it's contents. Frees all the memory previously
 * allocated by the arena.
 *
 * @param arena Arena to be deleted
 */
void sol_arena_del(struct sol_arena *arena);

/**
 * @brief Store a copy of a given string in the arena.
 *
 * Also, outputs a slice to the stored string in @c dst.
 *
 * @param arena The arena
 * @param dst String slice of the recently added string
 * @param str String to copy and store
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_dup_str(struct sol_arena *arena, struct sol_str_slice *dst, const char *str);

/**
 * @brief Store a copy of at most @c n characters of a given string in the arena.
 *
 * Also, outputs a slice to the stored string in @c dst.
 *
 * @param arena The arena
 * @param dst String slice of the recently added string
 * @param str String to copy and store
 * @param n Maximum number of characters to copy
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_dup_str_n(struct sol_arena *arena, struct sol_str_slice *dst, const char *str, size_t n);

/**
 * @brief Store a copy of a given string slice in the arena.
 *
 * Also, outputs the recently stored string slice in @c dst.
 *
 * @param arena The arena
 * @param dst String slice of the recently added string slice
 * @param slice String slice to copy and store
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_dup(struct sol_arena *arena, struct sol_str_slice *dst, struct sol_str_slice slice);

/**
 * @brief Store the output of 'sprintf()' in the arena.
 *
 * Also, outputs a slice to the stored string in @c dst.
 *
 * @param arena The arena
 * @param dst String slice of the recently added string
 * @param fmt A standard 'printf()' format string
 * @param ... The arguments for 'sprintf()'
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_sprintf(struct sol_arena *arena, struct sol_str_slice *dst, const char *fmt, ...);

/**
 * @brief Store a copy of a given string in the arena.
 *
 * @param arena The arena
 * @param str String to copy and store
 *
 * @return The stored string
 */
char *sol_arena_strdup(struct sol_arena *arena, const char *str);

/**
 * @brief Store a copy of at most @c n characters of a given string in the arena.
 *
 * @param arena The arena
 * @param str String to copy and store
 * @param n Maximum number of characters to copy
 *
 * @return The stored string
 */
char *sol_arena_strndup(struct sol_arena *arena, const char *str, size_t n);

/**
 * @brief Store a copy of a given string slice in the arena.
 *
 * @param arena The arena
 * @param slice String slice to copy and store
 *
 * @return The stored string
 */
char *sol_arena_strdup_slice(struct sol_arena *arena, const struct sol_str_slice slice);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
