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

/**
 * @file
 * @brief These are common Soletta macros.
 */

/**
 * @defgroup Macros Compiler Macros
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
 * @def SOL_ATTR_STRFTIME(fmt)
 *
 * @brief Specifies that a function takes @c strftime style arguments
 * which should be type-checked against a format string.
 *
 * @param fmt Index of the format string
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
 * @def SOL_ATTR_NO_INSTRUMENT
 *
 * @brief Used to tell that this functions shouldn't be instrumented.
 *
 * If @c -finstrument-functions is used, it won't be applied
 * in functions with this attribute.
 */

/**
 * @def SOL_ATTR_NON_NULL(...)
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
 * @def SOL_ATTR_NO_RETURN
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
 * @def SOL_LIKELY
 *
 * @brief Convenience macro for @c likely branch annotation. Provide the compiler with
 * branch prediction information.
 */

/**
 * @def SOL_UNLIKELY
 *
 * @brief Convenience macro for @c unlikely branch annotation. Provide the compiler with
 * branch prediction information.
 */

/**
 * @def SOL_STATIC_ARRAY_SIZE(n)
 *
 * @brief Convenience macro to declare the size of a static array that will handle
 * differences between C and C++.
 *
 * @param n Size of the array
 */

/**
 * @def SOL_UNREACHABLE
 *
 * @brief Macro to mark a location of code that is unreachable, usually after
 * calling a SOL_ATTR_NO_RETURN function.
 */

#if __GNUC__ >= 4
#define SOL_API  __attribute__((visibility("default")))
#define SOL_ATTR_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define SOL_ATTR_CONST __attribute__((__const__))
#define SOL_ATTR_PRINTF(fmt, arg) __attribute__((format(printf, fmt, arg)))
#define SOL_ATTR_SCANF(fmt, arg) __attribute__((format(scanf, fmt, arg)))
#define SOL_ATTR_STRFTIME(fmt) __attribute__((format(strftime, fmt, 0)))
#define SOL_ATTR_NO_INSTRUMENT __attribute__((no_instrument_function))
#define SOL_ATTR_NON_NULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define SOL_ATTR_SECTION(secname) __attribute__((section(secname)))
#define SOL_ATTR_USED __attribute__((__used__))
#define SOL_ATTR_UNUSED __attribute__((__unused__))
#define SOL_ATTR_SENTINEL __attribute__((sentinel))
#define SOL_ATTR_NO_RETURN __attribute__((noreturn))
#define SOL_ATTR_PURE __attribute__((pure))
#define SOL_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SOL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SOL_UNREACHABLE() __builtin_unreachable()
#else
#define SOL_API
#define SOL_ATTR_WARN_UNUSED_RESULT
#define SOL_ATTR_CONST
#define SOL_ATTR_PRINTF(fmt, arg)
#define SOL_ATTR_SCANF(fmt, arg)
#define SOL_ATTR_STRFTIME(fmt)
#define SOL_ATTR_NO_INSTRUMENT
#define SOL_ATTR_NON_NULL(...)
#define SOL_ATTR_SECTION(secname)
#define SOL_ATTR_USED
#define SOL_ATTR_UNUSED
#define SOL_ATTR_SENTINEL
#define SOL_ATTR_NO_RETURN
#define SOL_ATTR_PURE
#define SOL_LIKELY(x)
#define SOL_UNLIKELY(x)
#define SOL_UNREACHABLE() ((void)0)
#endif

#ifdef __cplusplus
#define SOL_STATIC_ARRAY_SIZE(n) n
#else
#define SOL_STATIC_ARRAY_SIZE(n) static n
#endif

/**
 * @}
 */
