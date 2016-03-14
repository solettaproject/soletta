/*
 * This file is part of the Soletta Project
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

#include <inttypes.h>

#include "sol-macros.h"
#include "sol-str-table.h"
#include "sol-util-internal.h"

SOL_API int16_t
sol_str_table_lookup_fallback(const struct sol_str_table *table,
    const struct sol_str_slice key,
    int16_t fallback)
{
    const struct sol_str_table *iter;
    uint16_t len;

    if (SOL_UNLIKELY(key.len > INT16_MAX))
        return fallback;

    len = (uint16_t)key.len;

    for (iter = table; iter->key; iter++) {
        if (iter->len == len && memcmp(iter->key, key.data, len) == 0) {
            return iter->val;
        }
    }
    return fallback;
}

SOL_API const void *
sol_str_table_ptr_lookup_fallback(const struct sol_str_table_ptr *table,
    const struct sol_str_slice key,
    const void *fallback)
{
    const struct sol_str_table_ptr *iter;
    size_t len;

    if (SOL_UNLIKELY(key.len > INT16_MAX))
        return fallback;

    len = key.len;

    for (iter = table; iter->key; iter++) {
        if (iter->len == len && memcmp(iter->key, key.data, len) == 0) {
            return iter->val;
        }
    }
    return fallback;
}
