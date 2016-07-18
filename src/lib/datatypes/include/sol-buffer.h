/*
 * This file is part of the Soletta (TM) Project
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

#pragma once

#include <assert.h>

#include <errno.h>
#include <sol-str-slice.h>
#include <sol-types.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define SOL_BUFFER_C_CAST
#else
#define SOL_BUFFER_C_CAST (struct sol_buffer)
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for its buffer implementation.
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
 * @brief Buffer is a dynamic array, that can be resized if needed.
 *
 * See also \ref Arena if you are allocating multiple pieces of data that will
 * be deallocated twice.
 *
 * @ingroup Datatypes
 *
 * @see Arena
 *
 * @{
 */

/**
 * @brief Flags used to set @ref sol_buffer capabilities.
 */
enum sol_buffer_flags {
    /**
     * @brief Default flags: buffer may be resized and memory will be free'd
     * at the end.
     */
    SOL_BUFFER_FLAGS_DEFAULT = 0,
    /**
     * @brief Fixed capacity buffers won't be resized, sol_buffer_resize()
     * will fail with -EPERM.
     */
    SOL_BUFFER_FLAGS_FIXED_CAPACITY = (1 << 0),
    /**
     * @brief No free buffers won't call @c free(buf->data) at
     * sol_buffer_fini().
     */
    SOL_BUFFER_FLAGS_NO_FREE = (1 << 1),
    /**
     * @brief Buffers where the @c buf->data is not owned by sol_buffer, that
     * is, it can't be resized and free() should not be called at it
     * at sol_buffer_fini().
     */
    SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED = (SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_NO_FREE),
    /**
     * @brief Do not reserve space for the NUL byte.
     */
    SOL_BUFFER_FLAGS_NO_NUL_BYTE = (1 << 2),
    /**
     * @brief Securely clear buffer data before finishing. Prefer using this
     * flag combined with SOL_BUFFER_FLAGS_FIXED_CAPACITY, because of resizing
     * overhead: every time buffer is resized, new memory is allocated, old
     * memory is copied to new destination and old memory is cleared.
     */
    SOL_BUFFER_FLAGS_CLEAR_MEMORY = (1 << 3),
};

/**
 * @def SOL_BUFFER_NEEDS_NUL_BYTE()
 *
 * Convenience flag to check for flags not containing
 * #SOL_BUFFER_FLAGS_NO_NUL_BYTE, that is, buffers that needs the
 * trailing nul byte terminator.
 */
#define SOL_BUFFER_NEEDS_NUL_BYTE(buf) (!((buf)->flags & SOL_BUFFER_FLAGS_NO_NUL_BYTE))

/**
 * @def SOL_BUFFER_CAN_RESIZE()
 *
 * Convenience flag to check if the buffer is resizable.
 */
#define SOL_BUFFER_CAN_RESIZE(buf) \
    (!(buf->flags & (SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED)))

/**
 * @brief A sol_buffer is a dynamic array, that can be resized if needed.
 *
 * It grows exponentially but also supports setting a specific size.
 *
 * Useful to reduce the noise of handling realloc/size-variable
 * manually.
 *
 * See also @ref sol_arena if you are allocating multiple pieces of data
 * that will be deallocated twice.
 */
typedef struct sol_buffer {
    void *data; /**< @brief Buffer data */
    size_t capacity; /**< @brief Buffer capacity in bytes */
    size_t used;  /**< @brief Used size in bytes */
    enum sol_buffer_flags flags; /**< @brief Buffer flags */
} sol_buffer;

/**
 * @brief Case of a string to be decoded.
 *
 * Used by functions @ref sol_buffer_append_from_base16 or @ref sol_buffer_insert_from_base16.
 */
enum sol_decode_case {
    SOL_DECODE_UPPERCASE,
    SOL_DECODE_LOWERCASE,
    SOL_DECODE_BOTH
};

/**
 * @def SOL_BUFFER_INIT_EMPTY
 *
 * @brief Helper macro to initialize an empty buffer.
 */
#define SOL_BUFFER_INIT_EMPTY SOL_BUFFER_C_CAST { .data = NULL, .capacity = 0, .used = 0, .flags = SOL_BUFFER_FLAGS_DEFAULT }

/**
 * @def SOL_BUFFER_INIT_FLAGS(data_, size_, flags_)
 *
 * @brief Helper macro to initialize an buffer with the given data and flags.
 *
 * @param data_ Buffer initial data
 * @param size_ Initial data size
 * @param flags_ Buffer flags
 */
#define SOL_BUFFER_INIT_FLAGS(data_, size_, flags_) SOL_BUFFER_C_CAST { .data = data_, .capacity = size_, .used = 0, .flags = (enum sol_buffer_flags)(flags_) }

/**
 * @def SOL_BUFFER_INIT_CONST(data_, size_)
 *
 * @brief Helper macro to initialize an buffer with @c const data.
 *
 * Also set the appropriated flag.
 *
 * @param data_ Buffer initial data
 * @param size_ Initial data size
 */
#define SOL_BUFFER_INIT_CONST(data_, size_) SOL_BUFFER_C_CAST { .data = data_, .capacity = size_, .used = size_, .flags = SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED }

/**
 * @def SOL_BUFFER_INIT_DATA(data_, size_)
 *
 * @brief Helper macro to initialize an buffer with the given data.
 *
 * @param data_ Buffer initial data
 * @param size_ Initial data size
 */
#define SOL_BUFFER_INIT_DATA(data_, size_) SOL_BUFFER_C_CAST { .data = data_, .capacity = size_, .used = size_, .flags = SOL_BUFFER_FLAGS_DEFAULT }

/**
 * @def SOL_BUFFER_DECLARE_STATIC(name_, size_)
 *
 * @brief A helper macro to create a static allocated buffer with a fixed capacity.
 *
 * This macro will expand into the following code:
 * @code{.c}
 * // SOL_BUFFER_DECLARE_STATIC(buf, 1024);
 * uint8_t buf_storage[1024] = { 0 };
 * struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(buf_storage, 1024, SOL_BUFFER_FLAGS_FIXED_CAPACITY);
 * @endcode
 *
 * @param name_ The name of the struct sol_buffer variable
 * @param size_ The capacity of the buffer
 */
#define SOL_BUFFER_DECLARE_STATIC(name_, size_) \
    uint8_t name_ ## storage[(size_)] = { 0 }; \
    struct sol_buffer name_ = SOL_BUFFER_INIT_FLAGS(name_ ## storage, (size_), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED)

/**
 * @brief Initializes a @c sol_buffer structure.
 *
 * flags are set to @c SOL_BUFFER_FLAGS_DEFAULT.
 *
 * @param buf Pointer to buffer
 */
static inline void
sol_buffer_init(struct sol_buffer *buf)
{
    assert(buf);
    buf->data = NULL;
    buf->capacity = 0;
    buf->used = 0;
    buf->flags = SOL_BUFFER_FLAGS_DEFAULT;
}

/**
 * @brief Initializes a @c sol_buffer structure with the given data and flags.
 *
 * @param buf Pointer to buffer
 * @param data Pointer to buffer initial data
 * @param data_size Size of the initial data
 * @param flags Buffer flags
 */
static inline void
sol_buffer_init_flags(struct sol_buffer *buf, void *data, size_t data_size, enum sol_buffer_flags flags)
{
    assert(buf);
    assert((flags & SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED) ? !!data : 1);
    buf->data = data;
    buf->capacity = data_size;
    buf->used = 0;
    buf->flags = flags;
}

/**
 * @brief Finalizes the buffer.
 *
 * Frees the data managed by the buffer but keeps the buffer handler intact for reuse.
 *
 * @param buf The buffer
 */
void sol_buffer_fini(struct sol_buffer *buf);

/**
 * @brief Returns a pointer to the data at position @c pos in the buffer @c buf.
 *
 * @param buf The buffer
 * @param pos Position of the data
 *
 * @return Pointer to the data at the given position
 */
static inline void *
sol_buffer_at(const struct sol_buffer *buf, size_t pos)
{
    if (!buf)
        return NULL;
    if (pos > buf->used)
        return NULL;
    return (char *)buf->data + pos;
}

/**
 * @brief Returns a pointer to the end of the used portion of the buffer.
 *
 * @param buf The buffer
 *
 * @return Pointer to the end of the buffer
 */
static inline void *
sol_buffer_at_end(const struct sol_buffer *buf)
{
    if (!buf)
        return NULL;
    return (char *)buf->data + buf->used;
}

/**
 * @brief Resize the buffer to the given size.
 *
 * The new size will be exactly of the given @a new_size, no null-byte
 * is automatically handled and if @c used member is larger than the
 * new size, then it's limited to that amount (clamp).
 *
 * @param buf The buffer
 * @param new_size New size
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_resize(struct sol_buffer *buf, size_t new_size);

/**
 * @brief Increment the buffer capacity to fit the @c bytes.
 *
 * This function will increase the buffer capacity in order to
 * be able to fit @c bytes.
 *
 * If buffer has null-bytes (ie: null terminated strings), then the
 * resized amount will include that null byte automatically. See
 * SOL_BUFFER_FLAGS_NO_NUL_BYTE.
 *
 * @param buf The buffer
 * @param bytes The number of bytes that the buffer must fit.
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @note Internally this function uses sol_buffer_ensure()
 */
int sol_buffer_expand(struct sol_buffer *buf, size_t bytes);

/**
 * @brief Ensures that @c buf has at least @c min_size.
 *
 * If buffer has null-bytes (ie: null terminated strings), then the
 * resized amount will include that null byte automatically. See
 * SOL_BUFFER_FLAGS_NO_NUL_BYTE.
 *
 * It may allocate more than requested to avoid subsequent reallocs,
 * the internal heuristic rounds up to next power-of-2.
 *
 * @param buf The buffer
 * @param min_size Minimum size
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_ensure(struct sol_buffer *buf, size_t min_size);

/**
 * @brief Copy @c slice into @c buf, ensuring that will fit.
 *
 * Also includes an extra NULL byte so the buffer data can be used as a C string.
 *
 * If data exists, then it won't be moved/shiffted, instead it will be
 * overriden. Example:
 *  - buffer "abcd", slice "XY", result: "XYcd";
 *  - buffer "XY", slice "abcd", result: "abcd".
 *
 * @param buf The buffer
 * @param slice String slice
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_set_slice(struct sol_buffer *buf, const struct sol_str_slice slice);

/**
 * @brief Creates a string slice from the buffer's valid data.
 *
 * @param buf The buffer
 *
 * @return String slice of the valid data
 */
static inline struct sol_str_slice
sol_buffer_get_slice(const struct sol_buffer *buf)
{
    if (!buf)
        return SOL_STR_SLICE_STR(NULL, 0);
    return SOL_STR_SLICE_STR((char *)buf->data, buf->used);
}

/**
 * @brief Copy @c src into @c dst, ensuring that will fit.
 *
 * If data exists, then it won't be moved/shiffted, instead it will be
 * overriden.
 *
 * @param dst The buffer's destiny
 * @param src The source data
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_buffer_set_buffer(struct sol_buffer *dst, const struct sol_buffer *src)
{
    if (!dst || !src)
        return -EINVAL;

    return sol_buffer_set_slice(dst, sol_buffer_get_slice(src));
}

/**
 * @brief Creates a string slice from the buffer's data starting at position @c pos.
 *
 * @param buf The buffer
 * @param pos Start position
 *
 * @return String slice of the data
 */
static inline struct sol_str_slice
sol_buffer_get_slice_at(const struct sol_buffer *buf, size_t pos)
{
    if (!buf || buf->used < pos)
        return SOL_STR_SLICE_STR(NULL, 0);
    return SOL_STR_SLICE_STR((char *)sol_buffer_at(buf,  pos), buf->used - pos);
}

/**
 * @brief Insert character @c c into @c buf at position @c pos,
 * reallocating if necessary.
 *
 * If pos == buf->end, then the behavior is the same as @ref sol_buffer_append_char
 * and a trailing '\0' is guaranteed.
 *
 * @param buf Destination buffer
 * @param pos Start position
 * @param c Character to be inserted
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_insert_char(struct sol_buffer *buf, size_t pos, const char c);

/**
 * @brief Appends character @c c into the end of @c buf,
 * reallocating if necessary.
 *
 * @param buf Destination buffer
 * @param c Character to be appended
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_append_char(struct sol_buffer *buf, const char c);

/**
 * @brief Appends @c slice into the end of @c buf, reallocating if necessary.
 *
 * @param buf Destination buffer
 * @param slice String slice to be appended
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_append_slice(struct sol_buffer *buf, const struct sol_str_slice slice);

/**
 * @brief Appends the @c bytes array to the end of @c buf,
 * reallocating if necessary.
 *
 * @param buf Destination buffer
 * @param bytes Bytes to be inserted
 * @param size Number of bytes to insert
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_append_bytes(struct sol_buffer *buf, const uint8_t *bytes, size_t size);

/**
 * @brief Appends the contents of the buffer @c from in the end of @c buf,
 * reallocating @c if necessary.
 *
 * @param dst Destination buffer
 * @param src The buffer from where the data will be copied
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_append_buffer(struct sol_buffer *dst, const struct sol_buffer *src);

/**
 * @brief Set the string slice @c slice into @c buf at position @c pos,
 * reallocating if necessary.
 *
 * The memory regions of @a slice and @a buf may overlap.
 *
 * If pos == buf->end, then the behavior is the same as @ref sol_buffer_append_slice.
 *
 * If data exists after @a pos, then it won't be moved/shiffted,
 * instead it will be overriden. Example:
 *  - buffer "abcd", slice "XY", pos: 4, result: "abcdXY";
 *  - buffer "abcd", slice "XY", pos: 3, result: "abcXY";
 *  - buffer "abcd", slice "XY", pos: 2, result: "abXY";
 *  - buffer "abcd", slice "XY", pos: 1, result: "aXYd";
 *  - buffer "abcd", slice "XY", pos: 0, result: "XYcd";
 *
 * @param buf Destination buffer
 * @param pos Start position
 * @param slice String slice to be set
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_set_slice_at(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice);

/**
 * @brief Copy @c src into @c dst at position @c pos,
 * ensuring that will fit.
 *
 * The memory regions of @a src and @a dst may overlap.
 *
 * @param dst The buffer's destiny
 * @param pos Start position
 * @param src The source data
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_buffer_set_buffer_at(struct sol_buffer *dst, size_t pos, const struct sol_buffer *src)
{
    if (!dst || !src)
        return -EINVAL;

    return sol_buffer_set_slice_at(dst, pos, sol_buffer_get_slice(src));
}

/**
 * @brief Set character @c c into @c buf at position @c pos,
 * reallocating if necessary.
 *
 * If pos == buf->end, then the behavior is the same as @ref sol_buffer_insert_char
 *
 * @param buf Destination buffer
 * @param pos Start position
 * @param c Character to be set
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_set_char_at(struct sol_buffer *buf, size_t pos, char c);

/**
 * @brief Insert the @c bytes array into @c buf at position @c pos,
 * reallocating if necessary.
 *
 * If pos == buf->end, then the behavior is the same as @ref sol_buffer_append_bytes.
 *
 * @param buf Destination buffer
 * @param pos Start position
 * @param bytes Bytes to be inserted
 * @param size Number of bytes to insert
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_insert_bytes(struct sol_buffer *buf, size_t pos, const uint8_t *bytes, size_t size);

/**
 * @brief Insert the @c slice into @c buf at position @c pos, reallocating if necessary.
 *
 * If pos == buf->end, then the behavior is the same as @ref sol_buffer_append_slice
 *
 * @param buf Destination buffer
 * @param pos Start position
 * @param slice Source slice
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_insert_slice(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice);

/**
 * @brief Insert the @c dst into @c src at position @c pos, reallocating if necessary.
 *
 * If pos == src->end, then the behavior is the same as @ref sol_buffer_append_buffer
 *
 * @param dst Destination buffer
 * @param pos Start position
 * @param src Source buffer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_buffer_insert_buffer(struct sol_buffer *dst, size_t pos, const struct sol_buffer *src)
{
    if (!dst || !src)
        return -EINVAL;

    return sol_buffer_insert_slice(dst, pos, sol_buffer_get_slice(src));
}

// TODO: move this to some other file? where
/**
 * @brief The default base 64 map to use. The last byte (position 64) is the
 * padding character. This is a NUL terminated string.
 */
extern const char SOL_BASE64_MAP[66];

/**
 * @brief Insert the 'slice' into 'buf' at position 'pos' encoded as base64 using the given map.
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_as_base64().
 * @param slice the byte string to encode, may contain null bytes
 *        @c (\0), it will be encoded up the @c slice.len.
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
int sol_buffer_insert_as_base64(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Append the 'slice' at the end of 'buf' encoded as base64 using the given map.
 *
 * See https://en.wikipedia.org/wiki/Base64
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param slice the byte string to encode, may contain null bytes
 *        @c (\0), it will be encoded up the @c slice.len.
 * @param base64_map the map to use, the default is available as
 *        #SOL_BASE64_MAP. Note that the last char in the map (position 64)
 *        is used as the padding char.
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base64()
 * @see sol_buffer_insert_from_base64()
 * @see sol_buffer_append_from_base64()
 */
int sol_buffer_append_as_base64(struct sol_buffer *buf, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Insert the 'slice' into 'buf' at position 'pos' decoded from base64 using the given map.
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
int sol_buffer_insert_from_base64(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Append the 'slice' at the end of 'buf' decoded from base64 using the given map.
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
int sol_buffer_append_from_base64(struct sol_buffer *buf, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Insert the 'slice' into 'buf' at position 'pos' encoded as base16 (hexadecimal).
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_as_base16().
 * @param slice the byte string to encode, may contain null bytes
 *        @c (\0), it will be encoded up the @c slice.len.
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
 * @brief Append the 'slice' at the end of 'buf' encoded as base16 (hexadecimal).
 *
 * See https://en.wikipedia.org/wiki/Base16
 *
 * @param buf the already-initialized buffer to append the encoded
 *        slice.
 * @param slice the byte string to encode, may contain null bytes
 *        @c (\0), it will be encoded up the @c slice.len.
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
 * @brief Insert the 'slice' into 'buf' at position 'pos' decoded from base16 (hexadecimal).
 *
 * @param buf the already-initialized buffer to append the decoded
 *        slice.
 * @param pos the position in bytes from 0 up to @c buf->used. If pos
 *        == buf->end, then the behavior is the same as
 *        sol_buffer_append_from_base16().
 * @param slice the slice to decode, it must be a set of 0-9 or
 *        letters A-F (if uppercase) or a-f, otherwise decode fails.
 * @param decode_case if SOL_DECODE_UPPERCASE, uppercase letters ABCDEF are
 *        used, if SOL_DECODE_LOWERCASE, lowercase abcdef are used instead.
 *        If SOL_DECODE_BOTH both, lowercase and uppercase, letters can be
 *        used.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base16()
 * @see sol_buffer_append_as_base16()
 * @see sol_buffer_append_from_base16()
 */

int sol_buffer_insert_from_base16(struct sol_buffer *buf, size_t pos, const struct sol_str_slice slice, enum sol_decode_case decode_case);

/**
 * @brief Append the 'slice' at the end of 'buf' decoded from base16 (hexadecimal).
 *
 * See https://en.wikipedia.org/wiki/Base16
 *
 * @param buf the already-initialized buffer to append the decoded
 *        slice.
 * @param slice the slice to decode, it must be a set of 0-9 or
 *        letters A-F (if uppercase) or a-f, otherwise decode fails.
 * @param decode_case if SOL_DECODE_UPPERCASE, uppercase letters ABCDEF are
 *        used, if SOL_DECODE_LOWERCASE, lowercase abcdef are used instead.
 *        If SOL_DECODE_BOTH both, lowercase and uppercase, letters can be
 *        used.
 * @return 0 on success, -errno on failure.
 *
 * @see sol_buffer_insert_as_base16()
 * @see sol_buffer_append_as_base16()
 * @see sol_buffer_insert_from_base16()
 */
int sol_buffer_append_from_base16(struct sol_buffer *buf, const struct sol_str_slice slice, enum sol_decode_case decode_case);

/**
 * @brief Append the formatted string in the end of the buffer (including trailing '\0').
 *
 * Similar to @ref sol_buffer_insert_printf, but receives @c va_list instead.
 *
 * @param buf The buffer
 * @param fmt A standard 'printf()' format string
 * @param args va_list to 'vprintf()'
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_append_vprintf(struct sol_buffer *buf, const char *fmt, va_list args) SOL_ATTR_PRINTF(2, 0);

/**
 * @brief Append the formatted string in the end of the buffer (including trailing '\0').
 *
 * @param buf The buffer
 * @param fmt A standard 'printf()' format string
 * @param ... The arguments to 'printf()'
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
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
 * @brief Insert the formatted string in the given position in the buffer.
 *
 * Similar to @ref sol_buffer_insert_printf, but receives @c va_list instead.
 *
 * If position == buf->pos, then the behavior is the same as
 * @ref sol_buffer_append_vprintf.
 *
 * @param buf The buffer
 * @param pos Position to start write the string
 * @param fmt A standard 'printf()' format string
 * @param args va_list to 'vprintf()'
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_buffer_insert_vprintf(struct sol_buffer *buf, size_t pos, const char *fmt, va_list args) SOL_ATTR_PRINTF(3, 0);

/**
 * @brief Insert the formatted string in the given position in the buffer.
 *
 * @param buf The buffer
 * @param pos Position to start write the string
 * @param fmt A standard 'printf()' format string
 * @param ... The arguments to 'printf()'
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
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

/**
 * @brief Frees memory that is not in being used by the buffer.
 *
 * If buffer has null-bytes (ie: null terminated strings), then the
 * resized amount will include that null byte automatically. See
 * SOL_BUFFER_FLAGS_NO_NUL_BYTE.
 *
 * @param buf The buffer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_buffer_trim(struct sol_buffer *buf)
{
    size_t new_size;

    if (!buf)
        return -EINVAL;

    if (buf->flags & SOL_BUFFER_FLAGS_NO_NUL_BYTE)
        new_size = buf->used;
    else if (buf->used == SIZE_MAX)
        return -EOVERFLOW;
    else
        new_size = buf->used + 1;

    if (new_size == buf->capacity)
        return 0;

    return sol_buffer_resize(buf, new_size);
}

/**
 *  @brief 'Steals' sol_buffer internal buffer and resets sol_buffer.
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
 *
 *  @note If the buffer flags are set to @c SOL_BUFFER_FLAGS_NO_FREE,
 *  this function will return NULL.
 */
void *sol_buffer_steal(struct sol_buffer *buf, size_t *size);

/**
 *  @brief 'Steals' @c buf internal buffer and resets it.
 *
 *  If the @c SOL_BUFFER_FLAGS_NO_FREE was set,
 *  it will return a copy of the buffer's data.
 *
 *  After this call, user is responsible for the memory returned.
 *
 *  @param buf buffer to have it's internal buffer stolen or copied
 *  @param size if not NULL, will store memory returned size
 *
 *  @return @a buffer internal buffer. It's caller responsibility now
 *  to free this memory
 *
 *  @note If @a buffer was allocated with @c sol_buffer_new(), it still
 *  needs to be freed by calling @c sol_buffer_free();
 *  @see sol_buffer_steal
 */
void *sol_buffer_steal_or_copy(struct sol_buffer *buf, size_t *size);

/**
 *  @brief Allocate a new sol_buffer and a new data block and copy the
 *  contents of the provided sol_buffer.
 *
 *  After this call, user is responsible for calling fini on the
 *  buffer and freeing it afterwards. For it's memory to be freed
 *  properly, the flag SOL_BUFFER_FLAGS_NO_FREE will always be
 *  unset, despite the original buffer.
 *
 *  @param buf Buffer to be copied
 *
 *  @return A copy of @c buf, @c NULL on error.
 */
struct sol_buffer *sol_buffer_copy(const struct sol_buffer *buf);

/**
 * @brief Reset the buffer content.
 *
 * All allocated memory is kept.
 *
 * @param buf The buffer
 */
static inline void
sol_buffer_reset(struct sol_buffer *buf)
{
    buf->used = 0;
}

/**
 * @brief Creates a new buffer.
 *
 * @return Pointer to the new buffer
 */
static inline struct sol_buffer *
sol_buffer_new(void)
{
    struct sol_buffer *buf = (struct sol_buffer *)calloc(1, sizeof(struct sol_buffer));

    if (!buf) return NULL;

    sol_buffer_init(buf);

    return buf;
}

/**
 * @brief Delete the buffer.
 *
 * Buffer is finalized and all memory is freed.
 *
 * @param buf Buffer to be deleted
 */
static inline void
sol_buffer_free(struct sol_buffer *buf)
{
    sol_buffer_fini(buf);
    free(buf);
}

/**
 * @brief Ensures that buffer has a terminating NULL byte.
 *
 * If flag SOL_BUFFER_FLAGS_NO_NUL_BYTE is not set.
 *
 * @return a negative number in case it was not possible to
 * ensure a terminating NULL byte - if flag SOL_BUFFER_FLAGS_NO_NUL_BYTE
 * is set for instance, or if it could not resize the buffer
 */
int sol_buffer_ensure_nul_byte(struct sol_buffer *buf);


/**
 * @brief Removes part of data inside the buffer rearranging the memory properly.
 *
 * It's removed up to the buffer's size in case of @c size greater than used data.
 *
 * @param buf The buffer (already-initialized)
 * @param offset Position (from begin of the buffer) where
 * @param size Amount of data (in bytes) that should be removed
 * @c size bytes will be removed
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @note the buffer keeps its capacity after this function, it means,
 * the data is not released. If that is wanted, one should call @ref sol_buffer_trim
 */
int sol_buffer_remove_data(struct sol_buffer *buf, size_t offset, size_t size);


/**
 * @brief Convert a buffer to a struct @ref sol_blob
 *
 * The buffer will be stolen by the created blob.
 *
 * @param buf The buf to be transformed in a blob
 * @return a blob or NULL on error
 */
struct sol_blob *sol_buffer_to_blob(struct sol_buffer *buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
