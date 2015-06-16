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

#include "sol-buffer.h"
#include "sol-util.h"

int
sol_buffer_resize(struct sol_buffer *buf, unsigned int new_size)
{
    char *new_data;

    assert(buf);

    if (buf->size == new_size)
        return 0;

    new_data = realloc(buf->data, new_size * sizeof(char));
    if (!new_data)
        return -errno;

    buf->data = new_data;
    buf->size = new_size;
    return 0;
}

int
sol_buffer_ensure(struct sol_buffer *buf, unsigned int min_size)
{
    assert(buf);
    if (min_size >= UINT_MAX - 1)
        return -EINVAL;
    if (buf->size >= min_size)
        return 0;
    return sol_buffer_resize(buf, align_power2(min_size + 1));
}

int
sol_buffer_copy_slice(struct sol_buffer *buf, struct sol_str_slice slice)
{
    int err;

    /* Extra room for the ending NUL-byte. */
    err = sol_buffer_ensure(buf, slice.len + 1);
    if (err < 0)
        return err;

    sol_str_slice_copy(buf->data, slice);
    return 0;
}
