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

#include <sol-str-slice.h>
#include <sol-vector.h>
#include <sol-macros.h>

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
 * @brief An arena is an object that does allocation on user's behalf and can
 * deallocate all at once.
 *
 * @see Str_Slice
 *
 * @{
 */

/**
 * @typedef sol_arena
 *
 * @brief Sol Arena type.
 *
 * See also @ref sol_buffer if you just need a single re-sizable buffer.
 */
struct sol_arena;
typedef struct sol_arena sol_arena;

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
 * @param src String to copy and store
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_dup_str(struct sol_arena *arena, struct sol_str_slice *dst, const char *src);

/**
 * @brief Store a copy of at most @c n characters of a given string in the arena.
 *
 * Also, outputs a slice to the stored string in @c dst.
 *
 * @param arena The arena
 * @param dst String slice of the recently added string
 * @param src String to copy and store
 * @param n Maximum number of characters to copy
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_dup_str_n(struct sol_arena *arena, struct sol_str_slice *dst, const char *src, size_t n);

/**
 * @brief Store a copy of a given string slice in the arena.
 *
 * Also, outputs the recently stored string src in @c dst.
 *
 * @param arena The arena
 * @param dst String slice of the recently added string slice
 * @param src String slice to copy and store
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_arena_slice_dup(struct sol_arena *arena, struct sol_str_slice *dst, struct sol_str_slice src);

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
int sol_arena_slice_sprintf(struct sol_arena *arena, struct sol_str_slice *dst, const char *fmt, ...) SOL_ATTR_PRINTF(3, 4);

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
char *sol_arena_str_dup_n(struct sol_arena *arena, const char *str, size_t n);

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
