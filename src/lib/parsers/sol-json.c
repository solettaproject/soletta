/*
 * This file is part of the Soletta Project
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "sol-json.h"
#include "sol-log.h"
#include "sol-macros.h"
#include "sol-util-internal.h"
#include <float.h>
#include <math.h>

static const char sol_json_escapable_chars[] = { '\\', '\"', '/', '\b', '\f', '\n', '\r', '\t' };

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

static inline const char *
get_lowest_not_null_pointer(const char *p, const char *p2)
{
    if (p && (!p2 || p < p2))
        return p;

    return p2;
}

static bool
json_path_parse_key_after_dot(struct sol_json_path_scanner *scanner, struct sol_str_slice *slice)
{
    const char *start, *first_dot, *first_bracket_start, *end;

    start = scanner->current + 1;
    first_dot = memchr(start, '.', scanner->end - start);
    first_bracket_start = memchr(start, '[', scanner->end - start);

    end = get_lowest_not_null_pointer(first_dot, first_bracket_start);
    if (end == NULL)
        end = scanner->end;

    if (end == start)
        return false;

    *slice = SOL_STR_SLICE_STR(start, end - start);
    scanner->current = start + slice->len;
    return true;
}

static bool
json_path_parse_key_in_brackets(struct sol_json_path_scanner *scanner, struct sol_str_slice *slice)
{
    const char *p;

    p = scanner->current + 1;

    //index is a string
    if (*p == '\'') {
        //Look for first unescaped '
        for (p = p + 1; p < scanner->end; p++) {
            p = memchr(p, '\'', scanner->end - p);
            if (!p)
                return false;
            if (*(p - 1) != '\\') //is not escaped
                break;
        }
        p++;
        if (p >= scanner->end || *p != ']')
            return false;
    } else if (*p != ']') { //index is is not empty and is suppose to be a num
        p++;
        p = memchr(p, ']', scanner->end - p);
        if (!p)
            return false;

    } else
        return false;

    p++;
    *slice = SOL_STR_SLICE_STR(scanner->current, p - scanner->current);
    scanner->current += slice->len;
    return true;
}

/*
 * If index is between [' and '] we need to unescape the ' char and to remove
 * the [' and '] separator.
 */
static int
json_path_parse_object_key(const struct sol_str_slice slice, struct sol_buffer *buffer)
{
    const char *end, *p, *p2;
    struct sol_str_slice key;
    int r;

    if (slice.data[0] != '[') {
        sol_buffer_init_flags(buffer, (char *)slice.data, slice.len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        buffer->used = buffer->capacity;
        return 0;
    }

    //remove [' and ']
    key = SOL_STR_SLICE_STR(slice.data + 2, slice.len - 4);

    //unescape '\'' if necessary
    sol_buffer_init_flags(buffer, NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    end = key.data + key.len;
    for (p = key.data; p < end; p = p2 + 1) {
        p2 = memchr(p, '\'', end - p);
        if (!p2)
            break;

        //Append string preceding '
        r = sol_buffer_append_slice(buffer, SOL_STR_SLICE_STR(p, p2 - p - 1));
        SOL_INT_CHECK(r, < 0, r);
        r = sol_buffer_append_char(buffer, '\'');
        SOL_INT_CHECK(r, < 0, r);
    }

    if (!buffer->data) {
        sol_buffer_init_flags(buffer, (char *)key.data, key.len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        buffer->used = buffer->capacity;
        return 0;
    }

    //Append the string leftover
    r = sol_buffer_append_slice(buffer, SOL_STR_SLICE_STR(p, end - p));
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

SOL_API int
sol_json_token_get_uint64(const struct sol_json_token *token, uint64_t *value)
{
    SOL_NULL_CHECK(token, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    *value = 0;

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
    SOL_NULL_CHECK(token, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    *value = 0;

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
    char *endptr;
    int r;

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

    SOL_NULL_CHECK(token, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    *value = sol_util_strtod_n(token->start, &endptr,
        sol_json_token_get_size(token), false);

    r = -errno;
    if (endptr == token->start)
        r = -EINVAL;
    else if (isinf(*value)) {
        SOL_DBG("token '%.*s' is infinite",
            sol_json_token_get_size(token), token->start);
        if (*value < 0)
            *value = -DBL_MAX;
        else
            *value = DBL_MAX;
        r = -ERANGE;
    } else if (isnan(*value)) {
        SOL_DBG("token '%.*s' is not a number",
            sol_json_token_get_size(token), token->start);
        *value = 0;
        r = -EINVAL;
    } else if (fpclassify(*value) == FP_SUBNORMAL) {
        r = 0;
    }

    return r;
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
            if (!isspace((uint8_t)scanner->current[0])) {
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
            if (SOL_UNLIKELY(level < 0)) {
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

SOL_API size_t
sol_json_calculate_escaped_string_len(const char *str)
{
    size_t len = 0;

    SOL_NULL_CHECK(str, 0);

    for (; *str; str++) {
        if (memchr(sol_json_escapable_chars, *str, sizeof(sol_json_escapable_chars)))
            len++;
        len++;
    }
    return len + 1;
}

SOL_API char *
sol_json_escape_string(const char *str, struct sol_buffer *buf)
{
    char *out, *r_str;
    size_t i, escaped_len;
    int r;

    SOL_NULL_CHECK(str, NULL);
    SOL_NULL_CHECK(buf, NULL);

    escaped_len = sol_json_calculate_escaped_string_len(str);

    r = sol_buffer_expand(buf, escaped_len);
    SOL_INT_CHECK(r, < 0, NULL);

    r_str = out = sol_buffer_at_end(buf);

    for (i = 0; *str && i < escaped_len; str++, i++) {
        if (memchr(sol_json_escapable_chars, *str, sizeof(sol_json_escapable_chars))) {
            *out++ = '\\';
            switch (*str) {
            case '"':  *out++ = '"'; break;
            case '\\': *out++ = '\\'; break;
            case '/':  *out++ = '/'; break;
            case '\b': *out++ = 'b'; break;
            case '\f': *out++ = 'f'; break;
            case '\n': *out++ = 'n'; break;
            case '\r': *out++ = 'r'; break;
            case '\t': *out++ = 't'; break;
            }
        } else {
            *out++ = *str;
        }
    }

    *out++ = '\0';
    buf->used += escaped_len - 1;

    return r_str;
}

SOL_API int
sol_json_double_to_str(const double value, struct sol_buffer *buf)
{
    int ret;
    char *decimal_point, *end;
    size_t used;

#ifdef HAVE_LOCALE
    struct lconv *lc = localeconv();
#endif

    SOL_NULL_CHECK(buf, -EINVAL);

    used = buf->used;

    ret = sol_buffer_append_printf(buf, "%g", value);
    SOL_INT_CHECK(ret, < 0, ret);

#ifdef HAVE_LOCALE
    if (lc->decimal_point && streq(lc->decimal_point, "."))
        return 0;

    end = (char *)buf->data + used;

    if ((decimal_point = strstr(end, lc->decimal_point))) {
        size_t decimal_len = strlen(lc->decimal_point);
        size_t offset = decimal_point - (char *)buf->data + 1;
        *decimal_point = '.';

        ret = sol_buffer_remove_data(buf, decimal_len - 1, offset);
        SOL_INT_CHECK(ret, < 0, ret);
    }

#endif

    return 0;
}

SOL_API bool
sol_json_is_valid_type(struct sol_json_scanner *scanner, enum sol_json_type type)
{
    struct sol_json_token token;
    const char *last_position;

    SOL_NULL_CHECK(scanner->mem_end, -EINVAL);

    if (*(scanner->mem_end - 1) == '\0')
        last_position = scanner->mem_end - 1;
    else
        last_position = scanner->mem_end;

    return sol_json_scanner_next(scanner, &token) &&
           sol_json_token_get_type(&token) == type &&
           sol_json_scanner_skip_over(scanner, &token) &&
           token.end == last_position;
}

SOL_API int
sol_json_serialize_string(struct sol_buffer *buffer, const char *str)
{
    int r;

    SOL_NULL_CHECK(buffer, -EINVAL);
    SOL_NULL_CHECK(str, -EINVAL);

    r = sol_buffer_append_char(buffer, '\"');
    SOL_INT_CHECK(r, < 0, r);

    SOL_NULL_CHECK(sol_json_escape_string(str, buffer), -EINVAL);

    r = sol_buffer_append_char(buffer, '\"');
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_json_serialize_double(struct sol_buffer *buffer, double val)
{
    int r;

    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_json_double_to_str(val, buffer);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_json_serialize_int32(struct sol_buffer *buffer, int32_t val)
{
    int r;

    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_buffer_append_printf(buffer, "%" PRId32, val);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_json_serialize_uint32(struct sol_buffer *buffer, uint32_t val)
{
    int r;

    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_buffer_append_printf(buffer, "%" PRIu32, val);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_json_serialize_int64(struct sol_buffer *buffer, int64_t val)
{
    int r;

    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_buffer_append_printf(buffer, "%" PRId64, val);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_json_serialize_uint64(struct sol_buffer *buffer, uint64_t val)
{
    int r;

    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_buffer_append_printf(buffer, "%" PRIu64, val);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_json_serialize_boolean(struct sol_buffer *buffer, bool val)
{
    int r;
    static const struct sol_str_slice t_str = SOL_STR_SLICE_LITERAL("true");
    static const struct sol_str_slice f_str = SOL_STR_SLICE_LITERAL("false");

    SOL_NULL_CHECK(buffer, -EINVAL);

    r = sol_buffer_append_slice(buffer, val ? t_str : f_str);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

#define MAX_BYTES_UNICODE 3

SOL_API int
sol_json_token_get_unescaped_string(const struct sol_json_token *token, struct sol_buffer *buffer)
{
    int r;
    const char *start, *p;
    bool is_escaped = false;
    char new_char;
    int8_t unicode_len;

    SOL_NULL_CHECK(buffer, -EINVAL);
    sol_buffer_init_flags(buffer, NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    SOL_NULL_CHECK(token, -EINVAL);
    SOL_NULL_CHECK(token->start, -EINVAL);
    SOL_NULL_CHECK(token->end, -EINVAL);

    if (*token->start != '"' || *(token->end - 1) != '"')
        goto invalid_json_string;

    for (start = p = token->start + 1; p < token->end - 1; p++) {
        if (!is_escaped && *p == '\\') {
            struct sol_str_slice slice = { .data = start, .len = p - start };

            r = sol_buffer_append_slice(buffer, slice);
            SOL_INT_CHECK_GOTO(r, < 0, error);
            is_escaped = true;
        } else if (is_escaped) {
            is_escaped = false;
            start = p + 1;
            switch (*p) {
            case '\\':
                new_char = '\\';
                break;
            case '/':
                new_char = '/';
                break;
            case '"':
                new_char = '"';
                break;
            case 'b':
                new_char = '\b';
                break;
            case 'r':
                new_char = '\r';
                break;
            case 'n':
                new_char = '\n';
                break;
            case 'f':
                new_char = '\f';
                break;
            case 't':
                new_char = '\t';
                break;
            case 'u':
                if (p + 4 < token->end - 1) {
                    uint8_t n1, n2;
                    void *buffer_end;

                    r = sol_util_base16_decode(&n1, 1,
                        SOL_STR_SLICE_STR(p + 1, 2),
                        SOL_DECODE_BOTH);
                    SOL_INT_CHECK(r, != 1, r);
                    r = sol_util_base16_decode(&n2, 1,
                        SOL_STR_SLICE_STR(p + 3, 2),
                        SOL_DECODE_BOTH);
                    SOL_INT_CHECK(r, != 1, r);
                    if (buffer->used > SIZE_MAX - MAX_BYTES_UNICODE)
                        return -EOVERFLOW;

                    r = sol_buffer_ensure(buffer,
                        buffer->used + MAX_BYTES_UNICODE);
                    SOL_INT_CHECK(r, < 0, r);
                    buffer_end = sol_buffer_at_end(buffer);
                    SOL_NULL_CHECK(buffer_end, -EINVAL);
                    unicode_len = sol_util_utf8_from_unicode_code(buffer_end,
                        MAX_BYTES_UNICODE, n1 << 8 | n2);
                    if (unicode_len < 0)
                        return unicode_len;
                    buffer->used += unicode_len;

                    start += 4;
                    p += 4;
                    continue;
                }
            default:
                goto invalid_json_string;
            }
            r = sol_buffer_append_char(buffer, new_char);
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    }

    if (is_escaped)
        goto invalid_json_string;

    if (start == token->start + 1) {
        sol_buffer_init_flags(buffer, (char *)start, p - start,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        buffer->used = buffer->capacity;
    } else {
        struct sol_str_slice slice = { .data = start, .len = p - start };
        r = sol_buffer_append_slice(buffer, slice);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    return 0;

error:
    sol_buffer_fini(buffer);
    return r;

invalid_json_string:
    SOL_WRN("Invalid JSON string: %.*s", (int)sol_json_token_get_size(token),
        (char *)token->start);
    return -EINVAL;
}

#undef MAX_BYTES_UNICODE

SOL_API char *
sol_json_token_get_unescaped_string_copy(const struct sol_json_token *value)
{
    struct sol_buffer buffer;
    int r;

    r = sol_json_token_get_unescaped_string(value, &buffer);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (buffer.flags & SOL_BUFFER_FLAGS_NO_FREE)
        return strndup(buffer.data, buffer.used);

    buffer.flags = SOL_BUFFER_FLAGS_DEFAULT;
    r = sol_buffer_ensure_nul_byte(&buffer);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return sol_buffer_steal(&buffer, NULL);

error:
    sol_buffer_fini(&buffer);
    return NULL;
}

SOL_API int
sol_json_object_get_value_by_key(struct sol_json_scanner *scanner, const struct sol_str_slice key_slice, struct sol_json_token *value)
{
    struct sol_json_token token, key;
    enum sol_json_loop_reason reason;

    SOL_NULL_CHECK(scanner, -EINVAL);
    SOL_NULL_CHECK(key_slice.data, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    if (sol_json_mem_get_type(scanner->current) != SOL_JSON_TYPE_OBJECT_START)
        return -EINVAL;

    SOL_JSON_SCANNER_OBJECT_LOOP (scanner, &token, &key, value, reason)
        if (sol_json_token_str_eq(&key, key_slice.data, key_slice.len))
            return 0;

    if (reason == SOL_JSON_LOOP_REASON_OK)
        return -ENOENT;
    return -EINVAL;
}

SOL_API int
sol_json_array_get_at_index(struct sol_json_scanner *scanner, uint16_t i, struct sol_json_token *value)
{
    uint16_t cur_index = 0;
    enum sol_json_loop_reason reason;

    SOL_NULL_CHECK(scanner, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    if (sol_json_mem_get_type(scanner->current) != SOL_JSON_TYPE_ARRAY_START)
        return -EINVAL;

    SOL_JSON_SCANNER_ARRAY_LOOP_ALL(scanner, value, reason) {
        if (i == cur_index)
            return 0;

        if (!sol_json_scanner_skip_over(scanner, value))
            return -ENOENT;
        cur_index++;
    }

    if (reason == SOL_JSON_LOOP_REASON_OK)
        return -ENOENT;
    return -EINVAL;
}

SOL_API int
sol_json_path_scanner_init(struct sol_json_path_scanner *scanner, struct sol_str_slice path)
{
    SOL_NULL_CHECK(path.data, -EINVAL);

    scanner->path = path.data;
    scanner->end = path.data + path.len;
    scanner->current = path.data;

    return 0;
}

SOL_API int
sol_json_get_value_by_path(struct sol_json_scanner *scanner, struct sol_str_slice path, struct sol_json_token *value)
{
    enum sol_json_type type;
    struct sol_str_slice key_slice = SOL_STR_SLICE_EMPTY;
    struct sol_buffer current_key;
    struct sol_json_path_scanner path_scanner;
    enum sol_json_loop_reason reason;
    const char *start;
    int32_t index_val;
    bool found;
    int r;

    SOL_NULL_CHECK(scanner, -EINVAL);
    SOL_NULL_CHECK(path.data, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    sol_json_path_scanner_init(&path_scanner, path);
    start = scanner->current;

    SOL_JSON_PATH_FOREACH(path_scanner, key_slice, reason) {
        type = sol_json_mem_get_type(scanner->current);

        switch (type) {
        case SOL_JSON_TYPE_OBJECT_START:
            if (sol_json_path_is_array_key(key_slice))
                return -ENOENT;

            r = json_path_parse_object_key(key_slice, &current_key);
            SOL_INT_CHECK(r, < 0, r);

            found = sol_json_object_get_value_by_key(scanner,
                sol_buffer_get_slice(&current_key), value) == 0;
            sol_buffer_fini(&current_key);
            if (!found)
                return -ENOENT;
            break;
        case SOL_JSON_TYPE_ARRAY_START:
            if (!sol_json_path_is_array_key(key_slice))
                return -ENOENT;

            index_val = sol_json_path_array_get_segment_index(key_slice);
            SOL_INT_CHECK(index_val, < 0, -ENOENT);

            if (sol_json_array_get_at_index(scanner, index_val, value) < 0)
                return -ENOENT;
            break;
        default:
            return -ENOENT;
        }

        scanner->current = value->start;
    }
    if (reason != SOL_JSON_LOOP_REASON_OK)
        return -ENOENT;

    //If path is root
    if (start == scanner->current) {
        value->start = scanner->current;
        value->end = value->start + 1;
    }

    return 0;
}

SOL_API bool
sol_json_path_get_next_segment(struct sol_json_path_scanner *scanner, struct sol_str_slice *slice, enum sol_json_loop_reason *end_reason)
{
    if (scanner->path == scanner->end)
        goto error;

    //Root element
    if (scanner->current == scanner->path) {
        if (scanner->path[0] != '$') {
            *end_reason = SOL_JSON_LOOP_REASON_INVALID;
            return false;
        }
        scanner->current = scanner->path + 1;
    }

    if (scanner->current >= scanner->end) {
        slice->data = scanner->end;
        slice->len = 0;
        return false;
    }

    if (*scanner->current == '[') {
        if (json_path_parse_key_in_brackets(scanner, slice))
            return true;
    } else if (*scanner->current == '.') {
        if (json_path_parse_key_after_dot(scanner, slice))
            return true;
    }

error:
    *end_reason = SOL_JSON_LOOP_REASON_INVALID;
    return false;
}

SOL_API int32_t
sol_json_path_array_get_segment_index(struct sol_str_slice key)
{
    int index_val;
    int r;

    SOL_NULL_CHECK(key.data, -EINVAL);
    SOL_INT_CHECK(key.len, < 3, -EINVAL);

    if (!sol_json_path_is_array_key(key))
        return -EINVAL;

    r = sol_str_slice_to_int(SOL_STR_SLICE_STR(key.data + 1, key.len - 2),
        &index_val);
    SOL_INT_CHECK(r, < 0, r);

    if ((int)(uint16_t)index_val != index_val)
        return -ERANGE;

    return index_val;
}
