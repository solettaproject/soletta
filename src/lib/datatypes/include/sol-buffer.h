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

#include <errno.h>
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
 * @defgroup Buffer Buffer
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
    SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED = (SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_NO_FREE),
    /**
     * do not reserve space for the NUL byte
     */
    SOL_BUFFER_FLAGS_NO_NUL_BYTE = (1 << 2)
};

struct sol_buffer {
    void *data;
    size_t capacity, used;
    enum sol_buffer_flags flags;
};

#define SOL_BUFFER_INIT_EMPTY (struct sol_buffer){.data = NULL, .capacity = 0, .used = 0, .flags = SOL_BUFFER_FLAGS_DEFAULT }
#define SOL_BUFFER_INIT_FLAGS(data_, size_, flags_) (struct sol_buffer){.data = data_, .capacity = size_, .used = 0, .flags = flags_ }
#define SOL_BUFFER_INIT_CONST(data_, size_) (struct sol_buffer){.data = data_, .capacity = size_, .used = size_, .flags = SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED }

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

static inline void *
sol_buffer_at(const struct sol_buffer *buf, size_t pos)
{
    if (!buf)
        return NULL;
    if (pos > buf->used)
        return NULL;
    return (char *)buf->data + pos;
}

static inline void *
sol_buffer_at_end(const struct sol_buffer *buf)
{
    if (!buf)
        return NULL;
    return (char *)buf->data + buf->used;
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
    return SOL_STR_SLICE_STR(sol_buffer_at(buf,  pos), buf->used - pos);
}

/* Insert the 'c' into 'buf' at position 'pos', reallocating if necessary.
 *
 * If pos == buf->end, then the behavior is the same as
 * sol_buffer_append_char() and a trailing '\0' is
 * guaranteed.
 */
int sol_buffer_insert_char(struct sol_buffer *buf, size_t pos, const char c);

/* Appends the 'c' into 'buf', reallocating if necessary. */
int sol_buffer_append_char(struct sol_buffer *buf, const char c);

/* Appends the 'slice' into 'buf', reallocating if necessary. */
int sol_buffer_append_slice(struct sol_buffer *buf, const struct sol_str_slice slice);

/* Set a slice at at position @a pos of @a buf. If @a pos plus the @a
 * slice's length is greater than the used portion of @a buf, it
 * ensures that @a buf has the resulting new lenght. @a pos can't
 * start after the buffer's used portion. The memory regions of @a
 * slice and @a buf may overlap. */
int sol_buffer_set_slice_at(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice);

/* Insert the 'slice' into 'buf' at position 'pos', reallocating if necessary.
 *
 * If pos == buf->end, then the behavior is the same as
 * sol_buffer_append_slice() and a trailing '\0' is
 * guaranteed.
 */
int sol_buffer_insert_slice(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice);


// TODO: move this to some other file? where
/**
 * The default base 64 map to use. The last byte (position 64) is the
 * padding character.
 */
extern const char SOL_BASE64_MAP[65];

/**
 * Insert the 'slice' into 'buf' at position 'pos' encoded as base64 using the given map.
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_as_base64().
 * @param slice the byte string to encode, may contain null bytes
 *        @c(\0), it will be encoded up the @c slice.len.
 * @param base64_map the map to use, the default is available as
 *        #SOL_BASE64_MAP. Note that the last char in the map (position 64)
 *        is used as the padding char.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_append_as_base64()
 * @see sol_buffer_insert_from_base64()
 * @see sol_buffer_append_from_base64()
 */
int sol_buffer_insert_as_base64(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, const char base64_map[static 65]);

/**
 * Append the 'slice' at the end of 'buf' encoded as base64 using the given map.
 *
 * See https://en.wikipedia.org/wiki/Base64
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param slice the byte string to encode, may contain null bytes
 *        @c(\0), it will be encoded up the @c slice.len.
 * @param base64_map the map to use, the default is available as
 *        #SOL_BASE64_MAP. Note that the last char in the map (position 64)
 *        is used as the padding char.
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base64()
 * @see sol_buffer_insert_from_base64()
 * @see sol_buffer_append_from_base64()
 */
int sol_buffer_append_as_base64(struct sol_buffer *buf, const struct sol_str_slice slice, const char base64_map[static 65]);

/**
 * Insert the 'slice' into 'buf' at position 'pos' decoded from base64 using the given map.
 *
 * @param buf the already-initialized buffer to append the decoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_from_base64().
 * @param slice the slice to decode, it must be composed solely of the
 *        base64_map characters or it will fail.
 * @param base64_map the map to use, the default is available as
 *        #SOL_BASE64_MAP. Note that the last char in the map (position 64)
 *        is used as the padding char.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base64()
 * @see sol_buffer_append_as_base64()
 * @see sol_buffer_append_from_base64()
 */
int sol_buffer_insert_from_base64(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, const char base64_map[static 65]);

/**
 * Append the 'slice' at the end of 'buf' decoded from base64 using the given map.
 *
 * See https://en.wikipedia.org/wiki/Base64
 *
 * @param buf the already-initialized buffer to append the decoded
 *        slice.
 * @param slice the slice to decode, it must be composed solely of the
 *        base64_map characters or it will fail.
 * @param base64_map the map to use, the default is available as
 *        #SOL_BASE64_MAP. Note that the last char in the map (position 64)
 *        is used as the padding char.
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base64()
 * @see sol_buffer_append_as_base64()
 * @see sol_buffer_insert_from_base64()
 */
int sol_buffer_append_from_base64(struct sol_buffer *buf, const struct sol_str_slice slice, const char base64_map[static 65]);

/**
 * Insert the 'slice' into 'buf' at position 'pos' encoded as base16 (hexadecimal).
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_as_base16().
 * @param slice the byte string to encode, may contain null bytes
 *        @c(\0), it will be encoded up the @c slice.len.
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_append_as_base16()
 * @see sol_buffer_insert_from_base16()
 * @see sol_buffer_append_from_base16()
 */
int sol_buffer_insert_as_base16(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, bool uppercase);

/**
 * Append the 'slice' at the end of 'buf' encoded as base16 (hexadecimal).
 *
 * See https://en.wikipedia.org/wiki/Base16
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param slice the byte string to encode, may contain null bytes
 *        @c(\0), it will be encoded up the @c slice.len.
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base16()
 * @see sol_buffer_insert_from_base16()
 * @see sol_buffer_append_from_base16()
 */
int sol_buffer_append_as_base16(struct sol_buffer *buf, const struct sol_str_slice slice, bool uppercase);

/**
 * Insert the 'slice' into 'buf' at position 'pos' decoded from base16 (hexadecimal).
 *
 * @param buf the already-initialized buffer to append the decoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_from_base16().
 * @param slice the slice to decode, it must be a set of 0-9 or
 *        letters A-F (if uppercase) or a-f, otherwise decode fails.
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base16()
 * @see sol_buffer_append_as_base16()
 * @see sol_buffer_append_from_base16()
 */
int sol_buffer_insert_from_base16(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, bool uppercase);

/**
 * Append the 'slice' at the end of 'buf' decoded from base16 (hexadecimal).
 *
 * See https://en.wikipedia.org/wiki/Base16
 *
 * @param buf the already-initialized buffer to append the decoded
 *        slice.
 * @param slice the slice to decode, it must be a set of 0-9 or
 *        letters A-F (if uppercase) or a-f, otherwise decode fails.
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base16()
 * @see sol_buffer_append_as_base16()
 * @see sol_buffer_insert_from_base16()
 */
int sol_buffer_append_from_base16(struct sol_buffer *buf, const struct sol_str_slice slice, bool uppercase);

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

/* insert the formatted string into the buffer at given position.  If
 * position == buf->pos, then the behavior is the same as
 * sol_buffer_append_vprintf().
 */
int sol_buffer_insert_vprintf(struct sol_buffer *buf, size_t pos, const char *fmt, va_list args);
static inline int sol_buffer_insert_printf(struct sol_buffer *buf, size_t pos, const char *fmt, ...) SOL_ATTR_PRINTF(3, 4);
static inline int
sol_buffer_insert_printf(struct sol_buffer *buf, size_t pos, const char *fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = sol_buffer_insert_vprintf(buf, pos, fmt, args);
    va_end(args);
    return r;
}

static inline int
sol_buffer_trim(struct sol_buffer *buf)
{
    if (!buf)
        return -EINVAL;

    if (buf->used == buf->capacity)
        return 0;

    return sol_buffer_resize(buf, buf->used);
}

/**
 *  'Steals' sol_buffer internal buffer and resets sol_buffer.
 *
 *  After this call, user is responsible for the memory returned.
 *
 *  @param buf buffer to have it's internal buffer stolen
 *  @param size if not NULL, will store memory returned size
 *
 *  @return @a buffer internal buffer. It's caller responsibility now
 *  to free this memory
 *
 *  @note If @a buffer was allocated with @c sol_buffer_new(), it still
 *  needs to be freed by calling @c sol_buffer_free();
 */
void *sol_buffer_steal(struct sol_buffer *buf, size_t *size);

/**
 *  Allocate a new sol_buffer and a new data block and copy the
 *  contents of the provided sol_buffer
 *
 *  After this call, user is responsible for calling fini on the
 *  buffer and freeing it afterwards. For it's memory to be freed
 *  properly, the flag SOL_BUFFER_FLAGS_NO_FREE will always be
 *  unset, despite the original buffer
 *
 *  @param buf buffer to be copied
 *
 *  @return A copy of buf or NULL on error.
 */
struct sol_buffer *sol_buffer_copy(const struct sol_buffer *buf);

static inline void
sol_buffer_reset(struct sol_buffer *buf)
{
    buf->used = 0;
}

static inline struct sol_buffer *
sol_buffer_new(void)
{
    struct sol_buffer *buf = calloc(1, sizeof(struct sol_buffer));

    if (!buf) return NULL;

    sol_buffer_init(buf);

    return buf;
}

static inline void
sol_buffer_free(struct sol_buffer *buf)
{
    sol_buffer_fini(buf);
    free(buf);
}

/**
 * Ensures that buffer has a terminating NUL byte, if
 * flag SOL_BUFFER_FLAGS_NO_NUL_BYTE is not set.
 *
 * @return a negative number in case it was not possible to
 * ensure a terminating NUL byte - if flag SOL_BUFFER_FLAGS_NO_NUL_BYTE
 * is set for instance, or if it could not resize the buffer
 */
int sol_buffer_ensure_nul_byte(struct sol_buffer *buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
