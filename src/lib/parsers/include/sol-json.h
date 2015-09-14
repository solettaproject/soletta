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
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sol-macros.h>
#include <sol-str-slice.h>

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
 * These are the parsers that Soletta provides (JSON only, for now)
 *
 * @{
 */

struct sol_json_scanner {
    const char *mem;
    const char *mem_end;
    const char *current;
};

struct sol_json_token {
    const char *start;
    const char *end; /* non-inclusive */
};

enum sol_json_type {
    SOL_JSON_TYPE_UNKNOWN = 0,
    SOL_JSON_TYPE_OBJECT_START = '{',
    SOL_JSON_TYPE_OBJECT_END = '}',
    SOL_JSON_TYPE_ARRAY_START = '[',
    SOL_JSON_TYPE_ARRAY_END = ']',
    SOL_JSON_TYPE_ELEMENT_SEP = ',',
    SOL_JSON_TYPE_PAIR_SEP = ':',
    SOL_JSON_TYPE_TRUE = 't',
    SOL_JSON_TYPE_FALSE = 'f',
    SOL_JSON_TYPE_NULL = 'n',
    SOL_JSON_TYPE_STRING = '"',
    SOL_JSON_TYPE_NUMBER = '1',
};

enum sol_json_loop_reason {
    SOL_JSON_LOOP_REASON_OK = 0,
    SOL_JSON_LOOP_REASON_INVALID
};

#define SOL_JSON_SCANNER_ARRAY_LOOP_NEST(scanner_, token_, element_type_, end_reason_) \
    for (end_reason_ = SOL_JSON_LOOP_REASON_OK;  \
        _sol_json_loop_helper_array(scanner_, token_, &end_reason_, element_type_);)

#define SOL_JSON_SCANNER_ARRAY_LOOP(scanner_, token_, element_type_, end_reason_) \
    for (end_reason_ = _sol_json_loop_helper_init(scanner_, token_, SOL_JSON_TYPE_ARRAY_START); \
        _sol_json_loop_helper_array(scanner_, token_, &end_reason_, element_type_);)

#define SOL_JSON_SCANNER_OBJECT_LOOP_NEST(scanner_, token_, key_, value_, end_reason_) \
    for (end_reason_ = SOL_JSON_LOOP_REASON_OK; \
        _sol_json_loop_helper_object(scanner_, token_, key_, value_, &end_reason_);)

#define SOL_JSON_SCANNER_OBJECT_LOOP(scanner_, token_, key_, value_, end_reason_) \
    for (end_reason_ = _sol_json_loop_helper_init(scanner_, token_, SOL_JSON_TYPE_OBJECT_START); \
        _sol_json_loop_helper_object(scanner_, token_, key_, value_, &end_reason_);)

static inline void
sol_json_scanner_init(struct sol_json_scanner *scanner, const void *mem, unsigned int size)
{
    scanner->mem = (const char *)mem;
    scanner->mem_end = scanner->mem + size;
    scanner->current = (const char *)mem;
}

static inline void
sol_json_scanner_init_null(struct sol_json_scanner *scanner)
{
    scanner->mem = NULL;
    scanner->mem_end = NULL;
    scanner->current = NULL;
}

static inline void
sol_json_scanner_init_from_scanner(struct sol_json_scanner *scanner,
    const struct sol_json_scanner *other)
{
    scanner->mem = other->mem;
    scanner->mem_end = other->mem_end;
    scanner->current = scanner->mem;
}

static inline void
sol_json_scanner_init_from_token(struct sol_json_scanner *scanner,
    const struct sol_json_token *token)
{
    scanner->mem = token->start;
    scanner->mem_end = token->end;
    scanner->current = scanner->mem;
}

static inline unsigned int
sol_json_scanner_get_size_remaining(const struct sol_json_scanner *scanner)
{
    return scanner->mem_end - scanner->current;
}

static inline unsigned int
sol_json_scanner_get_mem_offset(const struct sol_json_scanner *scanner, const void *mem)
{
    const char *p = (const char *)mem;

    if (p < scanner->mem || p > scanner->mem_end)
        return (unsigned int)-1;
    return p - scanner->mem;
}

static inline enum sol_json_type
sol_json_mem_get_type(const void *mem)
{
    const char *p = (const char *)mem;

    if (strchr("{}[],:tfn\"", *p))
        return (enum sol_json_type)*p;
    if (isdigit(*p) || *p == '-' || *p == '+')
        return SOL_JSON_TYPE_NUMBER;
    return SOL_JSON_TYPE_UNKNOWN;
}

static inline enum sol_json_type
sol_json_token_get_type(const struct sol_json_token *token)
{
    return sol_json_mem_get_type(token->start);
}

static inline unsigned int
sol_json_token_get_size(const struct sol_json_token *token)
{
    return token->end - token->start;
}

static inline bool
sol_json_token_str_eq(const struct sol_json_token *token, const char *str, unsigned int len)
{
    unsigned int size;

    assert(sol_json_token_get_type(token) == SOL_JSON_TYPE_STRING);

    size = sol_json_token_get_size(token);
    return (size == len + 2) && memcmp(token->start + 1, str, len) == 0;
}

#define SOL_JSON_TOKEN_STR_LITERAL_EQ(token_, str_) \
    sol_json_token_str_eq(token_, str_, sizeof(str_) - 1)

static inline void
sol_json_token_remove_quotes(struct sol_json_token *token)
{
    if (sol_json_token_get_size(token) < 3)
        return;
    if (*token->start == '"')
        token->start++;
    if (*(token->end - 1) == '"')
        token->end--;
}

/**
 * Get the numeric value of the given token as an 64 bits unsigned integer.
 *
 * @param token the token to convert to number.
 * @param value where to return the converted number.
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL or @c UINT64_MAX if @c ERANGE.
 *
 * @see sol_json_token_get_int64()
 * @see sol_json_token_get_uint32()
 * @see sol_json_token_get_double()
 */
int sol_json_token_get_uint64(const struct sol_json_token *token, uint64_t *value) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1, 2);

/**
 * Get the numeric value of the given token as an 64 bits signed integer.
 *
 * @param token the token to convert to number.
 * @param value where to return the converted number.
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL, @c INT64_MAX or @c INT64_MIN if @c ERANGE.
 *
 * @see sol_json_token_get_uint64()
 * @see sol_json_token_get_int32()
 * @see sol_json_token_get_double()
 */
int sol_json_token_get_int64(const struct sol_json_token *token, int64_t *value) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1, 2);

/**
 * Get the numeric value of the given token as an 32 bits unsigned integer.
 *
 * @param token the token to convert to number.
 * @param value where to return the converted number.
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL or @c UINT32_MAX if @c ERANGE.
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
 * Get the numeric value of the given token as an 32 bits signed integer.
 *
 * @param token the token to convert to number.
 * @param value where to return the converted number.
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0
 * if @c EINVAL, @c INT32_MAX or @c INT32_MIN if @c ERANGE.
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
 * Get the numeric value of the given token as double-precision floating point.
 *
 * @param token the token to convert to number.
 * @param value where to return the converted number.
 *
 * @return 0 on success, -errno on failure (@c EINVAL or @c
 * ERANGE). On errors @a value will be set to a best-match, such as 0.0
 * if @c EINVAL, @c DBL_MAX or @c -DBL_MAX if @c ERANGE.
 *
 * @see sol_json_token_get_uint64()
 * @see sol_json_token_get_int64()
 * @see sol_json_token_get_uint32()
 * @see sol_json_token_get_int32()
 */
int sol_json_token_get_double(const struct sol_json_token *token, double *value) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1, 2);

/**
 * Converts a JSON token to a string slice.
 *
 * @param token the token to convert to string slice.
 *
 * @return A string slice struct.
 *
 * @see sol_str_slice
 */
static inline struct sol_str_slice
sol_json_token_to_slice(const struct sol_json_token *token)
{
    return SOL_STR_SLICE_STR(token->start, token->end - token->start);
}


bool sol_json_scanner_next(struct sol_json_scanner *scanner,
    struct sol_json_token *token) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1, 2);

/* modifies token to be the last token to skip over the given entry.
 * if object/array start, it will be the matching end token.
 * otherwise it will be the given token (as there is no nesting).
 *
 * in every case the scanner->current position is reset to given
 * token->end and as it iterates the scanner->position is updated to
 * match the new token's end (sol_json_scanner_next() behavior).
 */
bool sol_json_scanner_skip_over(struct sol_json_scanner *scanner,
    struct sol_json_token *token) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1, 2);

bool sol_json_scanner_get_dict_pair(struct sol_json_scanner *scanner,
    struct sol_json_token *key, struct sol_json_token *value) SOL_ATTR_WARN_UNUSED_RESULT SOL_ATTR_NONNULL(1, 2, 3);

static inline bool
_sol_json_loop_helper_generic(struct sol_json_scanner *scanner, struct sol_json_token *token,
    enum sol_json_type end_type, enum sol_json_loop_reason *reason)
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

static inline bool
_sol_json_loop_helper_array(struct sol_json_scanner *scanner, struct sol_json_token *token,
    enum sol_json_loop_reason *reason, enum sol_json_type element_type)
{
    if (!_sol_json_loop_helper_generic(scanner, token, SOL_JSON_TYPE_ARRAY_END, reason))
        return false;

    if (sol_json_token_get_type(token) == element_type) {
        *reason = SOL_JSON_LOOP_REASON_OK;
        return true;
    }

    *reason = SOL_JSON_LOOP_REASON_INVALID;
    return false;
}

static inline bool
_sol_json_loop_helper_object(struct sol_json_scanner *scanner, struct sol_json_token *token,
    struct sol_json_token *key, struct sol_json_token *value, enum sol_json_loop_reason *reason)
{
    if (!_sol_json_loop_helper_generic(scanner, token, SOL_JSON_TYPE_OBJECT_END, reason))
        return false;

    *key = *token;
    if (!sol_json_scanner_get_dict_pair(scanner, key, value)) {
        *reason = SOL_JSON_LOOP_REASON_INVALID;
        return false;
    }

    *reason = SOL_JSON_LOOP_REASON_OK;
    return true;
}

static inline enum sol_json_loop_reason
_sol_json_loop_helper_init(struct sol_json_scanner *scanner, struct sol_json_token *token,
    enum sol_json_type start_type)
{
    if (!sol_json_scanner_next(scanner, token))
        return SOL_JSON_LOOP_REASON_INVALID;
    if (sol_json_token_get_type(token) != start_type)
        return SOL_JSON_LOOP_REASON_INVALID;
    return SOL_JSON_LOOP_REASON_OK;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
