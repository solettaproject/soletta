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

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <sol-macros.h>
#include <sol-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its string slice implementation.
 */

/**
 * @defgroup Str_Slice String slice
 * @ingroup Datatypes
 *
 * @brief Slice of a string with explicit length.
 *
 * It doesn't necessarily ends with NULL byte like C strings. This
 * representation is convenient for referencing substrings of a larger
 * string without having to duplicate them.
 *
 * So be careful with memory management when using slices.
 *
 * @{
 */

/**
 * @brief Helper macro to assert that the parameter is a string literal.
 */
#define SOL_STR_STATIC_ASSERT_LITERAL(_s) ("" _s)

/**
 * @brief Helper macro to make easier to declare a string slice from a string literal.
 */
#define SOL_STR_SLICE_LITERAL(_s) { (sizeof(SOL_STR_STATIC_ASSERT_LITERAL(_s)) - 1), (_s) }

/**
 * @brief Helper macro to make easier to declare a string slice from a string.
 */
#define SOL_STR_SLICE_STR(_s, _len) (struct sol_str_slice){.len = (_len), .data = (_s) }

/**
 * @brief Helper macro to make easier to declare an empty string slice.
 */
#define SOL_STR_SLICE_EMPTY { .len = 0, .data = "" }

/**
 * @brief Helper macro to be used together with "%.*s"
 * formatting in 'printf()' family of functions.
 */
#define SOL_STR_SLICE_PRINT(_s) (int)(_s).len, (_s).data

/**
 * @brief String slice type
 *
 */
typedef struct sol_str_slice {
    size_t len; /**< @brief Slice length */
    const char *data; /**< @brief Slice data */
} sol_str_slice;

/**
 * @brief Checks if the content of the slice is equal to the string.
 *
 * @param a The string slice
 * @param b The string
 *
 * @return @c true if the contents are equal, @c false otherwise
 *
 * @see sol_str_slice_str_case_eq
 */
static inline bool
sol_str_slice_str_eq(const struct sol_str_slice a, const char *b)
{
    return b && a.len == strlen(b) && (memcmp(a.data, b, a.len) == 0);
}

/**
 * @brief Checks if the content of both slices are equal.
 *
 * @param a First slice
 * @param b Second slice
 *
 * @return @c true if the contents are equal, @c false otherwise
 *
 * @see sol_str_slice_case_eq
 */
static inline bool
sol_str_slice_eq(const struct sol_str_slice a, const struct sol_str_slice b)
{
    return a.len == b.len && (memcmp(a.data, b.data, a.len) == 0);
}

/**
 * @brief Checks if the content of the slice is equal to the string.
 *
 * Similar to @ref sol_str_slice_str_eq, but ignoring the case of the characters.
 *
 * @param a The string slice
 * @param b The string
 *
 * @return @c true if the contents are equal, @c false otherwise
 *
 * @see sol_str_slice_str_eq
 */
static inline bool
sol_str_slice_str_case_eq(const struct sol_str_slice a, const char *b)
{
    return b && a.len == strlen(b) && (strncasecmp(a.data, b, a.len) == 0);
}

/**
 * @brief Checks if the content of both slices are equal.
 *
 * Similar to @ref sol_str_slice_case_eq, but ignoring the case of the characters.
 *
 * @param a First slice
 * @param b Second slice
 *
 * @return @c true if the contents are equal, @c false otherwise
 *
 * @see sol_str_slice_eq
 */
static inline bool
sol_str_slice_case_eq(const struct sol_str_slice a, const struct sol_str_slice b)
{
    return a.len == b.len && (strncasecmp(a.data, b.data, a.len) == 0);
}

/**
 * @brief Checks if @c haystack contains @c needle.
 *
 * @param haystack Slice that will be searched
 * @param needle Slice to search for in @c haystack
 *
 * @return A pointer to the beginning of substring or @c NULL if not
 */
char *sol_str_slice_contains(const struct sol_str_slice haystack, const struct sol_str_slice needle);

/**
 * @brief Checks if @c haystack contains @c needle.
 *
 * @param haystack Slice that will be searched
 * @param needle String to search for in @c haystack
 *
 * @return A pointer to the beginning of substring or @c NULL if not
 */
static inline char *
sol_str_slice_str_contains(const struct sol_str_slice haystack, const char *needle)
{
    return sol_str_slice_contains(haystack, SOL_STR_SLICE_STR(needle, strlen(needle)));
}

/**
 * @brief Copies the content of slice @c src into string @c dst.
 *
 * @note @c dst must be large enough to receive the copy.
 *
 * @param src Source slice
 * @param dst Destination string
 */
static inline void
sol_str_slice_copy(char *dst, const struct sol_str_slice src)
{
    memcpy(dst, src.data, src.len);
    dst[src.len] = 0;
}

/**
 * @brief Checks if @c slice begins with @c prefix.
 *
 * @param slice String slice
 * @param prefix Prefix to look for
 *
 * @return @c true if @c slice begins with @c prefix, @c false otherwise
 */
static inline bool
sol_str_slice_starts_with(const struct sol_str_slice slice, const struct sol_str_slice prefix)
{
    return slice.len >= prefix.len && strncmp(slice.data, prefix.data, prefix.len) == 0;
}

/**
 * @brief Checks if @c slice begins with @c prefix.
 *
 * @param slice String slice
 * @param prefix Prefix to look for
 *
 * @return @c true if @c slice begins with @c prefix, @c false otherwise
 */
static inline bool
sol_str_slice_str_starts_with(const struct sol_str_slice slice, const char *prefix)
{
    size_t len = strlen(prefix);

    return slice.len >= len && strncmp(slice.data, prefix, len) == 0;
}

/**
 * @brief Populates a slice from a string.
 *
 * @param s Source string
 *
 * @return Resulting slice
 */
static
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1)
#endif
inline struct sol_str_slice
sol_str_slice_from_str(const char *s)
{
    return SOL_STR_SLICE_STR(s, strlen(s));
}

/**
 * @brief Populates a slice from a @ref sol_blob.
 *
 * @param blob Source blob
 *
 * @return Resulting slice
 */
static
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1)
#endif
inline struct sol_str_slice
sol_str_slice_from_blob(const struct sol_blob *blob)
{
    return SOL_STR_SLICE_STR((char *)blob->mem, blob->size);
}

/**
 * @brief Creates a blob from a slice.
 *
 * This function creates @ref SOL_BLOB_TYPE_DEFAULT, this means that
 * the memory used by the slice will be duplicated.
 *
 * @param slice Source slice
 * @return A blob or @c NULL on error
 */
static inline struct sol_blob *
sol_str_slice_to_blob(const struct sol_str_slice slice)
{
    void *blob_mem;
    struct sol_blob *blob;

    blob_mem = malloc(slice.len);
    if (!blob_mem)
        return NULL;

    memcpy(blob_mem, slice.data, slice.len);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL,
        blob_mem, slice.len);

    if (!blob) {
        free(blob_mem);
        return NULL;
    }

    return blob;
}

/**
 * @brief Converts a string slice to an integer.
 *
 * @param s String slice
 * @param value Where to store the integer value
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_str_slice_to_int(const struct sol_str_slice s, long int *value);

/**
 * @brief Creates a string from a string slice.
 *
 * @note Content is duplicated.
 *
 * @param slice Source slice
 *
 * @return New string created from the slice
 */
static inline char *
sol_str_slice_to_str(const struct sol_str_slice slice)
{
    return strndup(slice.data, slice.len);
}

/**
 * @brief Returns a slice based on @c slice but without leading white spaces.
 *
 * @param slice Source slice
 *
 * @return Slice without leading white spaces
 */
static inline struct sol_str_slice
sol_str_slice_remove_leading_whitespace(struct sol_str_slice slice)
{
    struct sol_str_slice copy = slice;

    while (copy.len && isspace((uint8_t)*copy.data)) {
        copy.data++;
        copy.len--;
    }

    return copy;
}

/**
 * @brief Returns a slice based on @c slice but without trailing white spaces.
 *
 * @param slice Source slice
 *
 * @return Slice without trailing white spaces
 */
static inline struct sol_str_slice
sol_str_slice_remove_trailing_whitespace(struct sol_str_slice slice)
{
    struct sol_str_slice copy = slice;

    while (copy.len != 0) {
        uint8_t c = copy.data[copy.len - 1];
        if (isspace(c))
            copy.len--;
        else
            break;
    }

    if (slice.len - copy.len)
        return copy;

    return slice;
}

/**
 * @brief Returns a slice based on @c slice but without either leading or trailing white spaces.
 *
 * @param slice Source slice
 *
 * @return Slice without either leading or trailing white spaces
 */
static inline struct sol_str_slice
sol_str_slice_trim(struct sol_str_slice slice)
{
    return sol_str_slice_remove_trailing_whitespace
               (sol_str_slice_remove_leading_whitespace(slice));
}

/**
 * @brief Return a list of the words in a given string slice,
 * using @c delim as delimiter string.
 *
 * If @a maxsplit is given, at most that number of splits are done
 * (thus, the list will have at most @c maxsplit+1 elements).
 * If @c maxsplit is zero, then there is no limit on the
 * number of splits (all possible splits are made).
 *
 * @param slice Source slice
 * @param delim Delimiter string
 * @param maxsplit The maximum number of splits to make
 *
 * @return On success, vector of string slices of the words, @c NULL otherwise.
 * @see sol_str_slice_split_iterate()
 * @see sol_str_slice_str_split_iterate()
 */
struct sol_vector sol_str_slice_split(const struct sol_str_slice slice, const char *delim, size_t maxsplit);

/**
 * @brief Do an one step split iteration over a slice.
 *
 * Usage example:
 * @code{.c}
 * struct sol_str_slice slice = SOL_STR_SLICE_LITERAL("one;two;three");
 * struct sol_str_slice token;
 * const char *itr = NULL;
 * while (sol_str_slice_split_iterate(slice, &token, &itr, ";")) {
 *    printf("%.*s\t", SOL_STR_SLICE_PRINT(token));
 *    //It will print: one two three
 * }
 * @endcode
 *
 * @param slice The slice the be splitted
 * @param token A splitted token
 * @param itr An iterator - It should be @c NULL on the first call.
 * @param delim The delimiter slice
 * @return @c true if iteration should continue or @c false if iteration should be stopped.
 * @see sol_str_slice_split()
 * @see sol_str_slice_str_split_iterate()
 */
bool sol_str_slice_split_iterate(const struct sol_str_slice slice, struct sol_str_slice *token, const char **itr, const struct sol_str_slice delim);

/**
 * @brief Wrapper over sol_str_slice_split_iterate()
 *
 * @param slice The slice the be splitted
 * @param token A splitted token
 * @param itr An iterator - It should be @c NULL on the first call.
 * @param delim The delimiter string
 * @see sol_str_slice_split_iterate()
 * @see sol_str_slice_split()
 */
static inline bool
sol_str_slice_str_split_iterate(const struct sol_str_slice slice, struct sol_str_slice *token, const char **itr, const char *delim)
{
    return sol_str_slice_split_iterate(slice, token, itr, sol_str_slice_from_str(delim));
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
