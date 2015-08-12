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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-util.h"

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

    if (min_size >= SIZE_MAX - 1)
        return -EINVAL;
    if (buf->capacity >= min_size)
        return 0;

    err = sol_buffer_resize(buf, align_power2(min_size + 1));
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
    err = sol_buffer_ensure(buf, slice.len + 1);
    if (err < 0)
        return err;

    sol_str_slice_copy(buf->data, slice);
    buf->used = slice.len;
    return 0;
}

SOL_API int
sol_buffer_append_slice(struct sol_buffer *buf, const struct sol_str_slice slice)
{
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    /* Extra room for the ending NUL-byte. */
    /* FIXME: len+used might overflow! */
    err = sol_buffer_ensure(buf, slice.len + buf->used + 1);
    if (err < 0)
        return err;

    sol_str_slice_copy((char *)buf->data + buf->used, slice);
    buf->used += slice.len;
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
    p = (char *)buf->data + buf->used;
    done = vsnprintf(p, space, fmt, args_copy);
    if (done < 0) {
        r = -errno;
        goto end;
    } else if ((size_t)done >= space) {
        r = sol_buffer_ensure(buf, buf->used + done + 1);
        if (r < 0)
            goto end;

        space = buf->capacity - buf->used;
        p = (char *)buf->data + buf->used;
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
