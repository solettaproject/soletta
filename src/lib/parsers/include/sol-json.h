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
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sol-buffer.h>
#include <sol-macros.h>
#include <sol-str-slice.h>
#include <sol-buffer.h>
#include <sol-memdesc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are the parser routines that Soletta provides.
 */

/**
 * @defgroup Parsers Parsers
 *
 * @brief Parsers provided by Soletta.
 *
 */

/**
 * @defgroup JSON JSON
 * @ingroup Parsers
 *
 * @brief JSON parser.
 *
 * @{
 */

/**
 * @brief Structure to track a JSON document (or a portion of it) being parsed.
 */
typedef struct sol_json_scanner {
    const char *mem; /**< @brief Start of this portion of the JSON document. */
    const char *mem_end; /**< @brief End of this portion of the JSON document. */
    const char *current; /**< @brief Current point in the JSON document that needs to be processed. */
} sol_json_scanner;

/**
 * @brief Type describing a JSON token
 *
 * Used to point and delimit JSON element while parsing a JSON document.
 */
typedef struct sol_json_token {
    const char *start; /**< @brief Token start */
    const char *end; /**< @brief Token end. Non-inclusive */
} sol_json_token;

/**
 * @brief Token type enumeration.
 */
enum sol_json_type {
    SOL_JSON_TYPE_UNKNOWN = 0, /**< @brief Unknown token */
    SOL_JSON_TYPE_OBJECT_START = '{', /**< @brief JSON Object start */
    SOL_JSON_TYPE_OBJECT_END = '}', /**< @brief JSON Object end */
    SOL_JSON_TYPE_ARRAY_START = '[', /**< @brief JSON Array start */
    SOL_JSON_TYPE_ARRAY_END = ']', /**< @brief JSON Array end */
    SOL_JSON_TYPE_ELEMENT_SEP = ',', /**< @brief JSON Element separator */
    SOL_JSON_TYPE_PAIR_SEP = ':', /**< @brief JSON Pair separator */
    SOL_JSON_TYPE_TRUE = 't', /**< @brief 'true' value */
    SOL_JSON_TYPE_FALSE = 'f', /**< @brief 'false' value */
    SOL_JSON_TYPE_NULL = 'n', /**< @brief 'null' value */
    SOL_JSON_TYPE_STRING = '"', /**< @brief JSON string token */
    SOL_JSON_TYPE_NUMBER = '1', /**< @brief JSON number token */
};

/**
 * @brief Return values used by the parser 'loop' macros.
 *
 * Used to inform if the macro successfully parsed the provided content or not.
 * @c Invalid may be returned when an invalid JSON construct is found, but also
 * when what was parsed doesn't match the requirements provided through 'loop'
 * parameters.
 */
enum sol_json_loop_status {
    SOL_JSON_LOOP_REASON_OK = 0, /**< @brief Content successfully parsed. */
    SOL_JSON_LOOP_REASON_INVALID /**< @brief Failed to parse the content. */
};

/**
 * @brief Helper macro to iterate over the elements of a @b nested array in a JSON document
 *
 * When iterating over nested objects and arrays, you don't want to call
 * @ref sol_json_loop_iterate_init, that is why you need to use this macro for
 * nested stuff.
 *
 * @param scanner_ JSON scanner
 * @param token_ Current token on each iteration
 * @param element_type_ Token type of the elements in the array
 * @param status_ Loop exit status
 */
#define SOL_JSON_SCANNER_ARRAY_LOOP_TYPE_NESTED(scanner_, token_, element_type_, status_) \
    for (status_ = SOL_JSON_LOOP_REASON_OK; \
        sol_json_loop_iterate_array(scanner_, token_, &status_, element_type_);)

/**
 * @brief Helper macro to iterate over the elements of an array in a JSON document
 *
 * @param scanner_ JSON scanner
 * @param token_ Current token on each iteration
 * @param element_type_ Token type of the elements in the array
 * @param status_ Loop exit status
 *
 * @see sol_json_scanner_init
 */
#define SOL_JSON_SCANNER_ARRAY_LOOP_TYPE(scanner_, token_, element_type_, status_) \
    for (status_ = sol_json_loop_iterate_init(scanner_, token_, SOL_JSON_TYPE_ARRAY_START); \
        sol_json_loop_iterate_array(scanner_, token_, &status_, element_type_);)

/**
 * @brief Helper macro to iterate over the elements of a @b nested array in a JSON document,
 * ignoring the elements type.
 *
 * When iterating over nested objects and arrays, you don't want to call
 * @ref sol_json_loop_iterate_init, that is why you need to use this macro for
 * nested stuff.
 *
 * @param scanner_ JSON scanner
 * @param token_ Current token on each iteration
 * @param status_ Loop exit status
 */
#define SOL_JSON_SCANNER_ARRAY_LOOP_NESTED(scanner_, token_, status_) \
    for (status_ = SOL_JSON_LOOP_REASON_OK; \
        sol_json_loop_iterate_generic(scanner_, token_, SOL_JSON_TYPE_ARRAY_END, &status_);)

/**
 * @brief Helper macro to iterate over the elements an array in a JSON document,
 * ignoring the elements type.
 *
 * @param scanner_ JSON scanner
 * @param token_ Current token on each iteration
 * @param status_ Loop exit status
 */
#define SOL_JSON_SCANNER_ARRAY_LOOP(scanner_, token_, status_) \
    for (status_ = sol_json_loop_iterate_init(scanner_, token_, SOL_JSON_TYPE_ARRAY_START); \
        sol_json_loop_iterate_generic(scanner_, token_, SOL_JSON_TYPE_ARRAY_END, &status_);)

/**
 * @brief Helper macro to iterate over the elements of a @b nested object in a JSON document
 *
 * When iterating over nested objects and arrays, you don't want to call
 * @ref sol_json_loop_iterate_init, that is why you need to use this macro for
 * nested stuff.
 *
 * @param scanner_ JSON scanner
 * @param token_ Current token on each iteration
 * @param key_ Token to the current pair's key on each iteration
 * @param value_ Token to the current pair's value on each iteration
 * @param status_ Loop exit status
 */
#define SOL_JSON_SCANNER_OBJECT_LOOP_NESTED(scanner_, token_, key_, value_, status_) \
    for (status_ = SOL_JSON_LOOP_REASON_OK; \
        sol_json_loop_iterate_object(scanner_, token_, key_, value_, &status_);)

/**
 * @brief Helper macro to iterate over the elements of an object in a JSON document
 *
 * @param scanner_ JSON scanner
 * @param token_ Current token on each iteration
 * @param key_ Token to the current pair's key on each iteration
 * @param value_ Token to the current pair's value on each iteration
 * @param status_ Loop exit status
 *
 * @see sol_json_scanner_init
 */
#define SOL_JSON_SCANNER_OBJECT_LOOP(scanner_, token_, key_, value_, status_) \
    for (status_ = sol_json_loop_iterate_init(scanner_, token_, SOL_JSON_TYPE_OBJECT_START); \
        sol_json_loop_iterate_object(scanner_, token_, key_, value_, &status_);)

/**
 * @brief Initializes a JSON scanner.
 *
 * @param scanner JSON scanner
 * @param mem Pointer to the memory containing the JSON document
 * @param size Memory size in bytes
 *
 * @note May or may not be NULL terminated.
 */
static inline void
sol_json_scanner_init(struct sol_json_scanner *scanner, const void *mem, size_t size)
{
    scanner->mem = (const char *)mem;
    scanner->mem_end = scanner->mem + size;
    scanner->current = (const char *)mem;
}

/**
 * @brief Initializes a JSON scanner with empty information.
 *
 * @param scanner JSON scanner
 */
static inline void
sol_json_scanner_init_null(struct sol_json_scanner *scanner)
{
    scanner->mem = NULL;
    scanner->mem_end = NULL;
    scanner->current = NULL;
}

/**
 * @brief Initialized a JSON scanner from a struct @ref sol_str_slice
 *
 * @param scanner JSON scanner
 * @param slice The slice to initialized the JSON scanner
 */
static inline void
sol_json_scanner_init_from_slice(struct sol_json_scanner *scanner, const struct sol_str_slice slice)
{
    sol_json_scanner_init(scanner, slice.data, slice.len);
}

/**
 * @brief Initialized a JSON token from a struct @ref sol_str_slice
 *
 * @param token JSON token
 * @param slice The slice to initialized the JSON scanner
 */
static inline void
sol_json_token_init_from_slice(struct sol_json_token *token, const struct sol_str_slice slice)
{
    token->start = slice.data;
    token->end = slice.data + slice.len;
}

/**
 * @brief Initializes a JSON scanner based on the information of a second scanner.
 *
 * Useful to offload some segments of the JSON document to different parser routines.
 *
 * @param scanner JSON scanner
 * @param other Base JSON scanner
 */
static inline void
sol_json_scanner_init_from_scanner(struct sol_json_scanner *scanner,
    const struct sol_json_scanner *other)
{
    scanner->mem = other->mem;
    scanner->mem_end = other->mem_end;
    scanner->current = scanner->mem;
}

/**
 * @brief Initializes a JSON scanner based on the information of a JSON Token.
 *
 * Useful to offload some segments of the JSON document to different parser routines.
 *
 * @param scanner JSON scanner
 * @param token JSON token
 */
static inline void
sol_json_scanner_init_from_token(struct sol_json_scanner *scanner,
    const struct sol_json_token *token)
{
    scanner->mem = token->start;
    scanner->mem_end = token->end;
    scanner->current = scanner->mem;
}

/**
 * @brief Returns the size of the JSON document that wasn't scanned yet.
 *
 * @param scanner JSON scanner
 *
 * @return Remaining size in bytes
 */
static inline size_t
sol_json_scanner_get_size_remaining(const struct sol_json_scanner *scanner)
{
    return scanner->mem_end - scanner->current;
}

/**
 * @brief Returns the offset of @c mem in the data managed by @c scanner.
 *
 * Offset relative to the start of the JSON document portion handled by @c scanner.
 *
 * @param scanner JSON scanner
 * @param mem Pointer to JSON Document data
 *
 * @return Offset in bytes, @c -1 in case of error.
 */
static inline size_t
sol_json_scanner_get_mem_offset(const struct sol_json_scanner *scanner, const void *mem)
{
    const char *p = (const char *)mem;

    if (p < scanner->mem || p > scanner->mem_end)
        return (size_t)-1;
    return p - scanner->mem;
}

/**
 * @brief Returns the type of the token pointed by @c mem.
 *
 * @param mem Pointer to JSON Document data
 *
 * @return JSON type of the token pointed by @c mem
 */
static inline enum sol_json_type
sol_json_mem_get_type(const void *mem)
{
    const char *p = (const char *)mem;

    if (strchr("{}[],:tfn\"", *p))
        return (enum sol_json_type)*p;
    if (isdigit((uint8_t)*p) || *p == '-' || *p == '+')
        return SOL_JSON_TYPE_NUMBER;
    return SOL_JSON_TYPE_UNKNOWN;
}

/**
 * @brief Returns the type of the token pointed by @c token.
 *
 * @param token Token
 *
 * @return JSON type of @c token
 */
static inline enum sol_json_type
sol_json_token_get_type(const struct sol_json_token *token)
{
    return sol_json_mem_get_type(token->start);
}

/**
 * @brief Returns the token size.
 *
 * @param token Token
 *
 * @return Token size in bytes
 */
static inline size_t
sol_json_token_get_size(const struct sol_json_token *token)
{
    return token->end - token->start;
}

/**
 * @brief Checks if the string pointed by @c token is equal to the string @c str.
 *
 * @param token Token of type string
 * @param str The string to compare
 * @param len Size of @c str
 *
 * @return @c true if the contents are equal, @c false otherwise
 *
 * @note The JSON string pointed by token may be escaped, so take this into consideration
 * to compare compatible strings.
 */
static inline bool
sol_json_token_str_eq(const struct sol_json_token *token, const char *str, size_t len)
{
    size_t size;

    assert(sol_json_token_get_type(token) == SOL_JSON_TYPE_STRING);

    size = sol_json_token_get_size(token);
    return (size == len + 2) && memcmp(token->start + 1, str, len) == 0;
}

/**
 * @brief Helper macro to check if the string pointed by @c token_ is equal to a string literal.
 *
 * @param token_ Token of type string
 * @param str_ String literal
 *
 * @return @c true if the contents are equal, @c false otherwise
 *
 * @note The JSON string pointed by token may be escaped, so take this into consideration
 * to compare compatible strings.
 */
#define SOL_JSON_TOKEN_STR_LITERAL_EQ(token_, str_) \
    sol_json_token_str_eq(token_, str_, sizeof(str_) - 1)

/**
 * @brief Get the numeric value of the given token as an 64 bits unsigned integer.
 *
 * @param token the token to convert to number
 * @param value where to return the converted number
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL or @c UINT64_MAX if @c ERANGE
 *
 * @see sol_json_token_get_int64()
 * @see sol_json_token_get_uint32()
 * @see sol_json_token_get_double()
 */
int sol_json_token_get_uint64(const struct sol_json_token *token, uint64_t *value)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Get the numeric value of the given token as an 64 bits signed integer.
 *
 * @param token the token to convert to number
 * @param value where to return the converted number
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL, @c INT64_MAX or @c INT64_MIN if @c ERANGE
 *
 * @see sol_json_token_get_uint64()
 * @see sol_json_token_get_int32()
 * @see sol_json_token_get_double()
 */
int sol_json_token_get_int64(const struct sol_json_token *token, int64_t *value)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Get the numeric value of the given token as an 32 bits unsigned integer.
 *
 * @param token the token to convert to number
 * @param value where to return the converted number
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL or @c UINT32_MAX if @c ERANGE
 *
 * @see sol_json_token_get_uint64()
 * @see sol_json_token_get_int32()
 * @see sol_json_token_get_double()
 */
static inline int
sol_json_token_get_uint32(const struct sol_json_token *token, uint32_t *value)
{
    uint64_t tmp;
    int r = sol_json_token_get_uint64(token, &tmp);

    if (tmp > UINT32_MAX) {
        tmp = UINT32_MAX;
        if (r == 0)
            r = -ERANGE;
    }

    *value = tmp;
    return r;
}

/**
 * @brief Get the numeric value of the given token as an 32 bits signed integer.
 *
 * @param token the token to convert to number
 * @param value where to return the converted number
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL, @c INT32_MAX or @c INT32_MIN if @c ERANGE
 *
 * @see sol_json_token_get_uint64()
 * @see sol_json_token_get_int32()
 * @see sol_json_token_get_double()
 */
static inline int
sol_json_token_get_int32(const struct sol_json_token *token, int32_t *value)
{
    int64_t tmp;
    int r = sol_json_token_get_int64(token, &tmp);

    if (tmp > INT32_MAX) {
        tmp = INT32_MAX;
        if (r == 0)
            r = -ERANGE;
    } else if (tmp < INT32_MIN) {
        tmp = INT32_MIN;
        if (r == 0)
            r = -ERANGE;
    }

    *value = tmp;
    return r;
}

/**
 * @brief Get the numeric value of the given token as double-precision floating point.
 *
 * @param token the token to convert to number
 * @param value where to return the converted number
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0.0
 * if @c EINVAL, @c DBL_MAX or @c -DBL_MAX if @c ERANGE
 *
 * @see sol_json_token_get_uint64()
 * @see sol_json_token_get_int64()
 * @see sol_json_token_get_uint32()
 * @see sol_json_token_get_int32()
 */
int sol_json_token_get_double(const struct sol_json_token *token, double *value) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Converts a JSON token to a string slice.
 *
 * @param token the token to convert to string slice
 *
 * @return A string slice struct
 *
 * @see sol_str_slice
 */
static inline struct sol_str_slice
sol_json_token_to_slice(const struct sol_json_token *token)
{
    return SOL_STR_SLICE_STR(token->start, (size_t)(token->end - token->start));
}

/**
 * @brief Advance the scanner to the next JSON token.
 *
 * @param scanner JSON scanner to advance
 * @param token Current token after the advance
 *
 * @return @c true if successfully advanced, @c false otherwise
 */
bool sol_json_scanner_next(struct sol_json_scanner *scanner,
    struct sol_json_token *token)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NON_NULL(1, 2)
#endif
    ;

/**
 * @brief Modifies @c scanner to point to @c token end, skipping over the @c token content.
 *
 * If object/array start, it will be the matching end token.
 * otherwise it will be the given token (as there is no nesting).
 *
 * In every case the scanner->current position is reset to given
 * token->end and as it iterates the scanner->position is updated to
 * match the new token's end (@ref sol_json_scanner_next() behavior).
 *
 * @param scanner JSON scanner to advance
 * @param token Current token after the advance
 *
 * @return @c true if successfully skipped the token, @c false otherwise
 */
bool sol_json_scanner_skip(struct sol_json_scanner *scanner,
    struct sol_json_token *token)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NON_NULL(1, 2)
#endif
    ;

/**
 * @brief Retrieve <key, value> pair currently pointed by @c scanner.
 *
 * Retrieve the @c key and @c value tokens from scanner's current position
 * and update the scanner position to point to after the pair.
 *
 * @param scanner JSON scanner
 * @param key Token of the pair's key
 * @param value Token of the pair's value
 *
 * @return @c true if a pair is successfully retrieved, @c false otherwise
 */
bool sol_json_scanner_get_dict_pair(struct sol_json_scanner *scanner,
    struct sol_json_token *key, struct sol_json_token *value)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NON_NULL(1, 2, 3)
#endif
    ;

/**
 * @brief Function to help iterate over a generic JSON sequence.
 *
 * @param scanner JSON scanner
 * @param token Current token after the iteration
 * @param end_type Token type that ends the sequence
 * @param reason Exit status of an iteration
 *
 * @return @c true if successfully iterated over the sequence, @c false otherwise
 */
static inline bool
sol_json_loop_iterate_generic(struct sol_json_scanner *scanner, struct sol_json_token *token,
    enum sol_json_type end_type, enum sol_json_loop_status *reason)
{
    if (*reason != SOL_JSON_LOOP_REASON_OK)
        return false;

    if (!sol_json_scanner_next(scanner, token)) {
        *reason = SOL_JSON_LOOP_REASON_INVALID;
        return false;
    }

    if (sol_json_token_get_type(token) == end_type) {
        *reason = SOL_JSON_LOOP_REASON_OK;
        return false;
    }

    if (sol_json_token_get_type(token) == SOL_JSON_TYPE_ELEMENT_SEP) {
        if (!sol_json_scanner_next(scanner, token)) {
            *reason = SOL_JSON_LOOP_REASON_INVALID;
            return false;
        }
    }

    return true;
}

/**
 * @brief Function to help iterate over a JSON array.
 *
 * @param scanner JSON scanner
 * @param token Current token after the iteration
 * @param reason Exit status of an iteration
 * @param element_type Token type of the elements in the array
 *
 * @return @c true if successfully iterated over the sequence, @c false otherwise
 */
static inline bool
sol_json_loop_iterate_array(struct sol_json_scanner *scanner, struct sol_json_token *token,
    enum sol_json_loop_status *reason, enum sol_json_type element_type)
{
    if (!sol_json_loop_iterate_generic(scanner, token, SOL_JSON_TYPE_ARRAY_END, reason))
        return false;

    if (sol_json_token_get_type(token) == element_type) {
        *reason = SOL_JSON_LOOP_REASON_OK;
        return true;
    }

    *reason = SOL_JSON_LOOP_REASON_INVALID;
    return false;
}

/**
 * @brief Function to help iterate over a JSON object.
 *
 * @param scanner JSON scanner
 * @param token Current token after the iteration
 * @param key Token to the key of the current object pair
 * @param value Token to the value of the current object pair
 * @param reason Exit status of an iteration
 *
 * @return @c true if successfully iterated over the sequence, @c false otherwise
 */
static inline bool
sol_json_loop_iterate_object(struct sol_json_scanner *scanner, struct sol_json_token *token,
    struct sol_json_token *key, struct sol_json_token *value, enum sol_json_loop_status *reason)
{
    if (!sol_json_loop_iterate_generic(scanner, token, SOL_JSON_TYPE_OBJECT_END, reason))
        return false;

    *key = *token;
    if (!sol_json_scanner_get_dict_pair(scanner, key, value)) {
        *reason = SOL_JSON_LOOP_REASON_INVALID;
        return false;
    }

    *reason = SOL_JSON_LOOP_REASON_OK;
    return true;
}

/**
 * @brief Function to bootstrap an iteration over a JSON sequence.
 *
 * @param scanner JSON scanner
 * @param token Current token after initialization
 * @param start_type Token type that starts the sequence
 *
 * @return Exit status of the initialization
 */
static inline enum sol_json_loop_status
sol_json_loop_iterate_init(struct sol_json_scanner *scanner, struct sol_json_token *token,
    enum sol_json_type start_type)
{
    if (!sol_json_scanner_next(scanner, token))
        return SOL_JSON_LOOP_REASON_INVALID;
    if (sol_json_token_get_type(token) != start_type)
        return SOL_JSON_LOOP_REASON_INVALID;
    return SOL_JSON_LOOP_REASON_OK;
}

/**
 * @brief Calculate the size in bytes of the escaped version of a string.
 *
 * @param str Input string
 *
 * @return Size necessary to hold the escaped version of @c str
 */
size_t sol_json_calculate_escaped_string_len(const char *str);

/**
 * @brief Escapes JSON special and control characters from the string content.
 *
 * @param str String to escape
 * @param buf Where to append the escaped string - It must be already initialized.
 *
 * @return The escaped string
 */
char *sol_json_escape_string(const char *str, struct sol_buffer *buf);

/**
 * @brief Converts a double into a string suited for use in a JSON Document
 *
 * @param value Value to be converted
 * @param buf Where to append the converted value - It must be already initialized.
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_double_to_str(const double value, struct sol_buffer *buf);

/**
 * @brief Check if @c scanner content is pointing to a valid JSON element of
 * type @c start_type.
 *
 * @note May or may not be NULL terminated.
 *
 * @param scanner JSON scanner
 * @param start_type Token type of the desired JSON element
 *
 * @return @c true if JSON element is valid, @c false otherwise
 */
bool sol_json_is_valid_type(struct sol_json_scanner *scanner, enum sol_json_type start_type)
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1)
#endif
    ;

/**
 * @brief Inserts the string @c str in the end of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param str String to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @note The inserted string may be different of the original since this function
 * will call @ref sol_json_escape_string on it.
 */
int sol_json_serialize_string(struct sol_buffer *buffer, const char *str);

/**
 * @brief Inserts the string of the double @c val in the end
 * of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param val Value to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_serialize_double(struct sol_buffer *buffer, double val);

/**
 * @brief Inserts the string of the 32-bit integer @c val in the end
 * of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param val Value to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_serialize_int32(struct sol_buffer *buffer, int32_t val);

/**
 * @brief Inserts the string of the unsigned 32-bit integer @c val in the end
 * of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param val Value to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_serialize_uint32(struct sol_buffer *buffer, uint32_t val);

/**
 * @brief Inserts the string of the 64-bit integer @c val in the end
 * of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param val Value to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_serialize_int64(struct sol_buffer *buffer, int64_t val);

/**
 * @brief Inserts the string of the unsigned 64-bit integer @c val in the end
 * of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param val Value to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_serialize_uint64(struct sol_buffer *buffer, uint64_t val);

/**
 * @brief Inserts the string of the boolean value @c val in the end
 * of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 * @param val Value to be inserted in the JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_json_serialize_bool(struct sol_buffer *buffer, bool val);

/**
 * @brief Inserts the string "null" in the end of the JSON document contained in @c buffer.
 *
 * @param buffer Buffer containing the new JSON document
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_json_serialize_null(struct sol_buffer *buffer)
{
    static const struct sol_str_slice null = SOL_STR_SLICE_LITERAL("null");

    return sol_buffer_append_slice(buffer, null);
}

/**
 * @brief Appends the serialization of the given memory based on its description.
 *
 * If the SOL_MEMDESC_TYPE_STRUCTURE or SOL_MEMDESC_TYPE_PTR with
 * children, then it will be serialized as an object with keys being
 * the description name.
 *
 * @param buffer Buffer containing the new JSON document.
 * @param desc the memory description to use when serializing.
 * @param memory the memory described by @a desc.
 * @param detailed_structures if false, all members of struct marked
 *        as detailed will be omitted.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_json_load_memdesc()
 */
int sol_json_serialize_memdesc(struct sol_buffer *buffer, const struct sol_memdesc *desc, const void *memory, bool detailed_structures);

/**
 * @brief Loads the members of a memory from JSON according to its description.
 *
 * If the SOL_MEMDESC_TYPE_STRUCTURE or SOL_MEMDESC_TYPE_PTR with
 * children, then it will be loaded from an object with keys being
 * the description name.
 *
 * If defaults are desired, then call sol_memdesc_init_defaults()
 * before calling this function.
 *
 * If not all required members of a structure where provided, then @c
 * -ENODATA is returned. The code will try to load as much as possible
 * before returning.
 *
 * @param token the token to convert to memory using description.
 * @param desc the memory description to use when loading.
 * @param memory the memory described by @a desc.
 *
 * @return @c 0 on success, error code (always negative)
 *         otherwise. Note that @c -ENODATA will be returned if required
 *         structure members were missing.
 *
 * @see sol_json_serialize_memdesc()
 */
int sol_json_load_memdesc(const struct sol_json_token *token, const struct sol_memdesc *desc, void *memory);

/**
 *  @brief Copy to a @ref sol_buffer the string pointed by @c token.
 *
 *  If token is of type string, quotes are removed and string is unescaped. If not,
 *  the full token value is returned as a string.
 *
 *  If necessary memory will be allocated to place unescaped string, so
 *  caller is responsible to call @ref sol_buffer_fini after using the buffer
 *  content.
 *
 *  @note @ref sol_buffer_init will be called internally to initialize the buffer.
 *
 *  @param token Token to get the string from
 *  @param buffer Uninitialized buffer
 *
 *  @return @c 0 on success, a negative error code on failure
 */
int sol_json_token_get_unescaped_string(const struct sol_json_token *token, struct sol_buffer *buffer);

/**
 *  @brief Creates a copy of the unescaped and no-quotes string
 *  produced by @ref sol_json_token_get_unescaped_string.
 *
 *  Caller is responsible to free the string memory.
 *
 *  @param value A string type token
 *
 *  @return A copy of the unescaped string on success, @c NULL otherwise
 */
char *sol_json_token_get_unescaped_string_copy(const struct sol_json_token *value);

/**
 * @brief Get the value of the JSON Object child element referenced by
 * @a key_slice.
 *
 * @param scanner An initialized scanner, which is pointing to a JSON Object.
 * @param key_slice The key of the desired element.
 * @param value A pointer to the structure that will be filled with the value
 *        of the element referenced by @a key_slice.
 *
 * @return If any parameter is invalid, or if @a scanner is not pointing to a
 *         JSON Object, return -EINVAL. If @a key_slice is not present in the
 *         JSON Object, -ENOENT. If @a key was found, and @a value was update
 *         successfully with the value referenced by @a key, return 0.
 *
 * @see sol_json_array_get_at_index()
 */
int sol_json_object_get_value_by_key(struct sol_json_scanner *scanner, const struct sol_str_slice key_slice, struct sol_json_token *value);

/**
 * @brief Get the element in position @a i in JSON Array contained in @a scanner
 *
 * @param scanner An initialized scanner, which is pointing to a JSON Array.
 * @param value A pointer to the structure that will be filled with the value
 *        of the element at position @a i.
 * @param i The position of the desired element.
 *
 * @return If any parameter is invalid, or if @a scanner is not pointing to a
 *         JSON Array, return -EINVAL. If @a i is larger than the array's
 *         length, -ENOENT. If @a value was updated successfully with the value
 *         in position @a i, return 0.
 *
 * @see sol_json_object_get_value_by_key()
 */
int sol_json_array_get_at_index(struct sol_json_scanner *scanner, uint16_t i, struct sol_json_token *value);

/**
 * @brief Scanner used to go through segments of a JSON Path.
 *
 * @note JSONPath syntax is available at http://goessner.net/articles/JsonPath/.
 *
 * @see sol_json_path_scanner_init()
 * @see sol_json_path_get_next_segment()
 * @see SOL_JSON_PATH_FOREACH()
 */
typedef struct sol_json_path_scanner {
    const char *path; /**< @brief The JSONPath string. */
    const char *end; /**< @brief Points to last character from path. */
    /**
     * @brief Points to last visited position from path and the beginning of
     * next segment.
     */
    const char *current;
} sol_json_path_scanner;

/**
 * @brief Get the element referenced by the JSON Path @a path in a JSON Object
 * or Array.
 *
 * @param scanner An initialized scanner, which is pointing to a JSON Object
 *        or a JSON Array.
 * @param path The JSON Path of the desired element.
 * @param value A pointer to the structure that will be filled with the value
 *        of the element referenced by @a path.
 *
 * @return If any parameter is invalid, or if @a scanner is not pointing to a
 *         JSON Object or JSON Array, or if @a path is not a valid JSON Path,
 *         return -EINVAL. If @a path is pointing to an invalid position in a
 *         JSON Object or in a JSON Array, -ENOENT. If @a value was
 *         successfully updated with the value of the element referenced by @a
 *         path, return 0.
 *
 * @see sol_json_path_scanner
 */
int sol_json_get_value_by_path(struct sol_json_scanner *scanner, struct sol_str_slice path, struct sol_json_token *value);

/**
 * @brief Initialize a JSON Path @a scanner with @a path.
 *
 * JSON path scanner can be used to go through segments of a JSON Path using
 * SOL_JSON_PATH_FOREACH() or sol_json_path_get_next_segment() functions.
 *
 * @param scanner An uninitialized JSON Path scanner.
 * @param path A valid JSON Path string.
 *
 * @return 0 on success, -EINVAL if scanner is NULL.
 *
 * @see sol_json_path_scanner
 */
int sol_json_path_scanner_init(struct sol_json_path_scanner *scanner, struct sol_str_slice path)
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1)
#endif
    ;

/**
 * @brief Get next segment from JSON Path in @a scanner.
 *
 * Update @a slice with the next valid JSON Path segment in @a scanner.
 *
 * @param scanner An initialized JSON Path scanner.
 * @param slice A pointer to the slicer structure to be filled with next
 *        JSON Path segment.
 * @param status A pointer to the field to be filled with the reason this
 *        function termination. SOL_JSON_LOOP_REASON_INVALID if an error
 *        occurred when parsing the JSON Path. SOL_JSON_LOOP_REASON_OK if the
 *        next segment was updated in @a slice or if there is no more segments
 *        in this JSON Path.
 *
 * @return True if next segment was updated in @a value. False if an error
 *         occurred or if there is no more segments available.
 */
bool sol_json_path_get_next_segment(struct sol_json_path_scanner *scanner, struct sol_str_slice *slice, enum sol_json_loop_status *status)
#ifndef DOXYGEN_RUN
    SOL_ATTR_NON_NULL(1, 2, 3)
#endif
    ;

/**
 * @brief Get the integer index from a JSON Path array segment.
 *
 * This function expects a valid JSON Path segment with format: [NUMBER],
 * where NUMBER is an integer and returns NUMBER converted to an integer
 * variable.
 *
 * @param key The key to extract the integer index.
 *
 * @return If key is a valid array segment in the format expecified,
 *         returns the converted index. If key is invalid, returns
 *         -EINVAL and if number is out of range, returns -ERANGE.
 *
 * @see SOL_JSON_PATH_FOREACH()
 */
int32_t sol_json_path_array_get_segment_index(struct sol_str_slice key);

/**
 * @brief Check if @a slice is a valid JSON Path array segment.
 *
 * @param slice A JSON Path segment.
 *
 * @return True if @a slice is a valid JSON Path array segment. False
 *         otherwise.
 *
 * @see SOL_JSON_PATH_FOREACH()
 */
static inline bool
sol_json_path_is_array_key(struct sol_str_slice slice)
{
    return slice.data && slice.len >= 2 &&
           slice.data[0] == '[' &&  //is between brackets or
           slice.data[1] != '\''; //index is not a string
}

/**
 * @def SOL_JSON_PATH_FOREACH(scanner, key, status)
 *
 * @brief Go through all segments of a JSON Path.
 *
 * Macro used to visit all segments of a JSON Path. If the need of visiting a
 * JSON Path is accessing JSON Objects or JSON Array elements, prefer using
 * function @ref sol_json_get_value_by_path().
 *
 * @param scanner An initialized struct sol_json_path_scanner.
 * @param key A pointer to struct sol_str_slice, that is going to be filled
 *        with the current key being visited.
 * @param status A pointer to the field to be filled with the reason this
 *        macro termination. SOL_JSON_LOOP_REASON_INVALID if an error
 *        occurred when parsing the JSON Path. SOL_JSON_LOOP_REASON_OK if
 *        we reached the end of the JSON Path.
 *
 * Usage example:
 * @code
 *
 * const char *path = "$.my_key[3].other_key"; //Replace path here
 * struct sol_json_path_scanner path_scanner;
 * enum sol_json_loop_status reason;
 * struct sol_str_slice key_slice;
 *
 * sol_json_path_scanner_init(&path_scanner, path);
 * SOL_JSON_PATH_FOREACH(path_scanner, key_slice, reason) {
 *     printf("%*s\n", SOL_STR_SLICE_PRINT(key_slice));
 *     //Do something else
 * }
 *
 * if (status != SOL_JSON_LOOP_REASON_OK) {
 *     //Error Handling
 * }
 *
 * @endcode
 *
 * For the path in example, we would print:
 * @code
 * my_key
 * [3]
 * other_key
 * @endcode
 */
#define SOL_JSON_PATH_FOREACH(scanner, key, status) \
    for (status = SOL_JSON_LOOP_REASON_OK; \
        sol_json_path_get_next_segment(&scanner, &key_slice, &status);)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
