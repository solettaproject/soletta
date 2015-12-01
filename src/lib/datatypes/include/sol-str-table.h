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
 * @brief String table is a data type to store <string, 16-bit integer> pairs.
 * To store more complex data as value, please see @ref Str_Table_Ptr.
 *
 * @see Str_Table_Ptr
 *
 * @{
 */

/**
 * @struct sol_str_table
 *
 * @brief String table element type.
 */
struct sol_str_table {
    const char *key; /**< @brief Key string */
    uint16_t len; /**< @brief Key string length */
    int16_t val; /**< @brief Value (16 bits integer) */
};

/**
 * @def SOL_STR_TABLE_ITEM(_key, _val)
 *
 * @brief Helper macro to make easier to declare a <key, value> pair.
 *
 * @param _key Pair's key (string)
 * @param _val Pair's value (integer)
 */
#define SOL_STR_TABLE_ITEM(_key, _val) \
    { SOL_STR_STATIC_ASSERT_LITERAL(_key), sizeof(_key) - 1, _val }

/**
 * @brief Retrieves the value associated with a given key from the string table.
 *
 * Searches the string table for @c key and return its value, if @c key isn't found,
 * the value of @c fallback is returned.
 *
 * @param table String table
 * @param key Key to search
 * @param fallback Fallback value
 *
 * @return If @c key is found, return it's value, otherwise @c fallback is returned.
 */
int16_t sol_str_table_lookup_fallback(const struct sol_str_table *table,
    const struct sol_str_slice key,
    int16_t fallback) SOL_ATTR_NONNULL(1);

/**
 * @brief flag to detect key 'misses' in @ref sol_str_table_lookup.
 */
#define SOL_STR_TABLE_NOT_FOUND INT16_MAX

/**
 * @def sol_str_table_lookup(_table, _key, _pval)
 *
 * @brief Similar to @ref sol_str_table_lookup_fallback, but returning true/false.
 *
 * Returns true/false depending if key is found/not found and write the found value in @c _pval.
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
 * @}
 */

/**
 * @defgroup Str_Table_Ptr String/Pointer table
 * @ingroup Datatypes
 *
 * @brief String table is a data type to store <string, pointer> pairs. To store simple integers,
 * please check @ref Str_Table.
 *
 * @see Str_Table
 *
 * @{
 */

/**
 * @struct sol_str_table_ptr
 *
 * @brief String/Pointer table type
 */
struct sol_str_table_ptr {
    const char *key; /**< @brief Key string */
    const void *val; /**< @brief Value (pointer) */
    size_t len; /**< @brief Key string length */
};

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
      .len = sizeof(_key) - 1, \
      .val = _val }

/**
 * @brief Retrieves the value associated with a given key from the string/pointer table.
 *
 * Searches the table table for @c key string and return its pointer, if @c key isn't found,
 * the pointer @c fallback is returned.
 *
 * @param table_ptr String/pointer table
 * @param key Key to search
 * @param fallback Fallback pointer
 *
 * @return If @c key is found, return it's value, otherwise @c fallback is returned.
 */
const void *sol_str_table_ptr_lookup_fallback(const struct sol_str_table_ptr *table_ptr,
    const struct sol_str_slice key,
    const void *fallback) SOL_ATTR_NONNULL(1);

/**
 * @def sol_str_table_ptr_lookup(_table_ptr, _key, _pval)
 *
 * @brief Similar to @ref sol_str_table_ptr_lookup_fallback, but returning true/false.
 *
 * Returns true/false depending if key is found/not found and write the found value in @c _pval.
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
 * @}
 */

#ifdef __cplusplus
}
#endif
