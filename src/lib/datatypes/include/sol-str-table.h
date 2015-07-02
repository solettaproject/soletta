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

struct sol_str_table {
    const char *key;
    uint16_t len;
    int16_t val;
};

#define SOL_STR_TABLE_ITEM(_key, _val) \
    { SOL_STR_STATIC_ASSERT_LITERAL(_key), sizeof(_key) - 1, _val }

int16_t sol_str_table_lookup_fallback(const struct sol_str_table *table,
    const struct sol_str_slice key,
    int16_t fallback) SOL_ATTR_NONNULL(1);

#define SOL_STR_TABLE_NOT_FOUND INT16_MAX
#define sol_str_table_lookup(_table, _key, _pval) ({ \
    int16_t _v = sol_str_table_lookup_fallback(_table, _key, INT16_MAX); \
    if (_v != INT16_MAX) \
        *_pval = _v; \
    _v != INT16_MAX; \
})


struct sol_str_table_ptr {
    const char *key;
    const void *val;
    size_t len;
};

#define SOL_STR_TABLE_PTR_ITEM(_key, _val) \
    { .key = SOL_STR_STATIC_ASSERT_LITERAL(_key), \
      .len = sizeof(_key) - 1, \
      .val = _val }

const void *sol_str_table_ptr_lookup_fallback(const struct sol_str_table_ptr *table_ptr,
    const struct sol_str_slice key,
    const void *fallback) SOL_ATTR_NONNULL(1);

#define sol_str_table_ptr_lookup(_table_ptr, _key, _pval) ({ \
    const void *_v = sol_str_table_ptr_lookup_fallback(_table_ptr, \
        _key, NULL); \
    if (_v != NULL) \
        *_pval = _v; \
    _v != NULL; \
})

#ifdef __cplusplus
}
#endif
