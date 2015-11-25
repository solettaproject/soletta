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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sol-log.h"
#include "sol-str-slice.h"
#include "sol-util.h"

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

SOL_API bool
sol_str_slice_contains(const struct sol_str_slice haystack, const struct sol_str_slice needle)
{
    return memmem(haystack.data, haystack.len, needle.data, needle.len) != NULL;
}

SOL_API bool
sol_str_slice_str_contains(const struct sol_str_slice haystack, const char *needle)
{
    return memmem(haystack.data, haystack.len, needle, strlen(needle)) != NULL;
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
