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

/**
 * @file
 * @brief These are common Soletta macros.
 */

/**
 * @defgroup Macros Macros
 *
 * @{
 */

/**
 * @def SOL_API
 *
 * @brief Used to export functions on the final binaries.
 */

/**
 * @def SOL_ATTR_WARN_UNUSED_RESULT
 *
 * @brief Causes a warning to be emitted if a caller of the function
 * with this attribute does not use its return value.
 */

/**
 * @def SOL_ATTR_CONST
 *
 * @brief Used to enforce that the function is not allowed to read global memory.
 */

/**
 * @def SOL_ATTR_PRINTF(fmt, arg)
 *
 * @brief Specifies that a function takes @c printf style arguments
 * which should be type-checked against a format string.
 *
 * @param fmt Index of the format string
 * @param arg Index of the format string arguments
 */

/**
 * @def SOL_ATTR_SCANF(fmt, arg)
 *
 * @brief Specifies that a function takes scanf style arguments
 * which should be type-checked against a format string.
 *
 * @param fmt Index of the format string
 * @param arg Index of the format string arguments
 */

/**
 * @def SOL_ATTR_NOINSTRUMENT
 *
 * @brief Used to tell that this functions shouldn't be instrumented.
 *
 * If @c -finstrument-functions is used, it won't be applied
 * in functions with this attribute.
 */

/**
 * @def SOL_ATTR_NONNULL(...)
 *
 * @brief Specifies that some function parameters should be non-null pointers.
 *
 * @param ... Indexes of the arguments to check for non-nullity
 */

/**
 * @def SOL_ATTR_SECTION(secname)
 *
 * @brief Used to tell that a function should be placed in the section @c secname.
 *
 * @param secname Section's name
 */

/**
 * @def SOL_ATTR_USED
 *
 * @brief Used to tell that code must be emitted for the function even if it appears
 * that the function is not referenced.
 */

/**
 * @def SOL_ATTR_UNUSED
 *
 * @brief Used to tell that the function is meant to be possibly unused.
 */

/**
 * @def SOL_ATTR_SENTINEL
 *
 * @brief Used to ensure that the last parameter in a function call is an explicit NULL.
 */

/**
 * @def SOL_ATTR_NORETURN
 *
 * @brief Used to tell that a function never return.
 */

/**
 * @def SOL_ATTR_PURE
 *
 * @brief Used to tell that this functions has no effects except the return value
 * and their return value depends only on the parameters and/or global variables.
 */

/**
 * @def SOL_STATIC_ARRAY_SIZE(n)
 *
 * @brief Convenience macro to declare the size of a static array that will handle
 * differences between C and C++.
 *
 * @param n Size of the array
 */

#if __GNUC__ >= 4
#define SOL_API  __attribute__((visibility("default")))
#define SOL_ATTR_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define SOL_ATTR_CONST __attribute__((__const__))
#define SOL_ATTR_PRINTF(fmt, arg) __attribute__((format(printf, fmt, arg)))
#define SOL_ATTR_SCANF(fmt, arg) __attribute__((format(scanf, fmt, arg)))
#define SOL_ATTR_NOINSTRUMENT __attribute__((no_instrument_function))
#define SOL_ATTR_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define SOL_ATTR_SECTION(secname) __attribute__((section(secname)))
#define SOL_ATTR_USED __attribute__((__used__))
#define SOL_ATTR_UNUSED __attribute__((__unused__))
#define SOL_ATTR_SENTINEL __attribute__((sentinel))
#define SOL_ATTR_NORETURN __attribute__((noreturn))
#define SOL_ATTR_PURE __attribute__((pure))
#else
#define SOL_API
#define SOL_ATTR_WARN_UNUSED_RESULT
#define SOL_ATTR_CONST
#define SOL_ATTR_PRINTF(fmt, arg)
#define SOL_ATTR_SCANF(fmt, arg)
#define SOL_ATTR_NOINSTRUMENT
#define SOL_ATTR_NONNULL(...)
#define SOL_ATTR_SECTION(secname)
#define SOL_ATTR_USED
#define SOL_ATTR_UNUSED
#define SOL_ATTR_SENTINEL
#define SOL_ATTR_NORETURN
#define SOL_ATTR_PURE
#endif

#ifdef __cplusplus
#define SOL_STATIC_ARRAY_SIZE(n) n
#else
#define SOL_STATIC_ARRAY_SIZE(n) static n
#endif

/**
 * @}
 */
