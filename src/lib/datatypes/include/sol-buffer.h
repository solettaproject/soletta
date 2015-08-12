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

#include <assert.h>

#include <sol-str-slice.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Solleta provides for its buffer implementation.
 */

/**
 * @defgroup Datatypes Data types
 *
 * Soletta provides some data types to make development easier.
 * Focused on low memory footprints, they're a good choice for
 * node types development.
 *
 * @defgroup Buffer
 *
 * Buffer is a dynamic array, that can be resized if needed.
 * See also \ref Arena if you are allocating multiple pieces of data that will
 * be de-allocated twice.
 *
 * @ingroup Datatypes
 *
 * @{
 */

/**
 * @struct sol_buffer
 *
 * A sol_buffer is a dynamic array, that can be resized if needed. It
 * grows exponentially but also supports setting a specific size.
 *
 * Useful to reduce the noise of handling realloc/size-variable
 * manually.
 *
 * See also sol-arena.h if you are allocating multiple pieces of data
 * that will be de-allocated twice.
 */

enum sol_buffer_flags {
    /**
     * default flags: buffer may be resized and memory will be free'd
     * at the end.
     */
    SOL_BUFFER_FLAGS_DEFAULT = 0,
    /**
     * fixed capacity buffers won't be resized, sol_buffer_resize()
     * will fail with -EPERM.
     */
    SOL_BUFFER_FLAGS_FIXED_CAPACITY = (1 << 0),
    /**
     * no free buffers won't call @c free(buf->data) at
     * sol_buffer_fini().
     */
    SOL_BUFFER_FLAGS_NO_FREE = (1 << 1),
    /**
     * buffers where the @c buf->data is not owned by sol_buffer, that
     * is, it can't be resized and free() should not be called at it
     * at sol_buffer_fini().
     */
    SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED = (SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_NO_FREE)
};

struct sol_buffer {
    void *data;
    size_t capacity, used;
    enum sol_buffer_flags flags;
};

#define SOL_BUFFER_INIT_EMPTY (struct sol_buffer){.data = NULL, .capacity = 0, .used = 0, .flags = SOL_BUFFER_FLAGS_DEFAULT }
#define SOL_BUFFER_INIT_FLAGS(data_, size_, flags_) (struct sol_buffer){.data = data_, .capacity = size_, .used = 0, .flags = flags_ }

static inline void
sol_buffer_init(struct sol_buffer *buf)
{
    assert(buf);
    buf->data = NULL;
    buf->capacity = 0;
    buf->used = 0;
    buf->flags = SOL_BUFFER_FLAGS_DEFAULT;
}

static inline void
sol_buffer_init_flags(struct sol_buffer *buf, void *data, size_t data_size, enum sol_buffer_flags flags)
{
    assert(buf);
    buf->data = data;
    buf->capacity = data_size;
    buf->used = 0;
    buf->flags = flags;
}

static inline void
sol_buffer_fini(struct sol_buffer *buf)
{
    if (!buf)
        return;
    if (!(buf->flags & SOL_BUFFER_FLAGS_NO_FREE))
        free(buf->data);
    buf->data = NULL;
    buf->used = 0;
    buf->capacity = 0;
}

int sol_buffer_resize(struct sol_buffer *buf, size_t new_size);

/* Ensure that 'buf' has at least the given 'min_size'. It may
 * allocate more than requested. */
int sol_buffer_ensure(struct sol_buffer *buf, size_t min_size);

/* Copy the 'slice' into 'buf', ensuring that it will fit, including
 * an extra NUL byte so the buffer can be used as a cstr. */
int sol_buffer_set_slice(struct sol_buffer *buf, const struct sol_str_slice slice);

static inline struct sol_str_slice
sol_buffer_get_slice(const struct sol_buffer *buf)
{
    if (!buf)
        return SOL_STR_SLICE_STR(NULL, 0);
    return SOL_STR_SLICE_STR(buf->data, buf->used);
}

static inline struct sol_str_slice
sol_buffer_get_slice_at(const struct sol_buffer *buf, size_t pos)
{
    if (!buf || buf->used < pos)
        return SOL_STR_SLICE_STR(NULL, 0);
    return SOL_STR_SLICE_STR((char *)buf->data + pos, buf->used - pos);
}

/* Appends the 'slice' into 'buf', reallocating if necessary. */
int sol_buffer_append_slice(struct sol_buffer *buf, const struct sol_str_slice slice);

/* append the formatted string to buffer, including trailing \0 */
int sol_buffer_append_vprintf(struct sol_buffer *buf, const char *fmt, va_list args);
static inline int sol_buffer_append_printf(struct sol_buffer *buf, const char *fmt, ...) SOL_ATTR_PRINTF(2, 3);
static inline int
sol_buffer_append_printf(struct sol_buffer *buf, const char *fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = sol_buffer_append_vprintf(buf, fmt, args);
    va_end(args);
    return r;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
