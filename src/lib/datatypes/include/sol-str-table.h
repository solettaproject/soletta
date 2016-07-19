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

#include <inttypes.h>

#include <sol-str-slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its string table implementation.
 */

/**
 * @defgroup Str_Table String table
 * @ingroup Datatypes
 *
 * @brief String table is a data type to store integers or
 * pointers to more complex data in pairs. It could be a table of
 * <string, 16-bit integer>, <string, int64_t> or <string, pointer> pairs.
 *
 * @{
 */

/**
 * @brief String table element type.
 *
 * For larger integers see struct sol_str_table_int64, for pointers see
 * struct sol_str_table_ptr.
 */
typedef struct sol_str_table {
    const char *key; /**< @brief Key string */
    uint16_t len; /**< @brief Key string length */
    int16_t val; /**< @brief Value (16 bits integer) */
} sol_str_table;

/**
 * @def SOL_STR_TABLE_ITEM(_key, _val)
 *
 * @brief Helper macro to make easier to declare a <key, int16_t> pair.
 *
 * For larger integers see struct sol_str_table_int64, for pointers see
 * struct sol_str_table_ptr.
 *
 * @param _key Pair's key (string)
 * @param _val Pair's value (integer)
 */
#define SOL_STR_TABLE_ITEM(_key, _val) \
    { .key = SOL_STR_STATIC_ASSERT_LITERAL(_key), .len = sizeof(_key) - 1, .val = _val }

/**
 * @brief Retrieves the table entry associated with a given key from
 * the string/integer table.
 *
 * Searches @a table for @c key string and returns its table entry
 * pointer. If @c key isn't found (or a bad @a table argument is
 * passed), @c NULL is returned and @c errno is set (either to @c
 * EINVAL or to @c ENOENT).
 *
 * @param table String/integer table
 * @param key Key to search
 *
 * @return If @c key is found, it returns its table entry, otherwise
 *         @c NULL is returned and errno is set to @c ENOENT if the
 *         item was not found or to @c EINVAL if parameters were
 *         invalid.
 */
const struct sol_str_table *sol_str_table_entry_lookup(const struct sol_str_table *table, const struct sol_str_slice key);

/**
 * @brief Retrieves the value associated with a given key from the string table.
 *
 * Searches the string table for @c key and returns its value. If @c
 * key isn't found, @c fallback is returned.
 *
 * @param table String table
 * @param key Key to search
 * @param fallback Fallback value
 *
 * @return If @c key is found, return its value, otherwise @c
 *         fallback is returned and errno is set to @c ENOENT if item
 *         is not found of @c EINVAL if parameters were invalid.
 */
int16_t sol_str_table_lookup_fallback(const struct sol_str_table *table,
    const struct sol_str_slice key,
    int16_t fallback);

/**
 * @brief flag to detect key 'misses' in @ref sol_str_table_lookup.
 */
#define SOL_STR_TABLE_NOT_FOUND INT16_MAX

/**
 * @def sol_str_table_lookup(_table, _key, _pval)
 *
 * @brief Similar to @ref sol_str_table_lookup_fallback, but returning true/false.
 *
 * Returns true/false depending if key is found/not found and write
 * the found value in @c _pval. If not found, errno is set to @c
 * ENOENT. If parameters were invalid, errno is set to @c EINVAL.
 *
 * @note Uses @c SOL_STR_TABLE_NOT_FOUND as a flag to detect when key isn't found,
 * so this value cannot be used in the string table for use with this macro.
 *
 * @param _table The string table
 * @param _key Key to search
 * @param _pval Pointer that will hold the found value
 *
 * @see sol_str_table_lookup_fallback
 */
#define sol_str_table_lookup(_table, _key, _pval) ({ \
        int16_t _v = sol_str_table_lookup_fallback(_table, _key, INT16_MAX); \
        if (_v != INT16_MAX) \
            *_pval = _v; \
        _v != INT16_MAX; \
    })

/**
 * @brief String/Pointer table type
 */
typedef struct sol_str_table_ptr {
    const char *key; /**< @brief Key string */
    const void *val; /**< @brief Value (pointer) */
    size_t len; /**< @brief Key string length */
} sol_str_table_ptr;

/**
 * @def SOL_STR_TABLE_PTR_ITEM(_key, _val)
 *
 * @brief Helper macro to make easier to declare a <key, value> pair.
 *
 * @param _key Pair's key (string)
 * @param _val Pair's value (pointer)
 */
#define SOL_STR_TABLE_PTR_ITEM(_key, _val) \
    { .key = SOL_STR_STATIC_ASSERT_LITERAL(_key), \
      .val = _val, \
      .len = sizeof(_key) - 1 }

/**
 * @brief Retrieves the value associated with a given key from the string/pointer table.
 *
 * Searches @a table for @c key string and returns its pointer. If @c
 * key isn't found, @c fallback is returned.
 *
 * @param table_ptr String/pointer table
 * @param key Key to search
 * @param fallback Fallback pointer
 *
 * @return If @c key is found, return its value, otherwise @c
 *         fallback is returned and errno is set to @c
 *         ENOENT if item is not found of @c EINVAL if parameters were
 *         invalid.
 */
const void *sol_str_table_ptr_lookup_fallback(const struct sol_str_table_ptr *table_ptr,
    const struct sol_str_slice key,
    const void *fallback)
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1)
#endif
    ;

/**
 * @brief Retrieves the table entry associated with a given key from
 * the string/pointer table.
 *
 * Searches @a table for @c key string and returns its table entry
 * pointer. If @c key isn't found (or a bad @a table argument is
 * passed), @c NULL is returned and @c errno is set (either to @c
 * EINVAL or to @c ENOENT).
 *
 * @param table String/pointer table
 * @param key Key to search
 *
 * @return If @c key is found, it returns its table entry, otherwise
 *         @c NULL is returned and errno is set to @c ENOENT if the
 *         item was not found or to @c EINVAL if parameters were
 *         invalid.
 */
const struct sol_str_table_ptr *sol_str_table_ptr_entry_lookup(const struct sol_str_table_ptr *table, const struct sol_str_slice key);

/**
 * @def sol_str_table_ptr_lookup(_table_ptr, _key, _pval)
 *
 * @brief Similar to @ref sol_str_table_ptr_lookup_fallback, but returning true/false.
 *
 * Returns true/false depending if key is found/not found and write
 * the found value in @c _pval. If not found, errno is set to @c
 * ENOENT. If parameters were invalid, errno is set to @c EINVAL.
 *
 * @param _table_ptr The string/pointer table
 * @param _key Key to search
 * @param _pval Pointer to pointer that will hold the found value
 *
 * @see sol_str_table_lookup_fallback
 */
#define sol_str_table_ptr_lookup(_table_ptr, _key, _pval) ({ \
        const void *_v = sol_str_table_ptr_lookup_fallback(_table_ptr, \
            _key, NULL); \
        if (_v != NULL) \
            *_pval = _v; \
        _v != NULL; \
    })

/**
 * @brief String/int64_t table type
 */
typedef struct sol_str_table_int64 {
    const char *key; /**< @brief Key string */
    size_t len; /**< @brief Key string length */
    int64_t val; /**< @brief Value (int64_t) */
} sol_str_table_int64;

/**
 * @def SOL_STR_TABLE_INT64_ITEM(_key, _val)
 *
 * @brief Helper macro to make easier to declare a <key, value> pair.
 *
 * @param _key Pair's key (string)
 * @param _val Pair's value (int64_t)
 */
#define SOL_STR_TABLE_INT64_ITEM(_key, _val) \
    { .key = SOL_STR_STATIC_ASSERT_LITERAL(_key), \
      .len = sizeof(_key) - 1, \
      .val = _val }

/**
 * @brief Retrieves the table entry associated with a given key from
 * the string/pointer table.
 *
 * Searches @a table for @c key string and returns its table entry
 * pointer. If @c key isn't found (or a bad @a table argument is
 * passed), @c NULL is returned and @c errno is set (either to @c
 * EINVAL or to @c ENOENT).
 *
 * @param table String/pointer table
 * @param key Key to search
 *
 * @return If @c key is found, it returns its table entry, otherwise
 *         @c NULL is returned and errno is set to @c ENOENT if the
 *         item was not found or to @c EINVAL if parameters were
 *         invalid.
 */
const struct sol_str_table_int64 *sol_str_table_int64_entry_lookup(const struct sol_str_table_int64 *table, const struct sol_str_slice key);

/**
 * @brief Retrieves the value associated with a given key from the string/int64_t table.
 *
 * Searches @a table for @c key string and returns its int64_t value.
 * If @c key isn't found, @c fallback is returned.
 *
 * @param table_int64 String/int64_t table
 * @param key Key to search
 * @param fallback Fallback int64_t
 *
 * @return If @c key is found, return its value, otherwise @c
 *         fallback is returned and errno is set to @c ENOENT if item
 *         is not found of @c EINVAL if parameters were invalid.
 */
int64_t sol_str_table_int64_lookup_fallback(const struct sol_str_table_int64 *table_int64,
    const struct sol_str_slice key,
    int64_t fallback)
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1)
#endif
    ;

/**
 * @brief flag to detect key 'misses' in @ref sol_str_table_int64_lookup.
 */
#define SOL_STR_TABLE_INT64_NOT_FOUND INT64_MAX

/**
 * @def sol_str_table_int64_lookup(_table_int64, _key, _pval)
 *
 * @brief Similar to @ref sol_str_table_int64_lookup_fallback, but returning true/false.
 *
 * Returns true/false depending if key is found/not found and write
 * the found value in @c _pval. If not found, errno is set to
 * @c ENOENT. If parameters where invalid, errno is set to @c EINVAL.
 *
 * @param _table_int64 The string/int64_t table
 * @param _key Key to search
 * @param _pval int64_t to int64_t that will hold the found value
 *
 * @see sol_str_table_lookup_fallback
 */
#define sol_str_table_int64_lookup(_table_int64, _key, _pval) ({ \
        int64_t _v = sol_str_table_int64_lookup_fallback(_table_int64, \
            _key, SOL_STR_TABLE_INT64_NOT_FOUND); \
        if (_v != SOL_STR_TABLE_INT64_NOT_FOUND) \
            *_pval = _v; \
        _v != SOL_STR_TABLE_INT64_NOT_FOUND; \
    })

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
