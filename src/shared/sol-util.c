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

#include "sol-common-buildopts.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sol-util.h"
#include "sol-str-slice.h"

void *
sol_util_memdup(const void *data, size_t len)
{
    void *ptr;

    ptr = malloc(len);
    if (ptr)
        memcpy(ptr, data, len);
    return ptr;
}

#ifdef SOL_PLATFORM_CONTIKI
#include <contiki.h>

struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec ret;
    clock_time_t ticks;

    ticks = clock_time();
    ret.tv_sec = ticks / CLOCK_SECOND;
    ticks -= ret.tv_sec * CLOCK_SECOND;
    ret.tv_nsec = (ticks * NSEC_PER_SEC) / CLOCK_SECOND;
    return ret;
}
#else
struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}
#endif

char *
sol_util_strerror(int errnum, char *buf, size_t buflen)
{
    char *ret;

    if (buflen < 1)
        return NULL;

    buf[0] = '\0';

    ret = (char *)strerror_r(errnum, buf, buflen);
    /* if buf was used it means it can be XSI version (so ret won't be
       pointing to msg string), or GNU version using non static string
       (in this case ret == buf already) */
    if (buf[0] != '\0')
        ret = buf;

    return ret;
}

struct sol_vector
sol_util_str_split(const struct sol_str_slice slice, const char *delim, size_t maxsplit)
{
    struct sol_vector v = SOL_VECTOR_INIT(struct sol_str_slice);
    ssize_t dlen, len;
    const char *str = slice.data;

    if (!slice.len || !delim)
        return v;

    maxsplit = (maxsplit) ? : slice.len;
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

    while (str && (v.len < maxsplit)) {
        struct sol_str_slice *s;
        char *token = memmem(str, len, delim, dlen);
        if (!token) {
            CREATE_SLICE(str, len);
            break;
        }

        len -= (token - str) + dlen;
        CREATE_SLICE(str, token - str);
        str = token + dlen;
    }
#undef CREATE_SLICE

    return v;

err:
    sol_vector_clear(&v);
    return v;
}
