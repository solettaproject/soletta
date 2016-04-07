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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sol-log.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"

SOL_API int
sol_str_slice_to_int(const struct sol_str_slice s, int *value)
{
    char *endptr = NULL;
    long int v;

    SOL_INT_CHECK(s.len, == 0, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    v = sol_util_strtol(s.data, &endptr, s.len, 0);

    if (errno)
        return -errno;

    if (endptr != s.data + s.len)
        return -EINVAL;

    if ((long)(int)v != v)
        return -ERANGE;

    *value = v;
    return 0;
}

SOL_API char *
sol_str_slice_contains(const struct sol_str_slice haystack, const struct sol_str_slice needle)
{
    return memmem(haystack.data, haystack.len, needle.data, needle.len);
}

SOL_API char *
sol_str_slice_str_contains(const struct sol_str_slice haystack, const char *needle)
{
    SOL_NULL_CHECK(needle, NULL);

    return memmem(haystack.data, haystack.len, needle, strlen(needle));
}

SOL_API struct sol_vector
sol_str_slice_split(const struct sol_str_slice slice,
    const char *delim,
    size_t maxsplit)
{
    struct sol_vector v = SOL_VECTOR_INIT(struct sol_str_slice);
    const char *str = slice.data;
    ssize_t dlen;
    size_t len;

    if (!slice.len || !delim)
        return v;

    maxsplit = (maxsplit) ? : slice.len - 1;
    if (maxsplit == SIZE_MAX) //once we compare to maxsplit + 1
        maxsplit--;

    dlen = strlen(delim);
    len = slice.len;

#define CREATE_SLICE(_str, _len) \
    do { \
        s = sol_vector_append(&v); \
        if (!s) \
            goto err; \
        s->data = _str; \
        s->len = _len; \
    } while (0)

    while (str && (v.len < maxsplit + 1)) {
        struct sol_str_slice *s;
        char *token = memmem(str, len, delim, dlen);
        if (!token) {
            CREATE_SLICE(str, len);
            break;
        }

        if (v.len == (uint16_t)maxsplit)
            CREATE_SLICE(str, len);
        else
            CREATE_SLICE(str, (size_t)(token - str));

        len -= (token - str) + dlen;
        str = token + dlen;
    }
#undef CREATE_SLICE

    return v;

err:
    sol_vector_clear(&v);
    return v;
}
