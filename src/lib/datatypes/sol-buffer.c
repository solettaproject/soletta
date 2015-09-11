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

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-util.h"

SOL_ATTR_PURE static inline size_t
nul_byte_size(struct sol_buffer *buf)
{
    return (buf->flags & SOL_BUFFER_FLAGS_NO_NUL_BYTE) ? 0 : 1;
}

SOL_API int
sol_buffer_resize(struct sol_buffer *buf, size_t new_size)
{
    char *new_data;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_EXP_CHECK(buf->flags & SOL_BUFFER_FLAGS_FIXED_CAPACITY, -EPERM);
    SOL_EXP_CHECK(buf->flags & SOL_BUFFER_FLAGS_NO_FREE, -EPERM);

    if (buf->capacity == new_size)
        return 0;

    new_data = realloc(buf->data, new_size);
    if (!new_data)
        return -errno;

    buf->data = new_data;
    buf->capacity = new_size;
    return 0;
}

SOL_API int
sol_buffer_ensure(struct sol_buffer *buf, size_t min_size)
{
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    if (min_size >= SIZE_MAX - nul_byte_size(buf))
        return -EINVAL;
    if (buf->capacity >= min_size)
        return 0;

    err = sol_buffer_resize(buf, align_power2(min_size + nul_byte_size(buf)));
    if (err == -EPERM)
        return -ENOMEM;
    return err;
}

SOL_API int
sol_buffer_set_slice(struct sol_buffer *buf, const struct sol_str_slice slice)
{
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    /* Extra room for the ending NUL-byte. */
    if (slice.len >= SIZE_MAX - nul_byte_size(buf))
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, slice.len + nul_byte_size(buf));
    if (err < 0)
        return err;

    sol_str_slice_copy(buf->data, slice);
    buf->used = slice.len;
    return 0;
}

SOL_API int
sol_buffer_append_slice(struct sol_buffer *buf, const struct sol_str_slice slice)
{
    const size_t nul_size = nul_byte_size(buf);
    char *p;
    size_t new_size;
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    err = sol_util_size_add(buf->used, slice.len, &new_size);
    if (err < 0)
        return err;

    /* Extra room for the ending NUL-byte. */
    if (new_size >= SIZE_MAX - nul_size)
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, new_size + nul_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    memcpy(p, slice.data, slice.len);

    if (nul_size)
        p[slice.len] = '\0';
    buf->used += slice.len;
    return 0;
}

SOL_API int
sol_buffer_set_slice_at(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice)
{
    int err;
    const size_t nul_size = nul_byte_size(buf);

    SOL_NULL_CHECK(buf, -EINVAL);
    if (buf->used < pos) {
        return -EINVAL;
    }

    /* Extra room for the ending NUL-byte. */
    if (slice.len >= SIZE_MAX - nul_size - pos)
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, pos + slice.len + nul_size);
    if (err < 0)
        return err;

    /* deal with possible overlaps with memmove */
    memmove((char *)buf->data + pos, slice.data, slice.len);

    if (pos + slice.len >= buf->used) {
        buf->used = pos + slice.len;
        /* only terminate if we're growing */
        ((char *)buf->data)[buf->used] = 0;
    }

    return 0;
}

SOL_API int
sol_buffer_insert_slice(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice)
{
    const size_t nul_size = nul_byte_size(buf);
    char *p;
    size_t new_size;
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (pos == buf->used)
        return sol_buffer_append_slice(buf, slice);

    err = sol_util_size_add(buf->used, slice.len, &new_size);
    if (err < 0)
        return err;

    /* Extra room for the ending NUL-byte. */
    if (new_size >= SIZE_MAX - nul_size)
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, new_size + nul_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + slice.len, p, buf->used - pos);
    memcpy(p, slice.data, slice.len);
    buf->used += slice.len;

    if (nul_size) {
        p = sol_buffer_at_end(buf);
        p[0] = '\0';
    }

    return 0;
}

SOL_API int
sol_buffer_append_vprintf(struct sol_buffer *buf, const char *fmt, va_list args)
{
    va_list args_copy;
    size_t space;
    char *p;
    ssize_t done;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(fmt, -EINVAL);

    /* We need to copy args since we first try to do one vsnprintf()
     * to the existing space and if it doesn't fit we do a second
     * one. The second one would fail since args may be changed by
     * vsnprintf(), so the copy.
     */
    va_copy(args_copy, args);

    space = buf->capacity - buf->used;
    p = sol_buffer_at_end(buf);
    done = vsnprintf(p, space, fmt, args_copy);
    if (done < 0) {
        r = -errno;
        goto end;
    } else if ((size_t)done >= space) {
        size_t new_size;

        r = sol_util_size_add(buf->used, done, &new_size);
        if (r < 0)
            goto end;

        /* Extra room for the ending NUL-byte. */
        if (new_size >= SIZE_MAX - nul_byte_size(buf)) {
            r = -EOVERFLOW;
            goto end;
        }
        r = sol_buffer_ensure(buf, new_size + nul_byte_size(buf));
        if (r < 0)
            goto end;

        space = buf->capacity - buf->used;
        p = sol_buffer_at_end(buf);
        done = vsnprintf(p, space, fmt, args);
        if (done < 0) {
            r = -errno;
            goto end;
        }
    }

    buf->used += done;
    r = 0;

end:
    va_end(args_copy);
    return r;
}

SOL_API int
sol_buffer_insert_vprintf(struct sol_buffer *buf, size_t pos, const char *fmt, va_list args)
{
    char *s;
    ssize_t len;
    struct sol_str_slice slice;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(fmt, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (pos == buf->used)
        return sol_buffer_append_vprintf(buf, fmt, args);

    len = vasprintf(&s, fmt, args);
    if (len < 0)
        return -errno;

    slice = SOL_STR_SLICE_STR(s, len);
    r = sol_buffer_insert_slice(buf, pos, slice);
    free(s);
    return r;
}

SOL_API void *
sol_buffer_steal(struct sol_buffer *buf, size_t *size)
{
    void *r;

    SOL_NULL_CHECK(buf, NULL);

    r = buf->data;

    if (size)
        *size = buf->used;

    buf->data = NULL;
    buf->used = 0;
    buf->capacity = 0;

    return r;
}

SOL_API struct sol_buffer *
sol_buffer_copy(const struct sol_buffer *buf)
{
    struct sol_buffer *b_copy;

    if (!buf) return NULL;

    b_copy = sol_util_memdup(buf, sizeof(*buf));
    if (!b_copy) return NULL;

    b_copy->data = sol_util_memdup(buf->data, buf->used);
    if (!b_copy->data) {
        free(b_copy);
        return NULL;
    }

    b_copy->flags = SOL_BUFFER_FLAGS_DEFAULT;

    return b_copy;
}
