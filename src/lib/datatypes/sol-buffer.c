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
    if (!new_data && new_size)
        return -errno;

    buf->data = new_data;
    buf->capacity = new_size;
    if (buf->used > new_size)
        buf->used = new_size;
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
sol_buffer_append_bytes(struct sol_buffer *buf, const uint8_t *bytes, size_t size)
{
    const size_t nul_size = nul_byte_size(buf);
    char *p;
    size_t new_size;
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    err = sol_util_size_add(buf->used, size, &new_size);
    if (err < 0)
        return err;

    /* Extra room for the ending NUL-byte. */
    if (new_size >= SIZE_MAX - nul_size)
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, new_size + nul_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    memcpy(p, bytes, size);

    if (nul_size)
        p[size] = '\0';
    buf->used += size;
    return 0;
}

SOL_API int
sol_buffer_append_slice(struct sol_buffer *buf, const struct sol_str_slice slice)
{
    return sol_buffer_append_bytes(buf, (uint8_t *)slice.data, slice.len);
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
sol_buffer_set_char_at(struct sol_buffer *buf, size_t pos, char c)
{
    int err;
    const size_t nul_size = nul_byte_size(buf);

    SOL_NULL_CHECK(buf, -EINVAL);
    if (buf->used < pos) {
        return -EINVAL;
    }

    /* Extra room for the ending NUL-byte. */
    if (1 >= SIZE_MAX - nul_size - pos)
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, pos + 1 + nul_size);
    if (err < 0)
        return err;

    *((char *)buf->data + pos) = c;

    if (pos + 1 >= buf->used)
        buf->used = pos + 1;

    if (nul_byte_size(buf))
        return sol_buffer_ensure_nul_byte(buf);

    return 0;
}

SOL_API int
sol_buffer_insert_bytes(struct sol_buffer *buf, size_t pos, const uint8_t *bytes, size_t size)
{
    const size_t nul_size = nul_byte_size(buf);
    char *p;
    size_t new_size;
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (pos == buf->used)
        return sol_buffer_append_bytes(buf, bytes, size);

    err = sol_util_size_add(buf->used, size, &new_size);
    if (err < 0)
        return err;

    /* Extra room for the ending NUL-byte. */
    if (new_size >= SIZE_MAX - nul_size)
        return -EOVERFLOW;
    err = sol_buffer_ensure(buf, new_size + nul_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + size, p, buf->used - pos);
    memcpy(p, bytes, size);
    buf->used += size;

    if (nul_size) {
        p = sol_buffer_at_end(buf);
        p[0] = '\0';
    }

    return 0;
}

SOL_API int
sol_buffer_insert_slice(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice)
{
    return sol_buffer_insert_bytes(buf, pos, (uint8_t *)slice.data, slice.len);
}

/* Extra room for the ending NUL-byte, necessary even when the buffer has
 * SOL_BUFFER_FLAGS_NO_NUL_BYTE because vsnprintf always write the '\0'. */
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
        if (new_size >= SIZE_MAX - 1) {
            r = -EOVERFLOW;
            goto end;
        }
        r = sol_buffer_ensure(buf, new_size + 1);
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

    if (buf->flags & SOL_BUFFER_FLAGS_NO_FREE)
        return NULL;

    r = buf->data;

    if (size)
        *size = buf->used;

    buf->data = NULL;
    buf->used = 0;
    buf->capacity = 0;

    return r;
}

SOL_API void *
sol_buffer_steal_or_copy(struct sol_buffer *buf, size_t *size)
{
    void *r;

    SOL_NULL_CHECK(buf, NULL);

    r = sol_buffer_steal(buf, size);
    if (!r) {
        r = sol_util_memdup(buf->data, buf->used);
        SOL_NULL_CHECK(r, NULL);

        if (size)
            *size = buf->used;
    }

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

    b_copy->flags &= ~SOL_BUFFER_FLAGS_NO_FREE;

    return b_copy;
}

SOL_API int
sol_buffer_ensure_nul_byte(struct sol_buffer *buf)
{
    SOL_NULL_CHECK(buf, -EINVAL);

    if (buf->flags & SOL_BUFFER_FLAGS_NO_NUL_BYTE)
        return -EINVAL;

    if (buf->used && *((char *)sol_buffer_at_end(buf) - 1) == '\0')
        return 0;

    if (buf->used >= SIZE_MAX - 1 ||
        sol_buffer_ensure(buf, buf->used + 1) < 0)
        return -ENOMEM;

    *((char *)buf->data + buf->used) = '\0';

    return 0;
}

SOL_API int
sol_buffer_append_char(struct sol_buffer *buf, const char c)
{
    char *p;
    size_t new_size;
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    err = sol_util_size_add(buf->used, 1, &new_size);
    if (err < 0)
        return err;

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    *p = c;
    buf->used++;

    if (nul_byte_size(buf))
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_insert_char(struct sol_buffer *buf, size_t pos, const char c)
{
    char *p;
    size_t new_size;
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (pos == buf->used)
        return sol_buffer_append_char(buf, c);

    err = sol_util_size_add(buf->used, 1, &new_size);
    if (err < 0)
        return err;

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + 1, p, buf->used - pos);
    *p = c;
    buf->used++;

    if (nul_byte_size(buf))
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API const char SOL_BASE64_MAP[66] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

SOL_API int
sol_buffer_insert_as_base64(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, const char base64_map[static 65])
{
    char *p;
    size_t new_size;
    ssize_t encoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (slice.len == 0)
        return 0;

    if (pos == buf->used)
        return sol_buffer_append_as_base64(buf, slice, base64_map);

    encoded_size = sol_util_base64_calculate_encoded_len(slice, base64_map);
    if (encoded_size < 0)
        return encoded_size;

    err = sol_util_size_add(buf->used, encoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + encoded_size, p, buf->used - pos);
    r = sol_util_base64_encode(p, encoded_size, slice, base64_map);
    if (r != encoded_size) {
        memmove(p, p + encoded_size, buf->used - pos);
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += encoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_append_as_base64(struct sol_buffer *buf, const struct sol_str_slice slice, const char base64_map[static 65])
{
    char *p;
    size_t new_size;
    ssize_t encoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    if (slice.len == 0)
        return 0;

    encoded_size = sol_util_base64_calculate_encoded_len(slice, base64_map);
    if (encoded_size < 0)
        return encoded_size;

    err = sol_util_size_add(buf->used, encoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    r = sol_util_base64_encode(p, encoded_size, slice, base64_map);
    if (r != encoded_size) {
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += encoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_insert_from_base64(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, const char base64_map[static 65])
{
    char *p;
    size_t new_size;
    ssize_t decoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (slice.len == 0)
        return 0;

    if (pos == buf->used)
        return sol_buffer_append_from_base64(buf, slice, base64_map);

    decoded_size = sol_util_base64_calculate_decoded_len(slice, base64_map);
    if (decoded_size < 0)
        return decoded_size;

    err = sol_util_size_add(buf->used, decoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + decoded_size, p, buf->used - pos);
    r = sol_util_base64_decode(p, decoded_size, slice, base64_map);
    if (r != decoded_size) {
        memmove(p, p + decoded_size, buf->used - pos);
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += decoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_append_from_base64(struct sol_buffer *buf, const struct sol_str_slice slice, const char base64_map[static 65])
{
    char *p;
    size_t new_size;
    ssize_t decoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    if (slice.len == 0)
        return 0;

    decoded_size = sol_util_base64_calculate_decoded_len(slice, base64_map);
    if (decoded_size < 0)
        return decoded_size;

    err = sol_util_size_add(buf->used, decoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    r = sol_util_base64_decode(p, decoded_size, slice, base64_map);
    if (r != decoded_size) {
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += decoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_insert_as_base16(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, bool uppercase)
{
    char *p;
    size_t new_size;
    ssize_t encoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (slice.len == 0)
        return 0;

    if (pos == buf->used)
        return sol_buffer_append_as_base16(buf, slice, uppercase);

    encoded_size = sol_util_base16_calculate_encoded_len(slice);
    if (encoded_size < 0)
        return encoded_size;

    err = sol_util_size_add(buf->used, encoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + encoded_size, p, buf->used - pos);
    r = sol_util_base16_encode(p, encoded_size, slice, uppercase);
    if (r != encoded_size) {
        memmove(p, p + encoded_size, buf->used - pos);
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += encoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_append_as_base16(struct sol_buffer *buf, const struct sol_str_slice slice, bool uppercase)
{
    char *p;
    size_t new_size;
    ssize_t encoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    if (slice.len == 0)
        return 0;

    encoded_size = sol_util_base16_calculate_encoded_len(slice);
    if (encoded_size < 0)
        return encoded_size;

    err = sol_util_size_add(buf->used, encoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    r = sol_util_base16_encode(p, encoded_size, slice, uppercase);
    if (r != encoded_size) {
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += encoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_insert_from_base16(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, enum sol_decode_case decode_case)
{
    char *p;
    size_t new_size;
    ssize_t decoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(pos, > buf->used, -EINVAL);

    if (slice.len == 0)
        return 0;

    if (pos == buf->used)
        return sol_buffer_append_from_base16(buf, slice, decode_case);

    decoded_size = sol_util_base16_calculate_decoded_len(slice);
    if (decoded_size < 0)
        return decoded_size;

    err = sol_util_size_add(buf->used, decoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at(buf, pos);
    memmove(p + decoded_size, p, buf->used - pos);
    r = sol_util_base16_decode(p, decoded_size, slice, decode_case);
    if (r != decoded_size) {
        memmove(p, p + decoded_size, buf->used - pos);
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += decoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_append_from_base16(struct sol_buffer *buf, const struct sol_str_slice slice, enum sol_decode_case decode_case)
{
    char *p;
    size_t new_size;
    ssize_t decoded_size, r;
    const size_t nul_size = nul_byte_size(buf);
    int err;

    SOL_NULL_CHECK(buf, -EINVAL);

    if (slice.len == 0)
        return 0;

    decoded_size = sol_util_base16_calculate_decoded_len(slice);
    if (decoded_size < 0)
        return decoded_size;

    err = sol_util_size_add(buf->used, decoded_size, &new_size);
    if (err < 0)
        return err;

    if (nul_size) {
        err = sol_util_size_add(new_size, nul_size, &new_size);
        if (err < 0)
            return err;
    }

    err = sol_buffer_ensure(buf, new_size);
    if (err < 0)
        return err;

    p = sol_buffer_at_end(buf);
    r = sol_util_base16_decode(p, decoded_size, slice, decode_case);
    if (r != decoded_size) {
        if (nul_size)
            sol_buffer_ensure_nul_byte(buf);
        if (r < 0)
            return r;
        else
            return -EINVAL;
    }

    buf->used += decoded_size;

    if (nul_size)
        return sol_buffer_ensure_nul_byte(buf);
    return 0;
}

SOL_API int
sol_buffer_remove_data(struct sol_buffer *buf, size_t size, unsigned long offset)
{
    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_EXP_CHECK(buf->flags & SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED, -EPERM);

    if ((buf->used < offset) ||
        (buf->used < size)   ||
        (buf->used < (offset + size)))
        return -EINVAL;

    if (buf->used != (offset + size)) {
        memmove((char *)buf->data + offset,
            (char *)buf->data + offset + size,
            buf->used - size - offset);
    }

    buf->used -= size;

    return 0;
}
