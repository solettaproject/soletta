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

#include <sol-buffer.h>
#include <sol-str-slice.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Useful general routines.
 */

/**
 * @defgroup Utils Soletta utility functions.
 *
 * @brief Contains helpers to manipulate time, error code/string,
 * overflows, encode/decode data and some converters.
 *
 * @{
 */

/**
 * @brief Calculates the number of elements in an array
 */
#define sol_util_array_size(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * @brief number of nanoseconds in a second: 1,000,000,000.
 */
#define SOL_UTIL_NSEC_PER_SEC 1000000000ULL

/**
 * @brief number of milliseconds in a second: 1,000.
 */
#define SOL_UTIL_MSEC_PER_SEC 1000ULL

/**
 * @brief number of microseconds in a second: 1,000,000.
 */
#define SOL_UTIL_USEC_PER_SEC 1000000ULL

/**
 * @brief number of nanoseconds in a milliseconds: 1,000,000,000 / 1,000 = 1,000,000.
 */
#define SOL_UTIL_NSEC_PER_MSEC 1000000ULL

/**
 * @brief  number of nanoseconds in a microsecond: 1,000,000,000 / 1,000,000 = 1,000.
 */
#define SOL_UTIL_NSEC_PER_USEC 1000ULL

/**
 * @brief Gets the minimum value
 *
 * This will generate shadowing warnings if the same kind
 * is nested, but the warnings on type mismatch are a fair benefit.
 *
 * Use temporary variables to address nested call cases.
 */
#define sol_util_min(x, y) \
    ({ \
        __typeof__(x)_min1 = (x); \
        __typeof__(y)_min2 = (y); \
        (void)(&_min1 == &_min2); \
        _min1 < _min2 ? _min1 : _min2; \
    })

/**
 * @brief Gets the maximum value
 *
 * This will generate shadowing warnings if the same kind
 * is nested, but the warnings on type mismatch are a fair benefit.
 *
 * Use temporary variables to address nested call cases.
 */
#define sol_util_max(x, y) \
    ({ \
        __typeof__(x)_max1 = (x); \
        __typeof__(y)_max2 = (y); \
        (void)(&_max1 == &_max2); \
        _max1 > _max2 ? _max1 : _max2; \
    })

/**
 * @brief Gets the current time (Monotonic).
 *
 * @return The current time represented in @c struct timespec.
 */
struct timespec sol_util_timespec_get_current(void);

/**
 * @brief Gets the current time (System-wide clock).
 *
 * @param t Variable used to store the time.
 *
 * @return 0 for success, or -1 for failure (in which case errno is
       set appropriately).
 */
int sol_util_timespec_get_realtime(struct timespec *t);

/**
 * @brief Sum two time values.
 *
 * @param t1 First time value used on operation.
 * @param t2 Second time value used on operation.
 * @param result Variable used to store the sum's result.
 */
static inline void
sol_util_timespec_add(const struct timespec *t1, const struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec + t2->tv_nsec;
    result->tv_sec = t1->tv_sec + t2->tv_sec;
    if ((unsigned long long)result->tv_nsec >= SOL_UTIL_NSEC_PER_SEC) {
        result->tv_nsec -= SOL_UTIL_NSEC_PER_SEC;
        result->tv_sec++;
    }
}

/**
 * @brief Subtracts two time values.
 *
 * @param t1 First time value used on operation.
 * @param t2 Second time value used on operation.
 * @param result Variable used to store the subtraction's result.
 */
static inline void
sol_util_timespec_sub(const struct timespec *t1, const struct timespec *t2, struct timespec *result)
{
    result->tv_nsec = t1->tv_nsec - t2->tv_nsec;
    result->tv_sec = t1->tv_sec - t2->tv_sec;
    if (result->tv_nsec < 0) {
        result->tv_nsec += SOL_UTIL_NSEC_PER_SEC;
        result->tv_sec--;
    }
}

/**
 * @brief Compare two time values.
 *
 * Function to compare two times. It returns an integer less than,
 * equal to, or greater than zero.
 *
 * @param t1 First time value used on operation.
 * @param t2 Second time value used on operation.
 *
 * @return 0 if equal, -1 if t2 is greater or 1 otherwise.
 */
static inline int
sol_util_timespec_compare(const struct timespec *t1, const struct timespec *t2)
{
    int retval = (t1->tv_sec > t2->tv_sec) - (t1->tv_sec < t2->tv_sec);

    if (retval != 0)
        return retval;
    return (t1->tv_nsec > t2->tv_nsec) - (t1->tv_nsec < t2->tv_nsec);
}

/**
 * @brief Create a @c struct timespec from milliseconds.
 *
 * @param msec The number of milliseconds.
 *
 * @return a @c struct timespec representing @c msec milliseconds.
 */
static inline struct timespec
sol_util_timespec_from_msec(int msec)
{
    struct timespec ts;

    ts.tv_sec = msec / SOL_UTIL_MSEC_PER_SEC;
    ts.tv_nsec = (msec % SOL_UTIL_MSEC_PER_SEC) * SOL_UTIL_NSEC_PER_MSEC;
    return ts;
}

/**
 * @brief Create a @c struct timespec from microseconds.
 *
 * @param usec The number of microseconds.
 *
 * @return a @c struct timespec representing @c usec microseconds.
 */
static inline struct timespec
sol_util_timespec_from_usec(int usec)
{
    struct timespec ts;

    ts.tv_sec = usec / SOL_UTIL_USEC_PER_SEC;
    ts.tv_nsec = (usec % SOL_UTIL_USEC_PER_SEC) * SOL_UTIL_NSEC_PER_USEC;
    return ts;
}

/**
 * @brief Gets the number of milliseconds for given time.
 *
 * @param ts The struct timespec to get the milliseconds.
 *
 * @return the number of milliseconds on @c ts.
 */
static inline int
sol_util_msec_from_timespec(const struct timespec *ts)
{
    return ts->tv_sec * SOL_UTIL_MSEC_PER_SEC + ts->tv_nsec / SOL_UTIL_NSEC_PER_MSEC;
}

/**
 * @brief Gets the number of microseconds for given time.
 *
 * @param ts The struct timespec to get the microseconds.
 *
 * @return the number of microseconds on @c ts.
 */
static inline int
sol_util_usec_from_timespec(const struct timespec *ts)
{
    return ts->tv_sec * SOL_UTIL_USEC_PER_SEC + ts->tv_nsec / SOL_UTIL_NSEC_PER_USEC;
}

/**
 * @brief Gets a string from a given error.
 *
 * The function returns a pointer to a string that describes the error
 * code passed in the argument @c errnum.
 *
 * @param errnum The error code
 * @param buf Buffer used to append error the string - It must be already initialized.
 *
 * @return return the appropriate error description string.
 *
 * @see sol_util_strerrora
 */
char *sol_util_strerror(int errnum, struct sol_buffer *buf);

/**
 * @brief Gets a string from a given error using the stack.
 *
 * The function returns a pointer to a string (created using the
 * stack) that describes the error code passed in the argument @c
 * errnum.
 *
 * @param errnum The error code
 *
 * @return the appropriate error description string.
 *
 * @see sol_util_strerror
 */
#define sol_util_strerrora(errnum) \
    ({ \
        SOL_BUFFER_DECLARE_STATIC(buf ## __COUNT__, 512); \
        sol_util_strerror((errnum), &buf ## __COUNT__); \
    })

/**
 * @brief Multiply two values checking for overflow.
 *
 * This function multiply two variables of the type @c ssize_t and check
 * if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_size_mul
 */
int sol_util_ssize_mul(ssize_t op1, ssize_t op2, ssize_t *out);

/**
 * @brief Multiply two values checking for overflow.
 *
 * This function multiply two variables of the type @c size_t and check
 * if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the time.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_ssize_mul
 */
int sol_util_size_mul(size_t op1, size_t op2, size_t *out);

/**
 * @brief Add two values checking for overflow.
 *
 * This function adds two variables of the type @c size_t and check
 * if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_size_sub
 */
int sol_util_size_add(size_t op1, size_t op2, size_t *out);

/**
 * @brief Subtract two values checking for overflow.
 *
 * This function subtracts two variables of the type @c size_t and check
 * if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_size_add
 */
int sol_util_size_sub(size_t op1, size_t op2, size_t *out);

/**
 * @brief Multiply two values checking for overflow.
 *
 * This function multiply two unsigned variables of the type @c
 * uint64_t and check if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_int64_mul
 */
int sol_util_uint64_mul(uint64_t op1, uint64_t op2, uint64_t *out);

/**
 * @brief Multiply two values checking for overflow.
 *
 * This function multiply two variables of the type @c int64_t and
 * check if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_uint64_mul
 */
int sol_util_int64_mul(int64_t op1, int64_t op2, int64_t *out);

/**
 * @brief Add two values checking for overflow.
 *
 * This function add two variables of the type @c uint64_t and
 * check if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_util_size_add
 */
int sol_util_uint64_add(uint64_t op1, uint64_t op2, uint64_t *out);

/**
 * @brief Multiply two values checking for overflow.
 *
 * This function multiplies two variables of the type @c int32_t and
 * check if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_util_int32_mul(int32_t op1, int32_t op2, int32_t *out);

/**
 * @brief Multiply two values checking for overflow.
 *
 * This function multiplies two variables of the type @c int64_t and
 * check if this operation causes an overflow.
 *
 * @param op1 First operation's operator.
 * @param op2 Second operation's operator.
 * @param out Variable used to store the result.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_util_uint32_mul(uint32_t op1, uint32_t op2, uint32_t *out);

/**
 * @brief Generates a new universally unique identifier (UUID) string.
 *
 * The generated string is 16 bytes-long (128 bits) long and conforms to v4
 * UUIDs (generated from random—or pseudo-random—numbers).
 *
 * @param uppercase Whether to generate the UUID in uppercase or not
 * @param with_hyphens Format the resulting UUID string with hyphens
 *                     (e.g. "de305d54-75b4-431b-adb2-eb6b9e546014") or
 *                     without them.
 * @param uuid_buf An initialized buffer to be used to append the generated id.
 *        It will have 36 bytes of length if with_hyphens is true or 32 bytes
 *        of length if with_hyphens is false.
 *
 * @return 0 on success, negative error code otherwise.
 */
int sol_util_uuid_gen(bool uppercase, bool with_hyphens, struct sol_buffer *uuid_buf);

/**
 * @brief Checks if a given universally unique identifier (UUID), in string
 * form, is valid.
 *
 * All upcase/downcase, hyphenated/non-hyphenated cases are included.
 *
 * @param uuid The given UUID formatted in a string, with or without hyphens.
 *
 * @return @c true if it's valid, @c false otherwise.
 */
bool sol_util_uuid_str_is_valid(const struct sol_str_slice uuid);

/**
 * @brief Convert a UUID in byte format to UUID string format.
 *
 * @param uppercase Whether to create the UUID string in uppercase or not
 * @param with_hyphens Format the resulting UUID string with hyphens
 *                     (e.g. "de305d54-75b4-431b-adb2-eb6b9e546014") or
 *                     without them.
 * @param uuid_bytes A 16 byte array containing the UUID in byte format.
 * @param uuid_str n initialized buffer to be used to append the converted uuid.
 *        It will have 36 bytes of length if with_hyphens is true or 32 bytes
 *        of length if with_hyphens is false.
 *
 * @return 0 on success, negative error code otherwise.
 */
int sol_util_uuid_string_from_bytes(bool uppercase, bool with_hyphens, const uint8_t uuid_bytes[SOL_STATIC_ARRAY_SIZE(16)], struct sol_buffer *uuid_str);

/**
 * @brief Convert a UUID in string format to a byte array with UUID bytes.
 *
 * @param uuid_str The UUID in string format, with or without hyphens, using
 *        lowercase or uppercase characters.
 * @param uuid_bytes an initialized buffer to be used to append the converted uuid,
 *        totalizing 16 bytes.
 *
 * @return 0 on success, negative error code otherwise.
 */
int sol_util_uuid_bytes_from_string(struct sol_str_slice uuid_str, struct sol_buffer *uuid_bytes);

/**
 * @brief Restricts a number between two other numbers.
 *
 * @param start Minimum value.
 * @param end Maximum value.
 * @param value The value to clamp.
 *
 * @return @c value if living in the range imposed by the @c start and
 * @c end, the lower value if initially lower than @c start, or the higher
 * one if initially higher than @c end.
 */
static inline int32_t
sol_util_int32_clamp(int32_t start, int32_t end, int32_t value)
{
    if (value < start)
        return start;
    if (value > end)
        return end;
    return value;
}

/**
 * @brief Replace the string's contents.
 *
 * This function takes a string and replace its contents if different
 * from the new given string, otherwise it lets the string intact.
 *
 * @note If the string is replaced its memory is properly released.
 *
 * @param str The pointer for string which will be changed.
 * @param new_str The new string
 *
 * @return @c 1 if changed, 0 if unchanged, error code (always negative) otherwise.
 *
 * @see util_replace_str_from_slice_if_changed
 */
int sol_util_replace_str_if_changed(char **str, const char *new_str);

/**
 * @brief Replace the string's contents.
 *
 * This function takes a string and replace its contents if different
 * from the new given slice, otherwise it lets the string intact.
 *
 * @note If the string is replaced its memory is properly released.
 *
 * @param str The pointer for string which will be changed.
 * @param slice The slice with string
 *
 * @return @c 1 if changed, 0 if unchanged, error code (always negative) otherwise.
 *
 * @see util_replace_str_from_slice_if_changed
 */
int sol_util_replace_str_from_slice_if_changed(char **str, const struct sol_str_slice slice);

/**
 * Encode the binary slice to base64 using the given map.
 *
 * https://en.wikipedia.org/wiki/Base64
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the encoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the encoded slice, that is:
 *        (slice.len / 3 + 1) * 4
 * @param slice the slice to encode, it may contain null-bytes (\0),
 *        the whole size of the slice will be used (slice.len).
 * @param base64_map the map to use. The last char is used as the
 *        padding character if slice length is not multiple of 3 bytes.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base64_encode(void *buf, size_t buflen, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Decode the binary slice from base64 using the given map.
 *
 * https://en.wikipedia.org/wiki/Base64
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the decoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the decoded slice, that is:
 *        (slice.len / 4) * 3
 * @param slice the slice to decode, it must be composed solely of the
 *        base64_map characters or it will fail.
 * @param base64_map the map to use. The last char is used as the
 *        padding character if slice length is not multiple of 3 bytes.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base64_decode(void *buf, size_t buflen, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Calculate the size necessary to encode a given slice in base64.
 *
 * @param slice The slice that is wanted to know the encoded size.
 * @param base64_map the map to use. The last char is used as the
 *        padding character if slice length is not multiple of 3 bytes.
 *
 * @return the size that will be utilized to encode the @c slice or a
 * negative number on error.
 */
static inline ssize_t
sol_util_base64_calculate_encoded_len(const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)])
{
    ssize_t req_len = slice.len / 3;
    int err;

    if (slice.len % 3 != 0)
        req_len++;
    err = sol_util_ssize_mul(req_len, 4, &req_len);
    if (err < 0)
        return err;
    return req_len;
}

/**
 * @brief Calculate the size necessary to decode a given slice in base64.
 *
 * @param slice The slice that is wanted to know the decode size.
 * @param base64_map the map to use. The last char is used as the
 *        padding character if slice length is not multiple of 3 bytes.
 *
 * @return the size that will be utilized to decode the @c slice or a
 * negative number on error.
 */
ssize_t
sol_util_base64_calculate_decoded_len(const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)]);

/**
 * @brief Encode the binary slice to base16 (hexadecimal).
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the encoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the encoded slice, that is:
 *        slice.len * 2
 * @param slice the slice to encode, it may contain null-bytes (\0),
 *        the whole size of the slice will be used (slice.len).
 * @param uppercase if true, uppercase letters ABCDEF are used, otherwise
 *        lowercase abcdef are used instead.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base16_encode(void *buf, size_t buflen, const struct sol_str_slice slice, bool uppercase);

/**
 * @brief Decode the binary slice from base16 (hexadecimal).
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf a buffer of size @a buflen that is big enough to hold
 *        the decoded string.
 * @param buflen the number of bytes available in buffer. Must be
 *        large enough to contain the decoded slice, that is:
 *        slice.len / 2.
 * @param slice the slice to decode, it must be a set of 0-9 or
 *        letters A-F (if uppercase) or a-f, otherwise decode fails.
 * @param decode_case if SOL_DECODE_UPPERCASE, uppercase letters ABCDEF are
 *        used, if SOL_DECODE_LOWERCASE, lowercase abcdef are used instead.
 *        If SOL_DECODE_BOTH both, lowercase and uppercase, letters can be
 *        used.
 *
 * @return the number of bytes written or -errno if failed.
 */
ssize_t sol_util_base16_decode(void *buf, size_t buflen, const struct sol_str_slice slice, enum sol_decode_case decode_case);

/**
 * @brief Convert from unicode code to utf-8 string.
 *
 * Write at string buf the bytes needed to represent the unicode charater
 * informed as utf-8. One to four characters will be written on success. No
 * character is written on error.
 *
 * @note @b no trailing null '\0' is added!
 *
 * @param buf Buffer to write the utf-8 representation of the unicode character
 * @param buf_len The buffer length
 * @param unicode_code Code from unicode table of the character to be converted
 *
 * @return The number of bytes written in 'buf' or a negative number on error.
 */
int8_t sol_util_utf8_from_unicode_code(uint8_t *buf, size_t buf_len, uint32_t unicode_code);

/**
 * @brief Convert a utf-8 character to unicode code.
 *
 * @param buf Buffer with the utf-8 representation of the unicode character.
 * @param buf_len The buffer length.
 * @param bytes_read Optional pointer to variable to write number of bytes read
 * from buf.
 *
 * @return The code from unicode table of the character in 'buf' or a negative number on error.
 */
int32_t sol_util_unicode_code_from_utf8(const uint8_t *buf, size_t buf_len, uint8_t *bytes_read);

/**
 * @brief Calculate the size necessary to encode a given slice in base16.
 *
 * @param slice The slice that is wanted to know the encoded size.
 *
 * @return the size that will be utilized to encode the @c slice or a
 * negative number on error.
 */
static inline ssize_t
sol_util_base16_calculate_encoded_len(const struct sol_str_slice slice)
{
    ssize_t req_len;
    int err = sol_util_ssize_mul(slice.len, 2, &req_len);

    if (err < 0)
        return err;
    return req_len;
}

/**
 * @brief Calculate the size necessary to decode a given slice in base16.
 *
 * @param slice The slice that is wanted to know the decoded size.
 *
 * @return the size that will be utilized to decode the @c slice or a
 * negative number on error.
 */
static inline ssize_t
sol_util_base16_calculate_decoded_len(const struct sol_str_slice slice)
{
    return slice.len / 2;
}

/**
 * @brief Clear an allocated memory securely.
 *
 * Clobber memory pointed to by @c buf to prevent the optimizer from
 * eliding the @c memset() call.
 *
 * @param buf The memory block
 * @param len The buf length
 */
static inline void
sol_util_clear_memory_secure(void *buf, size_t len)
{
    memset(buf, 0, len);

    /* Clobber memory pointed to by `buf` to prevent the optimizer from
     * eliding the memset() call.  */
    __asm__ __volatile__ ("" : : "g" (buf) : "memory");
}

/**
 * @brief Wrapper over strtod() that consumes up to @c len bytes and may not
 * use a locale.
 *
 * This variation of strtod() will work with buffers that are not
 * null-terminated.
 *
 * It also offers a way to skip the currently set locale, forcing
 * plain "C". This is required to parse numbers in formats that
 * require '.' as the decimal point while the current locale may use
 * ',' such as in pt_BR.
 *
 * All the formats accepted by strtod() are accepted and the behavior
 * should be the same, including using information from @c LC_NUMERIC
 * if locale is configured and @a use_locale is true.
 *
 * @param nptr the string containing the number to convert.
 * @param endptr if non-NULL, it will contain the last character used
 *        in the conversion. If no conversion was done, endptr is @a nptr.
 * @param use_locale if true, then current locale is used, if false
 *        then "C" locale is forced.
 *
 * @param len use at most this amount of bytes of @a nptr. If -1, assumes
 *        nptr has a trailing NUL and calculate the string length.
 *
 * @return the converted value, if any. The converted value may be @c
 *         NAN, @c INF (positive or negative). See the strtod(3)
 *         documentation for the details.
 */
double sol_util_strtod_n(const char *nptr, char **endptr, ssize_t len, bool use_locale);

/**
 * @brief Wrapper over strtol() that consumes up to @c len bytes
 *
 * This variation of strtol() will work with buffers that are not
 * null-terminated.
 *
 * All the formats accepted by strtol() are accepted and the behavior
 * should be the same.
 *
 * @param nptr the string containing the number to convert.
 *
 * @param endptr if non-NULL, it will contain the last character used
 *        in the conversion. If no conversion was done, endptr is @a nptr.
 *
 * @param len use at most this amount of bytes of @a nptr. If -1, assumes
 *        nptr has a trailing NUL and calculate the string length.
 *
 * @param base it's the base of conversion, which must be between 2
 *        and 36 inclusive, or be the special value 0.
 *        A zero base is taken as 10 (decimal) unless the next character
 *        is '0', in which case it  is  taken as 8 (octal).
 *
 * @return the converted value, if any.
 */
long int sol_util_strtol_n(const char *nptr, char **endptr, ssize_t len, int base);

/**
 * @brief Wrapper over strtoul() that consumes up to @c len bytes
 *
 * This variation of strtoul() will work with buffers that are not
 * null-terminated.
 *
 * All the formats accepted by strtoul() are accepted and the behavior
 * should be the same.
 *
 * @param nptr the string containing the number to convert.
 *
 * @param endptr if non-NULL, it will contain the last character used
 *        in the conversion. If no conversion was done, endptr is @a nptr.
 *
 * @param len use at most this amount of bytes of @a nptr. If -1, assumes
 *        nptr has a trailing NUL and calculate the string length.
 *
 * @param base it's the base of conversion, which must be between 2
 *        and 36 inclusive, or be the special value 0.
 *        A zero base is taken as 10 (decimal) unless the next character
 *        is '0', in which case it  is  taken as 8 (octal).
 *
 * @return the converted value, if any.
 */
unsigned long int sol_util_strtoul_n(const char *nptr, char **endptr, ssize_t len, int base);

/**
 * @brief Swaps the bytes of a 16 bytes unsigned int
 */
#define sol_util_uint16_bytes_swap(val) \
    ((uint16_t)((((val) >> 8) & 0xff) | (((val) & 0xff) << 8)))

/**
 * @brief Convert a 16 bytes integer to big endian format
 *
 * This function converts a integer of 16 bytes to big endian format, in case of
 * the integer being in big endian format nothing is done.
 *
 * @param val the uint16_t number to convert.
 *
 * @return the given value on big endian format.
 *
 * @see sol_util_cpu_to_le16
 */
static inline uint16_t
sol_util_cpu_to_be16(uint16_t val)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return sol_util_uint16_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 16 bytes integer to little endian format
 *
 * This function converts a integer of 16 bytes to little endian format, in case of
 * the integer being in little endian format nothing is done.
 *
 * @param val the uint16_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_cpu_to_be16
 */
static inline uint16_t
sol_util_cpu_to_le16(uint16_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return sol_util_uint16_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 16 bytes big endian integer to cpu endianness.
 *
 * This function converts a integer of 16 bytes to little endian format, in case of
 * the integer being in the cpu endiannesst nothing is done.
 *
 * @param val the uint16_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_le16_to_cpu
 */
static inline uint16_t
sol_util_be16_to_cpu(uint16_t val)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return sol_util_uint16_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 16 bytes little endian integer to cpu endianness.
 *
 * This function converts a integer of 16 bytes to little endian format, in case of
 * the integer being in the cpu endianness nothing is done.
 *
 * @param val the uint16_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_be16_to_cpu
 */
static inline uint16_t
sol_util_le16_to_cpu(uint16_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return sol_util_uint16_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Swaps the bytes of a 32 bytes unsigned int
 */
#define sol_util_uint32_bytes_swap(val) \
    ((uint32_t)((((val) & 0xff000000) >> 24) | (((val) & 0x00ff0000) >>  8) | \
    (((val) & 0x0000ff00) <<  8) | (((val) & 0x000000ff) << 24)))

/**
 * @brief Convert a 32 bytes integer to big endian format
 *
 * This function converts a integer of 32 bytes to big endian format, in case of
 * the integer being in big endian format nothing is done.
 *
 * @param val the uint32_t number to convert.
 *
 * @return the given value on big endian format.
 *
 * @see sol_util_cpu_to_le32
 */
static inline uint32_t
sol_util_cpu_to_be32(uint32_t val)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return sol_util_uint32_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 32 bytes integer to little endian format
 *
 * This function converts a integer of 32 bytes to little endian format, in case of
 * the integer being in little endian format nothing is done.
 *
 * @param val the uint32_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_cpu_to_be32
 */
static inline uint32_t
sol_util_cpu_to_le32(uint32_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return sol_util_uint32_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 32 bytes big endian integer to cpu endianness.
 *
 * This function converts a integer of 32 bytes to little endian format, in case of
 * the integer being in the cpu endiannesst nothing is done.
 *
 * @param val the uint32_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_le32_to_cpu
 */
static inline uint32_t
sol_util_be32_to_cpu(uint32_t val)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return sol_util_uint32_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 32 bytes little endian integer to cpu endianness.
 *
 * This function converts a integer of 32 bytes to little endian format, in case of
 * the integer being in the cpu endianness nothing is done.
 *
 * @param val the uint32_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_be32_to_cpu
 */
static inline uint32_t
sol_util_le32_to_cpu(uint32_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return sol_util_uint32_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Swaps the bytes of a 32 bytes unsigned int
 */
#define sol_util_uint64_bytes_swap(val) \
    ((uint64_t)((((val) & 0xff00000000000000ull) >> 56) \
    | (((val) & 0x00ff000000000000ull) >> 40) | (((val) & 0x0000ff0000000000ull) >> 24) \
    | (((val) & 0x000000ff00000000ull) >> 8) | (((val) & 0x00000000ff000000ull) << 8) \
    | (((val) & 0x0000000000ff0000ull) << 24) | (((val) & 0x000000000000ff00ull) << 40) \
    | (((val) & 0x00000000000000ffull) << 56)))

/**
 * @brief Convert a 64 bytes integer to big endian format
 *
 * This function converts a integer of 64 bytes to big endian format, in case of
 * the integer being in big endian format nothing is done.
 *
 * @param val the uint64_t number to convert.
 *
 * @return the given value on big endian format.
 *
 * @see sol_util_cpu_to_le64
 */
static inline uint64_t
sol_util_cpu_to_be64(uint64_t val)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return sol_util_uint64_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 64 bytes integer to little endian format
 *
 * This function converts a integer of 64 bytes to little endian format, in case of
 * the integer being in little endian format nothing is done.
 *
 * @param val the uint64_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_cpu_to_be64
 */
static inline uint64_t
sol_util_cpu_to_le64(uint64_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return sol_util_uint64_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 64 bytes big endian integer to cpu endianness.
 *
 * This function converts a integer of 64 bytes to little endian format, in case of
 * the integer being in the cpu endiannesst nothing is done.
 *
 * @param val the uint64_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_le64_to_cpu
 */
static inline uint64_t
sol_util_be64_to_cpu(uint64_t val)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return sol_util_uint64_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Convert a 64 bytes little endian integer to cpu endianness.
 *
 * This function converts a integer of 64 bytes to little endian format, in case of
 * the integer being in the cpu endianness nothing is done.
 *
 * @param val the uint64_t number to convert.
 *
 * @return the given value on little endian format.
 *
 * @see sol_util_be64_to_cpu
 */
static inline uint64_t
sol_util_le64_to_cpu(uint64_t val)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return sol_util_uint64_bytes_swap(val);
#else
    return val;
#endif
}

/**
 * @brief Unescape a string removing quotes from it.
 *
 * This function will unescape single quotes (\') and double quotes (\") from the slice,
 * it will also remove single and double quotes from the string if they are not escaped.
 * Trying to unescape a character that is not a single quote or double quote it is also
 * considered an error.
 *
 * @param slice The slice to be escaped
 * @param buf - The buffer to hold the unescaped string - It will be initialized by this function.
 * @return 0 on success, negative errno otherwise.
 */
int sol_util_unescape_quotes(const struct sol_str_slice slice, struct sol_buffer *buf);


/**
 * @brief Wrapper around strftime()/strftime_l()
 *
 * This is a simple wrapper around strftime()/strftime_l() functions. The
 * @c use_locale parameter is only considered if strftime_l() and newlocale() are
 * available, otherwise this wrapper will fallback to strftime() - Thus current system's
 * locale will be considered when formatting the time.
 *
 * @param buf The buffer to append the formatted time to - It must be already initialzed.
 * @param format The date format - check strftime man page for accepted formats.
 * @param timeptr The broken down time struct.
 * @param use_locale true to use current system locale or false to do not use system's locale.
 * @return The number of bytes written, negative errno or 0 on error. (see strftime man page).
 */
ssize_t sol_util_strftime(struct sol_buffer *buf, const char *format, const struct tm *timeptr, bool use_locale) SOL_ATTR_STRFTIME(2);

/**
 * @brief Checks @c var0 and @c var1 for equality.
 *
 * It uses relative comparison to account for impressions caused by floating point arithmetics,
 * so give preference to use this function instead of comparing the numbers directly.
 *
 * @param var0 First argument
 * @param var1 Second argument
 *
 * @return @c true if both values are equal, @c false otherwise.
 */
bool sol_util_double_eq(double var0, double var1);

/**
 * @brief Duplicate memory.
 *
 * It's a helper function to allocated memory and copy data, returning
 * the pointer to it.
 *
 * @param data Pointer to the memory to be copied
 * @param len Size in bytes of the memory to be allocated
 *
 * @return A valid pointer to the allocated memory, with copied data or @c NULL
 * if it fails to allocate it. The pointer need to be freed after usage.
 */
void *sol_util_memdup(const void *data, size_t len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
