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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "sol-json.h"
#include "sol-log.h"
#include "sol-util.h"
#include <float.h>
#include <math.h>

static bool
check_symbol(struct sol_json_scanner *scanner, struct sol_json_token *token,
    const char *symname, unsigned symlen)
{
    if (sol_json_scanner_get_size_remaining(scanner) < symlen) {
        SOL_ERR("%u: premature end of buffer: %u available, "
            "need sizeof(%s)=%u",
            sol_json_scanner_get_mem_offset(scanner, scanner->current),
            sol_json_scanner_get_size_remaining(scanner), symname,
            symlen);
        errno = EINVAL;
        return false;
    }
    if (memcmp(scanner->current, symname, symlen) != 0) {
        SOL_ERR("%u: expected token \"%s\", have \"%.*s\"",
            sol_json_scanner_get_mem_offset(scanner, scanner->current),
            symname, symlen, scanner->current);
        errno = EINVAL;
        return false;
    }
    token->start = scanner->current;
    token->end = scanner->current + symlen;
    scanner->current = token->end;
    return true;
}

static bool
check_string(struct sol_json_scanner *scanner, struct sol_json_token *token)
{
    static const char escapable_chars[] = { '"', '\\', '/', 'b', 'f', 'n', 'r', 't', 'u' };
    bool escaped = false;

    token->start = scanner->current;
    for (scanner->current++; scanner->current < scanner->mem_end; scanner->current++) {
        char c = scanner->current[0];
        if (escaped) {
            escaped = false;
            if (!memchr(escapable_chars, c, sizeof(escapable_chars))) {
                SOL_ERR("%u: cannot escape %#x (%c)",
                    sol_json_scanner_get_mem_offset(scanner, scanner->current),
                    scanner->current[0], scanner->current[0]);
                token->start = NULL;
                errno = EINVAL;
                return false;
            }
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            token->end = scanner->current + 1;
            scanner->current = token->end;
            return true;
        }
    }
    SOL_ERR("%u: unfinished string.", sol_json_scanner_get_mem_offset(scanner, scanner->current));
    token->start = NULL;
    errno = EINVAL;
    return false;
}

static bool
check_number(struct sol_json_scanner *scanner, struct sol_json_token *token)
{
    const char *frac = NULL;
    const char *exp = NULL;

    token->start = scanner->current;
    for (scanner->current++; scanner->current < scanner->mem_end; scanner->current++) {
        char c = scanner->current[0];
        if (c >= '0' && c <= '9')
            continue;

        if (exp)
            break;

        if (c == 'e' || c == 'E') {
            if (scanner->current + 1 < scanner->mem_end) {
                c = scanner->current[1];
                if (c == '-' || c == '+')
                    scanner->current++;
            }
            exp = scanner->current;
        } else if (!frac && c == '.') {
            frac = scanner->current;
        } else {
            break;
        }
    }
    if (frac == scanner->current || exp == scanner->current) {
        SOL_ERR("%u: missing trailing digits in number",
            sol_json_scanner_get_mem_offset(scanner, scanner->current));
        token->start = NULL;
        errno = EINVAL;
        return false;
    }

    token->end = scanner->current;
    return true;
}

static int
token_get_uint64(const struct sol_json_token *token, uint64_t *value)
{
    const char *itr = token->start;
    uint64_t tmpvar = 0;

    if (*itr == '+')
        itr++;

    for (; itr < token->end; itr++) {
        const char c = *itr;
        if (c >= '0' && c <= '9') {
            int r;

            r = sol_util_uint64_mul(tmpvar, 10, &tmpvar);
            if (r < 0)
                goto overflow;

            r = sol_util_uint64_add(tmpvar, c - '0', &tmpvar);
            if (r < 0)
                goto overflow;
            continue;
        }
        *value = tmpvar; /* best effort */
        SOL_DBG("unexpected char '%c' at position %u of integer token %.*s",
            c, (unsigned)(itr - token->start),
            sol_json_token_get_size(token), token->start);
        return -EINVAL;

overflow:
        *value = UINT64_MAX; /* best effort */
        SOL_DBG("number is too large at position %u of integer token %.*s",
            (unsigned)(itr - token->start),
            sol_json_token_get_size(token), token->start);
        return -ERANGE;
    }

    *value = tmpvar;
    return 0;
}

static int
token_get_int64(const struct sol_json_token *token, int64_t *value)
{
    struct sol_json_token inttoken = *token;
    int r, sign = 1;
    uint64_t tmpvar;

    if (*inttoken.start == '-') {
        sign = -1;
        inttoken.start++;
    }

    r = token_get_uint64(&inttoken, &tmpvar);
    if (r == 0) {
        if (sign > 0 && tmpvar > INT64_MAX) {
            *value = INT64_MAX;
            return -ERANGE;
        } else if (sign < 0 && tmpvar > ((uint64_t)INT64_MAX + 1)) {
            *value = INT64_MIN;
            return -ERANGE;
        }
        *value = sign * tmpvar;
        return 0;
    } else {
        /* best effort to help users ignoring return false */
        if (r == -ERANGE) {
            if (sign > 0)
                *value = INT64_MAX;
            else
                *value = INT64_MIN;
        } else {
            if (sign > 0 && tmpvar > INT64_MAX)
                *value = INT64_MAX;
            else if (sign < 0 && tmpvar > ((uint64_t)INT64_MAX + 1))
                *value = INT64_MIN;
            else
                *value = sign * tmpvar;
        }
        return r;
    }
}

SOL_API int
sol_json_token_get_uint64(const struct sol_json_token *token, uint64_t *value)
{
    *value = 0;
    SOL_NULL_CHECK(token, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    if (token->start >= token->end) {
        SOL_WRN("invalid token: start=%p, end=%p",
            token->start, token->end);
        return -EINVAL;
    }
    if (sol_json_token_get_type(token) != SOL_JSON_TYPE_NUMBER) {
        SOL_WRN("expected number, got token type '%c' for token \"%.*s\"",
            sol_json_token_get_type(token),
            sol_json_token_get_size(token), token->start);
        return -EINVAL;
    }
    if (*token->start == '-') {
        SOL_DBG("%.*s: negative number where unsigned is expected",
            sol_json_token_get_size(token), token->start);
        return -ERANGE;
    }

    return token_get_uint64(token, value);
}

SOL_API int
sol_json_token_get_int64(const struct sol_json_token *token, int64_t *value)
{
    *value = 0;
    SOL_NULL_CHECK(token, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);
    if (token->start >= token->end) {
        SOL_WRN("invalid token: start=%p, end=%p",
            token->start, token->end);
        return -EINVAL;
    }
    if (sol_json_token_get_type(token) != SOL_JSON_TYPE_NUMBER) {
        SOL_WRN("expected number, got token type '%c' for token \"%.*s\"",
            sol_json_token_get_type(token),
            sol_json_token_get_size(token), token->start);
        return -EINVAL;
    }

    return token_get_int64(token, value);
}

SOL_API int
sol_json_token_get_double(const struct sol_json_token *token, double *value)
{
    double tmp_value;
    char *endptr;

    /* NOTE: Using a copy to ensure trailing \0 and strtod() so we
     * properly parse numbers with large precision.
     *
     * Splitting the integer, fractional and exponent parts and doing
     * the math using double numbers will result in rounding errors
     * when parsing DBL_MAX using "%.64g" formatting.
     *
     * Since parsing it is complex (ie:
     * http://www.netlib.org/fp/dtoa.c), we take the short path to
     * call our helper around libc's strtod() that limits the amount
     * of bytes.
     */

    errno = 0;
    tmp_value = sol_util_strtodn(token->start, &endptr,
        sol_json_token_get_size(token), false);
    SOL_INT_CHECK(errno, != 0, -errno);

    if (endptr == token->start)
        return -EINVAL;

    *value = tmp_value;

    return 0;
}

SOL_API bool
sol_json_scanner_next(struct sol_json_scanner *scanner, struct sol_json_token *token)
{
    token->start = NULL;
    token->end = NULL;

    for (; scanner->current < scanner->mem_end; scanner->current++) {
        enum sol_json_type type = sol_json_mem_get_type(scanner->current);
        switch (type) {
        case SOL_JSON_TYPE_UNKNOWN:
            if (!isspace(scanner->current[0])) {
                SOL_ERR("%u: unexpected symbol %#x (%c)",
                    sol_json_scanner_get_mem_offset(scanner, scanner->current),
                    scanner->current[0], scanner->current[0]);
                errno = EINVAL;
                return false;
            }
            break;

        case SOL_JSON_TYPE_OBJECT_START:
        case SOL_JSON_TYPE_OBJECT_END:
        case SOL_JSON_TYPE_ARRAY_START:
        case SOL_JSON_TYPE_ARRAY_END:
        case SOL_JSON_TYPE_ELEMENT_SEP:
        case SOL_JSON_TYPE_PAIR_SEP:
            token->start = scanner->current;
            token->end = scanner->current + 1;
            scanner->current = token->end;
            return true;

        case SOL_JSON_TYPE_TRUE:
            return check_symbol(scanner, token, "true", sizeof("true") - 1);

        case SOL_JSON_TYPE_FALSE:
            return check_symbol(scanner, token, "false", sizeof("false") - 1);

        case SOL_JSON_TYPE_NULL:
            return check_symbol(scanner, token, "null", sizeof("null") - 1);

        case SOL_JSON_TYPE_STRING:
            return check_string(scanner, token);

        case SOL_JSON_TYPE_NUMBER:
            return check_number(scanner, token);
        }
    }

    errno = 0;
    return false;
}

SOL_API bool
sol_json_scanner_skip_over(struct sol_json_scanner *scanner,
    struct sol_json_token *token)
{
    int level = 0;

    scanner->current = token->end;
    do {
        switch (sol_json_token_get_type(token)) {
        case SOL_JSON_TYPE_UNKNOWN:
            errno = EINVAL;
            return false;

        case SOL_JSON_TYPE_OBJECT_START:
        case SOL_JSON_TYPE_ARRAY_START:
            level++;
            break;

        case SOL_JSON_TYPE_OBJECT_END:
        case SOL_JSON_TYPE_ARRAY_END:
            level--;
            if (unlikely(level < 0)) {
                errno = EINVAL;
                return false;
            }
            break;

        case SOL_JSON_TYPE_ELEMENT_SEP:
        case SOL_JSON_TYPE_PAIR_SEP:
        case SOL_JSON_TYPE_TRUE:
        case SOL_JSON_TYPE_FALSE:
        case SOL_JSON_TYPE_NULL:
        case SOL_JSON_TYPE_STRING:
        case SOL_JSON_TYPE_NUMBER:
            break;
        }

        if (level > 0) {
            if (!sol_json_scanner_next(scanner, token)) {
                errno = EINVAL;
                return false;
            }
        }
    } while (level > 0);

    return true;
}

SOL_API bool
sol_json_scanner_get_dict_pair(struct sol_json_scanner *scanner,
    struct sol_json_token *key,
    struct sol_json_token *value)
{
    const char *start;

    if (sol_json_mem_get_type(key->start) != SOL_JSON_TYPE_STRING) {
        SOL_ERR("offset %u: unexpected token '%c' (want string)",
            sol_json_scanner_get_mem_offset(scanner, key->start),
            key->start[0]);
        errno = EINVAL;
        return false;
    }

    if (!sol_json_scanner_next(scanner, value)) {
        SOL_ERR("offset %u: unexpected end of file (want pair separator)",
            sol_json_scanner_get_mem_offset(scanner, scanner->current));
        errno = EINVAL;
        return false;
    }

    if (sol_json_token_get_type(value) != SOL_JSON_TYPE_PAIR_SEP) {
        SOL_ERR("offset %u: unexpected token '%c' (want pair separator)",
            sol_json_scanner_get_mem_offset(scanner, value->start),
            value->start[0]);
        errno = EINVAL;
        return false;
    }

    if (!sol_json_scanner_next(scanner, value)) {
        SOL_ERR("offset %u: unexpected end of file (want pair value)",
            sol_json_scanner_get_mem_offset(scanner, scanner->current));
        errno = EINVAL;
        return false;
    }

    start = value->start;
    if (!sol_json_scanner_skip_over(scanner, value)) {
        SOL_ERR("offset %u: unexpected end of file (want pair value to skip over)",
            sol_json_scanner_get_mem_offset(scanner, scanner->current));
        errno = EINVAL;
        return false;
    }

    value->start = start;
    return true;
}
