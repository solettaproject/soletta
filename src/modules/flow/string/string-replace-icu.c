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

/* Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009,
 * 2010, 2011, 2012, 2013, 2014, 2015 Python Software Foundation; All
 * Rights Reserved. This file has code excerpts (string module)
 * extracted from cpython project (https://hg.python.org/cpython/),
 * that comes under the PSFL license. The string formatting code was
 * adapted here to Soletta data types. The entire text for that
 * license is present in this directory */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unicode/ustring.h>
#include <unicode/utypes.h>
#include <unicode/uchar.h>

#include "string.h"

#define FAST_COUNT 0
#define FAST_SEARCH 1

#define STRINGLIB_BLOOM_WIDTH sizeof(UChar)

#define STRINGLIB_BLOOM_ADD(mask, ch) \
    ((mask |= (1UL << ((ch) & (STRINGLIB_BLOOM_WIDTH - 1)))))
#define STRINGLIB_BLOOM(mask, ch)     \
    ((mask &  (1UL << ((ch) & (STRINGLIB_BLOOM_WIDTH - 1)))))

static inline ssize_t
fast_search(const UChar *str,
    size_t str_len,
    const UChar *pattern,
    size_t pattern_len,
    size_t max_count,
    int mode)
{
    const UChar *str_ptr;
    const UChar *pattern_ptr;
    unsigned long mask;
    size_t i, j, mlast;
    size_t skip, count = 0;
    ssize_t w;

    w = str_len - pattern_len;

    if (w < 0 || (mode == FAST_COUNT && max_count == 0))
        return -1;

    /* look for special cases */
    if (pattern_len <= 1) {
        if (pattern_len <= 0)
            return -1;

        /* use special case for 1-character strings */
        if (str_len > 10 && (mode == FAST_SEARCH)) {
            /* use memchr if we can choose a needle without too many
               likely false positives */

            UChar *ptr = u_memchr(str, pattern[0], str_len);
            if (!ptr)
                return -1;

            return ptr - str;
        }
        if (mode == FAST_COUNT) {
            for (i = 0; i < str_len; i++)
                if (str[i] == pattern[0]) {
                    count++;
                    if (count == max_count)
                        return max_count;
                }
            return count;
        } else { /* (mode == FAST_SEARCH) */
            for (i = 0; i < str_len; i++)
                if (str[i] == pattern[0])
                    return i;
        }

        return -1;
    }

    mlast = pattern_len - 1;
    skip = mlast - 1;
    mask = 0;

    str_ptr = str + pattern_len - 1;
    pattern_ptr = pattern + pattern_len - 1;

    /* create compressed boyer-moore delta 1 table */

    /* process pattern[:-1] */
    for (i = 0; i < mlast; i++) {
        STRINGLIB_BLOOM_ADD(mask, pattern[i]);
        if (pattern[i] == pattern[mlast])
            skip = mlast - i - 1;
    }
    /* process pattern[-1] outside the loop */
    STRINGLIB_BLOOM_ADD(mask, pattern[mlast]);

    for (i = 0; i <= (size_t)w; i++) {
        /* note: using mlast in the skip path slows things down on x86 */
        if (str_ptr[i] == pattern_ptr[0]) {
            /* candidate match */
            for (j = 0; j < mlast; j++)
                if (str[i + j] != pattern[j])
                    break;
            if (j == mlast) {
                /* got a match! */
                if (mode != FAST_COUNT)
                    return i;
                count++;
                if (count == max_count)
                    return max_count;
                i = i + mlast;
                continue;
            }
            /* miss: check if next character is part of pattern */
            if (!STRINGLIB_BLOOM(mask, str_ptr[i + 1]))
                i = i + pattern_len;
            else
                i = i + skip;
        } else {
            /* skip: check if next character is part of pattern */
            if (!STRINGLIB_BLOOM(mask, str_ptr[i + 1]))
                i = i + pattern_len;
        }
    }

    if (mode != FAST_COUNT)
        return -1;

    return count;
}

static inline ssize_t
sub_str_count(const UChar *str,
    size_t str_len,
    const UChar *sub,
    size_t sub_len,
    size_t max_count)
{
    ssize_t count;

    if (sub_len == 0)
        return (str_len < max_count) ? str_len + 1 : max_count;

    count = fast_search(str, str_len, sub, sub_len, max_count, FAST_COUNT);

    if (count < 0)
        return 0;  /* no match */

    return count;
}

static inline ssize_t
sub_str_find(const UChar *str,
    ssize_t str_len,
    const UChar *sub,
    ssize_t sub_len,
    ssize_t offset)
{
    ssize_t pos;

    if (sub_len == 0)
        return offset;

    pos = fast_search(str, str_len, sub, sub_len, -1, FAST_SEARCH);

    if (pos >= 0)
        pos += offset;

    return pos;
}

#define ICU_STR_FROM_UTF8_GOTO(_icu_str, _icu_sz, _icu_err_val, \
        _utf_str, _icu_size_calc_goto, _no_mem_goto, _icu_conv_goto) \
    do { \
        _icu_err_val = U_ZERO_ERROR; \
        u_strFromUTF8(NULL, 0, &_icu_sz, _utf_str, -1, &_icu_err_val); \
        if (U_FAILURE(_icu_err_val) && \
            _icu_err_val != U_BUFFER_OVERFLOW_ERROR) \
            goto _icu_size_calc_goto; \
        _icu_str = calloc(_icu_sz + 1, sizeof(*_icu_str)); \
        if (!_icu_str) \
            goto _no_mem_goto; \
        _icu_err_val = U_ZERO_ERROR; \
        u_strFromUTF8(_icu_str, _icu_sz + 1, &_icu_sz, _utf_str, \
            -1, &_icu_err_val); \
        if (U_FAILURE(_icu_err_val)) { \
            goto _icu_conv_goto; \
        } \
    } while (0)

static UChar *
u_strdup(UChar *in)
{
    uint32_t len = u_strlen(in) + 1;
    UChar *result = malloc(sizeof(UChar) * len);

    SOL_NULL_CHECK(result, NULL);
    u_memcpy(result, in, len);
    return result;
}

static inline void
replace_1_char_in_place(UChar *s, UChar *end,
    UChar u1, UChar u2, ssize_t max_count)
{
    *s = u2;
    while (--max_count && ++s != end) {
        /* Find the next character to be replaced. If it occurs often,
         * it is faster to scan for it using an inline loop. If it
         * occurs seldom, it is faster to scan for it using a function
         * call; the overhead of the function call is amortized across
         * the many characters that call covers. We start with an
         * inline loop and use a heuristic to determine whether to
         * fall back to a function call.
         */
        if (*s != u1) {
            int attempts = 10;

            while (true) {
                if (++s == end)
                    return;
                if (*s == u1)
                    break;
                if (!--attempts) {
                    s++;
                    s = u_memchr(s, u1, end - s);
                    if (s == NULL)
                        return;
                    /* restart the dummy loop */
                    break;
                }
            }
        }
        *s = u2;
    }
}

UChar *
string_replace(struct sol_flow_node *node,
    UChar *value,
    UChar *change_from,
    UChar *change_to,
    size_t max_count)
{
    UChar *ret;
    size_t value_len = u_strlen(value);
    size_t change_to_len = u_strlen(change_to);
    size_t change_from_len = u_strlen(change_from);

    if (max_count == 0) {
        ret = u_strdup(value);
        goto nothing;
    }

    if (u_strcmp(change_from, change_to) == 0) {
        ret = u_strdup(value);
        goto nothing;
    }

    if (change_from_len == change_to_len) {
        /* same length */
        if (change_from_len == 0) {
            ret = u_strdup(value);
            goto nothing;
        }
        if (change_from_len == 1) {
            /* replace characters */
            ret = u_strdup(value);
            replace_1_char_in_place(ret, ret + value_len, change_from[0],
                change_to[0], max_count);
        } else {
            UChar *token;
            ssize_t i;

            ret = u_strdup(value);
            token = u_strFindFirst(value, value_len, change_from,
                change_from_len);
            if (!token)
                goto nothing;

            i = token - value;

            u_memcpy(ret, value, value_len);
            /* change everything in-place, starting with this one */
            u_memcpy(ret +  i, change_to, change_to_len);
            i += change_from_len;

            while (--max_count > 0) {
                token = u_strFindFirst(value + i, value_len - i, change_from,
                    change_from_len);
                if (!token)
                    break;
                u_memcpy(ret +  i, change_to, change_to_len);
                i += change_from_len;
            }
        }
    } else {
        ssize_t count, i, j, ires, new_size;
        int r;

        count = sub_str_count(value, value_len,
            change_from, change_from_len, max_count);
        if (count == 0) {
            ret = u_strdup(value);
            goto nothing;
        }

        if (change_from_len < change_to_len &&
            change_to_len - change_from_len >
            (INT32_MAX - value_len) / count) {
            sol_flow_send_error_packet(node, -EINVAL,
                "replace string is too long");
            goto error;
        }
        r = sol_util_ssize_mul(count,
            (ssize_t)(change_to_len - change_from_len), &new_size);
        if (r < 0 ||
            (new_size > 0 && SSIZE_MAX - new_size < (ssize_t)value_len)) {
            sol_flow_send_error_packet(node, -EINVAL,
                "replace string is too long");
            goto error;
        }

        new_size += value_len;

        if (new_size == 0) {
            UErrorCode err;

            r = icu_str_from_utf8("", &ret, &err);
            SOL_INT_CHECK_GOTO(r, < 0, error);

            goto done;
        }

        ret = calloc(new_size + 1, sizeof(*ret));
        if (!ret)
            goto error;

        ires = i = 0;
        if (change_from_len > 0) {
            while (count-- > 0) {
                UChar *token;

                /* look for next match */
                token = u_strFindFirst(value + i, value_len - i, change_from,
                    change_from_len);
                if (!token) {
                    free(ret);
                    ret = u_strdup(value);
                    goto nothing;
                }

                j = sub_str_find(value +  i, value_len - i,
                    change_from, change_from_len, i);
                if (j == -1)
                    break;
                else if (j > i) {
                    /* copy unchanged part [i:j] */
                    u_memcpy(ret +  ires, value +  i, (j - i));
                    ires += j - i;
                }
                /* copy substitution string */
                if (change_to_len > 0) {
                    u_memcpy(ret +  ires, change_to, change_to_len);
                    ires += change_to_len;
                }
                i = j + change_from_len;
            }
            if (i < (ssize_t)value_len)
                /* copy tail [i:] */
                u_memcpy(ret +  ires, value +  i, (value_len - i));
        } else {
            /* interleave */
            while (count > 0) {
                u_memcpy(ret +  ires, change_to, change_to_len);
                ires += change_to_len;
                if (--count <= 0)
                    break;
                u_memcpy(ret +  ires, value +  i, 1);
                ires++;
                i++;
            }
            u_memcpy(ret +  ires, value +  i, (value_len - i));
        }
    }

done:
nothing:
    return ret;

error:
    return NULL;
}
