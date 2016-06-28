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

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "sol-buffer.h"
#include "sol-macros.h"
#include "sol-util-internal.h"
#include "sol-log.h"
#include "sol-random.h"
#include "sol-str-slice.h"

struct sol_uuid {
    uint8_t bytes[16];
};

#if defined(HAVE_NEWLOCALE) && (defined(HAVE_STRTOD_L) || defined(HAVE_STRFTIME_L))
static locale_t c_locale;
static void
clear_c_locale(void)
{
    freelocale(c_locale);
    c_locale = NULL;
}

static bool
init_c_locale(void)
{
    if (c_locale)
        return true;

    c_locale = newlocale(LC_ALL_MASK, "C", NULL);
    if (!c_locale)
        return false;

    atexit(clear_c_locale);
    return true;
}
#endif

SOL_API void *
sol_util_memdup(const void *data, size_t len)
{
    void *ptr;

    ptr = malloc(len);
    if (ptr)
        memcpy(ptr, data, len);
    return ptr;
}

SOL_API long int
sol_util_strtol_n(const char *nptr, char **endptr, ssize_t len, int base)
{
    char *tmpbuf, *tmpbuf_endptr;
    long int r;

    if (len < 0)
        len = (ssize_t)strlen(nptr);

    tmpbuf = strndupa(nptr, len);
    errno = 0;
    r = strtol(tmpbuf, &tmpbuf_endptr, base);

    if (endptr)
        *endptr = (char *)nptr + (tmpbuf_endptr - tmpbuf);

    return r;
}

SOL_API unsigned long int
sol_util_strtoul_n(const char *nptr, char **endptr, ssize_t len, int base)
{
    char *tmpbuf, *tmpbuf_endptr;
    unsigned long int r;

    if (len < 0)
        len = (ssize_t)strlen(nptr);

    tmpbuf = strndupa(nptr, len);
    errno = 0;
    r = strtoul(tmpbuf, &tmpbuf_endptr, base);

    if (endptr)
        *endptr = (char *)nptr + (tmpbuf_endptr - tmpbuf);
    return r;
}

SOL_API double
sol_util_strtod_n(const char *nptr, char **endptr, ssize_t len, bool use_locale)
{
    char *tmpbuf, *tmpbuf_endptr;
    double value;

#if defined(HAVE_NEWLOCALE) && defined(HAVE_STRTOD_L)
    if (!use_locale) {
        if (!init_c_locale()) {
            /* not great, but a best effort to convert something */
            use_locale = false;
            SOL_WRN("could not create locale 'C', use current locale.");
        }
    }
#endif

    if (len < 0)
        len = (ssize_t)strlen(nptr);

    if (SOL_UNLIKELY(len > (DBL_MANT_DIG - DBL_MIN_EXP + 3))) {
        errno = EINVAL;
        return FP_NAN;
    }

    /* NOTE: Using a copy to ensure trailing \0 and strtod() so we
     * properly parse numbers with large precision.
     *
     * Since parsing it is complex (ie:
     * http://www.netlib.org/fp/dtoa.c), we take the short path to
     * call libc.
     */
    tmpbuf = strndupa(nptr, len);

    errno = 0;
#ifdef HAVE_NEWLOCALE
    if (!use_locale) {
#ifdef HAVE_STRTOD_L
        value = strtod_l(tmpbuf, &tmpbuf_endptr, c_locale);
#else
        /* fallback to query locale's decimal point and if it's
         * different than '.' we replace JSON's '.' with locale's
         * decimal point if the given number contains a '.'.
         *
         * We also replace any existing locale decimal_point with '.'
         * so it will return correct endptr.
         *
         * Extra care since decimal point may be multi-byte.
         */
        struct lconv *lc = localeconv();
        if (lc && lc->decimal_point && !streq(lc->decimal_point, ".")) {
            if (strchr(tmpbuf, '.') || strstr(tmpbuf, lc->decimal_point)) {
                int dplen = strlen(lc->decimal_point);
                const char *src, *src_end = tmpbuf + len;
                char *dst;
                if (dplen == 1) {
                    for (src = tmpbuf, dst = tmpbuf; src < src_end; src++, dst++) {
                        if (*src == '.')
                            *dst = *lc->decimal_point;
                        else if (*src == *lc->decimal_point)
                            *dst = '.';
                    }
                } else {
                    char *transl;
                    unsigned count = 0;

                    for (src = tmpbuf; src < src_end; src++) {
                        if (*src == '.')
                            count++;
                    }

                    transl = alloca(len + (count * dplen) + 1);
                    for (src = tmpbuf, dst = transl; src < src_end;) {
                        if (*src == '.') {
                            memcpy(dst, lc->decimal_point, dplen);
                            dst += dplen;
                            src++;
                        } else if ((src_end - src) >= dplen &&
                            streqn(src, lc->decimal_point, dplen)) {
                            *dst = '.';
                            dst++;
                            src += dplen;
                        } else {
                            *dst = *src;
                            dst++;
                            src++;
                        }
                    }
                    *dst = '\0';
                    tmpbuf = transl;
                }
            }
        }
        value = strtod(tmpbuf, &tmpbuf_endptr);
#endif
    } else
#endif
    {
        value = strtod(tmpbuf, &tmpbuf_endptr);
    }

    if (endptr)
        *endptr = (char *)nptr + (tmpbuf_endptr - tmpbuf);

    return value;
}

SOL_API char *
sol_util_strerror(int errnum, struct sol_buffer *buf)
{
    char *ret;
    int err;

    SOL_NULL_CHECK(buf, NULL);

#define CHUNK_SIZE (512)

#ifdef HAVE_XSI_STRERROR_R
    /* XSI-compliant strerror_r returns int */
    while (1) {
        errno = 0;
        ret = sol_buffer_at_end(buf);
        err = strerror_r(errnum, ret, buf->capacity - buf->used);

        if (err == ERANGE || (err < 0 && errno == ERANGE)) {
            err = sol_buffer_expand(buf, CHUNK_SIZE);
            SOL_INT_CHECK(err, < 0, NULL);
        } else if (err != 0)
            return NULL;
        else
            break;
    }
    buf->used += strlen(ret);
#else

    /* GNU libc version of strerror_r returns char* */
    while (1) {
        ret = strerror_r(errnum, sol_buffer_at_end(buf), buf->capacity - buf->used);

        if (!ret) {
            err = sol_buffer_expand(buf, CHUNK_SIZE);
            SOL_INT_CHECK(err, < 0, NULL);
        } else
            break;
    }

    {
        size_t len;

        len = strlen(ret);

        if (buf->capacity - buf->used < len) {
            err = sol_buffer_expand(buf, len);
            SOL_INT_CHECK(err, < 0, NULL);
        }

        err = sol_buffer_append_bytes(buf, (const uint8_t *)ret, len);
        SOL_INT_CHECK(err, < 0, NULL);
    }
#endif

    return ret;

#undef CHUNCK_SIZE
}

static struct sol_uuid
assert_uuid_v4(struct sol_uuid id)
{
    id.bytes[6] = (id.bytes[6] & 0x0F) | 0x40;
    id.bytes[8] = (id.bytes[8] & 0x3F) | 0x80;

    return id;
}

static int
uuid_gen(struct sol_uuid *ret)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(ret, sizeof(*ret),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    struct sol_random *engine;
    ssize_t size;

    SOL_NULL_CHECK(ret, -EINVAL);
    engine = sol_random_new(SOL_RANDOM_DEFAULT, 0);
    SOL_NULL_CHECK(engine, -errno);

    size = sol_random_fill_buffer(engine, &buf, sizeof(*ret));
    sol_random_del(engine);
    sol_buffer_fini(&buf);

    if (size != (ssize_t)sizeof(*ret))
        return -EIO;

    *ret = assert_uuid_v4(*ret);
    return 0;
}

SOL_API int
sol_util_uuid_string_from_bytes(bool uppercase, bool with_hyphens, const uint8_t uuid_bytes[SOL_STATIC_ARRAY_SIZE(16)], struct sol_buffer *uuid_str)
{
    static struct sol_str_slice hyphen = SOL_STR_SLICE_LITERAL("-");
    const struct sol_str_slice uuid = SOL_STR_SLICE_STR((char *)uuid_bytes, 16);
    /* hyphens on positions 8, 13, 18, 23 (from 0) */
    static const int hyphens_pos[] = { 8, 13, 18, 23 };
    unsigned i;
    int r;

    SOL_NULL_CHECK(uuid_str, -EINVAL);

    r = sol_buffer_append_as_base16(uuid_str, uuid, uppercase);
    SOL_INT_CHECK(r, < 0, r);

    if (with_hyphens) {
        for (i = 0; i < sol_util_array_size(hyphens_pos); i++) {
            r = sol_buffer_insert_slice(uuid_str, hyphens_pos[i], hyphen);
            SOL_INT_CHECK(r, < 0, r);
        }
    }

    return 0;
}

SOL_API int
sol_util_uuid_bytes_from_string(struct sol_str_slice uuid_str, struct sol_buffer *uuid_bytes)
{
    /* hyphens on positions 8, 13, 18, 23 (from 0) */
    static const int slices[] = { -1, 8, 13, 18, 23, 36 };
    int r;
    unsigned i;

    SOL_NULL_CHECK(uuid_bytes, -EINVAL);

    if (!sol_util_uuid_str_is_valid(uuid_str))
        return -EINVAL;

    if (uuid_str.len == 32)
        return sol_buffer_append_from_base16(uuid_bytes, uuid_str, SOL_DECODE_BOTH);

    for (i = 1; i < sol_util_array_size(slices); i++) {
        uuid_str.len = slices[i] - slices[i - 1] - 1;
        r = sol_buffer_append_from_base16(uuid_bytes, uuid_str, SOL_DECODE_BOTH);
        SOL_INT_CHECK(r, < 0, r);
        uuid_str.data += uuid_str.len + 1;
    }

    return 0;
}

SOL_API int
sol_util_uuid_gen(bool uppercase, bool with_hyphens, struct sol_buffer *uuid_buf)
{
    int r;
    struct sol_uuid uuid = { { 0 } };

    SOL_NULL_CHECK(uuid_buf, -EINVAL);

    r = uuid_gen(&uuid);
    SOL_INT_CHECK(r, < 0, r);

    return sol_util_uuid_string_from_bytes(uppercase, with_hyphens, uuid.bytes,
        uuid_buf);
}

SOL_API int
sol_util_replace_str_if_changed(char **str, const char *new_str)
{
    struct sol_str_slice slice = SOL_STR_SLICE_EMPTY;

    if (new_str)
        slice = sol_str_slice_from_str(new_str);

    return sol_util_replace_str_from_slice_if_changed(str, slice);
}

SOL_API int
sol_util_replace_str_from_slice_if_changed(char **str,
    const struct sol_str_slice slice)
{
    SOL_NULL_CHECK(str, -EINVAL);

    if (!slice.len) {
        if (!*str)
            return 0;
        free(*str);
        *str = NULL;
        return 1;
    }

    if (*str) {
        char *tmp = *str;
        size_t str_len = strlen(*str);

        if (str_len == slice.len && memcmp(*str, slice.data, str_len) == 0) {
            return 0;
        } else if (str_len < slice.len) {
            tmp = realloc(*str, slice.len + 1);
            SOL_NULL_CHECK(tmp, -ENOMEM);
        }
        memcpy(tmp, slice.data, slice.len);
        tmp[slice.len] = '\0';
        *str = tmp;
    } else {
        *str = sol_str_slice_to_str(slice);
        SOL_NULL_CHECK(*str, -ENOMEM);
    }

    return 1;
}

SOL_API ssize_t
sol_util_base64_encode(void *buf, size_t buflen, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)])
{
    char *output;
    const uint8_t *input;
    size_t req_len;
    size_t i, o;
    uint8_t c;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(slice.data, -EINVAL);

    if (slice.len == 0)
        return 0;

    req_len = sol_util_base64_calculate_encoded_len(slice, base64_map);
    SOL_INT_CHECK(buflen, < req_len, -ENOMEM);

    input = (const uint8_t *)slice.data;
    output = buf;

    for (i = 0, o = 0; i + 3 <= slice.len; i += 3) {
        c = (input[i] & (((1 << 6) - 1) << 2)) >> 2;
        output[o++] = base64_map[c];

        c = (input[i] & ((1 << 2) - 1)) << 4;
        c |= (input[i + 1] & (((1 << 4) - 1) << 4)) >> 4;
        output[o++] = base64_map[c];

        c = (input[i + 1] & ((1 << 4) - 1)) << 2;
        c |= (input[i + 2] & (((1 << 2) - 1) << 6)) >> 6;
        output[o++] = base64_map[c];

        c = input[i + 2] & ((1 << 6) - 1);
        output[o++] = base64_map[c];
    }

    if (i + 1 == slice.len) {
        c = (input[i] & (((1 << 6) - 1) << 2)) >> 2;
        output[o++] = base64_map[c];

        c = (input[i] & ((1 << 2) - 1)) << 4;
        output[o++] = base64_map[c];

        output[o++] = base64_map[64];
        output[o++] = base64_map[64];
    } else if (i + 2 == slice.len) {
        c = (input[i] & (((1 << 6) - 1) << 2)) >> 2;
        output[o++] = base64_map[c];

        c = (input[i] & ((1 << 2) - 1)) << 4;
        c |= (input[i + 1] & (((1 << 4) - 1) << 4)) >> 4;
        output[o++] = base64_map[c];

        c = (input[i + 1] & ((1 << 4) - 1)) << 2;
        output[o++] = base64_map[c];

        output[o++] = base64_map[64];
    }

    return o;
}

static inline uint8_t
base64_index_of(char c, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)])
{
    const char *p = memchr(base64_map, c, 65);

    if (SOL_UNLIKELY(!p))
        return UINT8_MAX;
    return p - base64_map;
}

SOL_API ssize_t
sol_util_base64_decode(void *buf, size_t buflen, const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)])
{
    uint8_t *output;
    const char *input;
    size_t i, o, req_len;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(slice.data, -EINVAL);

    if (slice.len == 0)
        return 0;

    req_len = sol_util_base64_calculate_decoded_len(slice, base64_map);
    SOL_INT_CHECK(buflen, < req_len, -ENOMEM);

    input = slice.data;
    output = buf;

    for (i = 0, o = 0; i + 4 <= slice.len; i += 4) {
        uint8_t _6bits[4];
        uint8_t n;

        /* precomputing reverse table would make lookup faster, but would need
         * a setup that is potentially longer than the time we use during lookup
         * as well as the need to allocate memory for the reverse table.
         */
        for (n = 0; n < 4; n++) {
            _6bits[n] = base64_index_of(input[i + n], base64_map);
            SOL_INT_CHECK(_6bits[n], == UINT8_MAX, -EINVAL);
        }

        output[o++] = (_6bits[0] << 2) | ((_6bits[1] & (((1 << 4) - 1) << 4)) >> 4);
        if (_6bits[2] != 64) {
            output[o++] = ((_6bits[1] & ((1 << 4) - 1)) << 4) | ((_6bits[2] & (((1 << 4) - 1) << 2)) >> 2);
            if (_6bits[3] != 64) {
                output[o++] = ((_6bits[2] & ((1 << 2) - 1)) << 6) | _6bits[3];
            }
        }
    }

    SOL_INT_CHECK(i, != slice.len, -EINVAL);

    return o;
}

static inline char
base16_encode_digit(const uint8_t nibble, const char a)
{
    if (SOL_LIKELY(nibble < 10))
        return '0' + nibble;
    return a + (nibble - 10);
}

SOL_API ssize_t
sol_util_base64_calculate_decoded_len(const struct sol_str_slice slice, const char base64_map[SOL_STATIC_ARRAY_SIZE(65)])
{
    size_t req_len = (slice.len / 4) * 3;
    size_t i;

    for (i = slice.len; i > 0; i--) {
        if (slice.data[i - 1] != base64_map[64])
            break;
        req_len--;
    }
    if (req_len > SSIZE_MAX)
        return -EOVERFLOW;
    return req_len;
}

SOL_API ssize_t
sol_util_base16_encode(void *buf, size_t buflen, const struct sol_str_slice slice, bool uppercase)
{
    char *output, a;
    const uint8_t *input;
    size_t req_len;
    size_t i, o;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(slice.data, -EINVAL);

    if (slice.len == 0)
        return 0;

    req_len = sol_util_base16_calculate_encoded_len(slice);
    SOL_INT_CHECK(buflen, < req_len, -ENOMEM);

    input = (const uint8_t *)slice.data;
    output = buf;
    a = uppercase ? 'A' : 'a';

    for (i = 0, o = 0; i < slice.len; i++) {
        const uint8_t b = input[i];
        const uint8_t nibble[2] = {
            (b & 0xf0) >> 4,
            (b & 0x0f)
        };
        uint8_t n;
        for (n = 0; n < 2; n++)
            output[o++] = base16_encode_digit(nibble[n], a);
    }

    return o;
}

static inline uint8_t
base16_decode_digit(const char digit, const char a, const char f, const char A, const char F)
{
    if (SOL_LIKELY('0' <= digit && digit <= '9'))
        return digit - '0';
    else if (a <= digit && digit <= f)
        return 10 + (digit - a);
    else if (A != a && A <= digit && digit <= F)
        return 10 + (digit - A);
    else
        return UINT8_MAX;
}

SOL_API ssize_t
sol_util_base16_decode(void *buf, size_t buflen, const struct sol_str_slice slice, enum sol_decode_case decode_case)
{
    uint8_t *output;
    const char *input;
    char a, f, A, F;
    size_t i, o, req_len;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(slice.data, -EINVAL);

    if (slice.len == 0)
        return 0;

    req_len = sol_util_base16_calculate_decoded_len(slice);
    SOL_INT_CHECK(buflen, < req_len, -ENOMEM);

    input = slice.data;
    output = buf;
    a = decode_case == SOL_DECODE_UPPERCASE ? 'A' : 'a';
    f = a + 5;
    A = decode_case == SOL_DECODE_BOTH ? 'A' : a;
    F = A + 5;

    for (i = 0, o = 0; i + 2 <= slice.len; i += 2) {
        uint8_t n, b = 0;
        for (n = 0; n < 2; n++) {
            const uint8_t c = input[i + n];
            uint8_t nibble = base16_decode_digit(c, a, f, A, F);
            if (SOL_UNLIKELY(nibble == UINT8_MAX)) {
                SOL_WRN("Invalid base16 char %c, index: %zd", c, i + n);
                return -EINVAL;
            }
            if (n == 0)
                b |= nibble << 4;
            else
                b |= nibble;
        }
        output[o++] = b;
    }

    SOL_INT_CHECK(i, != slice.len, -EINVAL);

    return o;
}

SOL_API int8_t
sol_util_utf8_from_unicode_code(uint8_t *buf, size_t buf_len, uint32_t unicode_code)
{
    uint8_t b;
    uint8_t len = 0;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(buf_len, == 0, -EINVAL);

    if (unicode_code > 0x10FFFF)
        return -EINVAL;

    if (unicode_code < 0x80) {
        //Just one byte
        b = unicode_code & 0xFF;
        buf[len++] = b;
    } else if (unicode_code >= 0x0800) {
        if (unicode_code > 0xFFFF) {
            //Four bytes
            if (buf_len < 4)
                return -EINVAL;
            buf[len++] = 0xF0;
            buf[len++] = 0x90;
        } else {
            //Three bytes
            if (buf_len < 3)
                return -EINVAL;
            b = 0xe0;
            b |= (unicode_code & 0xF000) >> 12;
            buf[len++] = b;
        }

        b = 0x80;
        b |= (unicode_code & 0x0F00) >> 6;
        b |= (unicode_code & 0xC0) >> 6;
        buf[len++] = b;

        b = 0x80;
        b |= (unicode_code & 0x3F);
        buf[len++] = b;
    } else {
        //Two bytes
        if (buf_len < 2)
            return -EINVAL;
        b = 0xc0;
        b |= (unicode_code & 0x700) >> 6;
        b |= ((0xC0 & unicode_code) >> 6);
        buf[len++] = b;

        b = 0x80 | (unicode_code & 0x3F);
        buf[len++] = b;
    }

    return len;
}

static inline bool
valid_utf8_byte(const uint8_t byte)
{
    return (byte & 0xC0) == 0x80;
}

SOL_API int32_t
sol_util_unicode_code_from_utf8(const uint8_t *buf, size_t buf_len, uint8_t *bytes_read)
{
    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_INT_CHECK(buf_len, == 0, -EINVAL);

    if (buf[0] < 0x80) {
        if (bytes_read)
            *bytes_read = 1;
        return buf[0];
    }

    if (buf[0] < 0xE0) {
        if (buf_len < 2 || !valid_utf8_byte(buf[1]))
            goto error;
        if (bytes_read)
            *bytes_read = 2;
        return ((buf[0] & 0x1F) << 6) | (buf[1] & 0x3F);
    }

    if (buf[0] < 0xF0) {
        if (buf_len < 3 || !valid_utf8_byte(buf[1]) ||
            !valid_utf8_byte(buf[2]))
            goto error;
        if (bytes_read)
            *bytes_read = 3;
        return ((buf[0] & 0x0F) << 12) | ((buf[1] & 0x3F) << 6) |
               (buf[2] & 0x3F);
    }

    if (buf[0] == 0xF0 && buf[1] == 0x90) {
        if (buf_len < 4 || !valid_utf8_byte(buf[2]) ||
            !valid_utf8_byte(buf[3]))
            goto error;
        if (bytes_read)
            *bytes_read = 4;
        return 0x10000 | ((buf[2] & 0x3F) << 6) | (buf[3] & 0x3F);
    }

error:
    SOL_WRN("Invalid unicode character in buffer");
    return -EINVAL;
}

SOL_API int
sol_util_ssize_mul(ssize_t op1, ssize_t op2, ssize_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(op1, op2, out))
        return -EOVERFLOW;
#else
    bool overflow = false;

    if (op1 > 0 && op2 > 0) {
        overflow = op1 > OVERFLOW_SSIZE_T_POS / op2;
    } else if (op1 > 0 && op2 <= 0) {
        overflow = op2 < OVERFLOW_SSIZE_T_NEG / op1;
    } else if (op1 <= 0 && op2 > 0) {
        overflow = op1 < OVERFLOW_SSIZE_T_NEG / op2;
    } else { // op1 <= 0 && op2 <= 0
        overflow = op1 != 0 && op2 < OVERFLOW_SSIZE_T_POS / op1;
    }

    if (overflow)
        return -EOVERFLOW;

    *out = op1 * op2;
#endif
    return 0;
}

SOL_API int
sol_util_size_mul(size_t elem_size, size_t num_elems, size_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(elem_size, num_elems, out))
        return -EOVERFLOW;
#else
    if ((elem_size >= OVERFLOW_SIZE_T || num_elems >= OVERFLOW_SIZE_T) &&
        elem_size > 0 && SIZE_MAX / elem_size < num_elems)
        return -EOVERFLOW;
    *out = elem_size * num_elems;
#endif
    return 0;
}

SOL_API int
sol_util_size_add(size_t a, size_t b, size_t *out)
{
#ifdef HAVE_BUILTIN_ADD_OVERFLOW
    if (__builtin_add_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if (a > 0 && b > SIZE_MAX - a)
        return -EOVERFLOW;
    *out = a + b;
#endif
    return 0;
}

SOL_API int
sol_util_size_sub(size_t a, size_t b, size_t *out)
{
#ifdef HAVE_BUILTIN_SUB_OVERFLOW
    if (__builtin_sub_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if (b > a)
        return -EOVERFLOW;
    *out = a - b;
#endif
    return 0;
}

SOL_API int
sol_util_uint64_mul(uint64_t a, uint64_t b, uint64_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if ((a >= OVERFLOW_UINT64 || b >= OVERFLOW_UINT64) &&
        a > 0 && UINT64_MAX / a < b)
        return -EOVERFLOW;
    *out = a * b;
#endif
    return 0;
}

SOL_API int
sol_util_int64_mul(int64_t a, int64_t b, int64_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if ((a == INT64_MAX && b == -1) ||
        (a > 0 && b > 0 && (a > INT64_MAX / b)) ||
        (a < 0 && b < 0 && (a < INT64_MAX / b)) ||
        (a > 0 && b < -1 && (a > INT64_MIN / b)) ||
        (a < 0 && b > 0 && (a < INT64_MIN / b)))
        return -EOVERFLOW;
    *out = a * b;
#endif
    return 0;
}

SOL_API int
sol_util_uint64_add(uint64_t a, uint64_t b, uint64_t *out)
{
#ifdef HAVE_BUILTIN_ADD_OVERFLOW
    if (__builtin_add_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if (a > 0 && b > UINT64_MAX - a)
        return -EOVERFLOW;
    *out = a + b;
#endif
    return 0;
}

SOL_API int
sol_util_int32_mul(int32_t a, int32_t b, int32_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if ((a == INT32_MAX && b == -1) ||
        (a > 0 && b > 0 && (a > INT32_MAX / b)) ||
        (a < 0 && b < 0 && (a < INT32_MAX / b)) ||
        (a > 0 && b < -1 && (a > INT32_MIN / b)) ||
        (a < 0 && b > 0 && (a < INT32_MIN / b)))
        return -EOVERFLOW;
    *out = a * b;
#endif
    return 0;
}

SOL_API int
sol_util_uint32_mul(uint32_t a, uint32_t b, uint32_t *out)
{
#ifdef HAVE_BUILTIN_MUL_OVERFLOW
    if (__builtin_mul_overflow(a, b, out))
        return -EOVERFLOW;
#else
    if ((a >= OVERFLOW_UINT32 || b >= OVERFLOW_UINT32) &&
        a > 0 && UINT32_MAX / a < b)
        return -EOVERFLOW;
    *out = a * b;
#endif
    return 0;
}

SOL_API bool
sol_util_uuid_str_is_valid(const struct sol_str_slice uuid)
{
    size_t i;

    if (uuid.len == 32) {
        for (i = 0; i < uuid.len; i++) {
            if (!isxdigit((uint8_t)uuid.data[i]))
                return false;
        }
    } else if (uuid.len == 36) {
        char c;
        for (i = 0; i < uuid.len; i++) {
            c = uuid.data[i];

            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-')
                    return false;
            } else if (!isxdigit((uint8_t)c))
                return false;
        }
    } else
        return false;

    return true;
}

SOL_API int
sol_util_unescape_quotes(const struct sol_str_slice slice,
    struct sol_buffer *buf)
{
    bool is_escaped = false;
    size_t i, last_append;
    char *quote_start, *quote_end, *txt_start, *txt_end, *quote_middle;
    int r;

    SOL_NULL_CHECK(buf, -EINVAL);

    sol_buffer_init(buf);

    if (!slice.len)
        return 0;

    last_append = 0;
    quote_start = quote_end = txt_start = txt_end = quote_middle = NULL;

    for (i = 0; i < slice.len; i++) {
        int is_space = isspace((int)slice.data[i]);

        if (!is_space)
            txt_end = (char *)slice.data + i;

        if (!is_escaped && (slice.data[i] == '"' || slice.data[i] == '\'')) {
            if (quote_middle && *quote_middle == slice.data[i]) {
                size_t len;
                len = (quote_middle - (txt_start + last_append));
                r = sol_buffer_append_slice(buf,
                    SOL_STR_SLICE_STR(txt_start + last_append, len));
                SOL_INT_CHECK_GOTO(r, < 0, err_exit);
                len = ((char *)slice.data + i) - quote_middle - 1;
                r = sol_buffer_append_slice(buf,
                    SOL_STR_SLICE_STR(quote_middle + 1, len));
                SOL_INT_CHECK_GOTO(r, < 0, err_exit);
                last_append = i + 1;
                quote_middle = NULL;
            } else if (!quote_start) {
                if (i > 0 && txt_start) {
                    //The is slice is something like: MySlice 'WithQuotesInTheMiddle' MySlice continue...
                    quote_middle = (char *)slice.data + i;
                } else {
                    quote_start = (char *)slice.data + i;
                    if (!txt_start)
                        txt_start = quote_start + 1;
                }
            } else if (quote_start && *quote_start == slice.data[i]) {
                txt_end = quote_end = (char *)slice.data + i;
            }
        } else if (!is_escaped && slice.data[i] == '\\') {
            is_escaped = true;
            if (!txt_start)
                continue;
            r = sol_buffer_append_slice(buf,
                SOL_STR_SLICE_STR(slice.data + last_append, i - last_append));
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        } else if (!is_escaped && !txt_start && !is_space) {
            txt_start = (char *)slice.data + i;
        } else if (is_escaped) {
            char c;

            is_escaped = false;
            switch (slice.data[i]) {
            case '\'':
                c = '\'';
                break;
            case '"':
                c = '"';
                break;
            default:
                SOL_WRN("Invalid character to be escapted: '%c'",
                    slice.data[i]);
                r = -EINVAL;
                goto err_exit;
            }

            r = sol_buffer_append_char(buf, c);
            SOL_INT_CHECK_GOTO(r, < 0, err_exit);
            last_append = i + 1;
        }
    }

    if (quote_start && !quote_end) {
        r = -EINVAL;
        SOL_WRN("Missing quotes from slice: %.*s",
            SOL_STR_SLICE_PRINT(slice));
        goto err_exit;
    }

    if (is_escaped) {
        SOL_WRN("Invalid string format, missing character to be escapted."
            " String: %.*s", SOL_STR_SLICE_PRINT(slice));
        r = -EINVAL;
        goto err_exit;
    }

    if (!last_append) {
        size_t len = 0;

        if (txt_start && txt_start == txt_end && !isspace((int)*txt_start))
            len = 1;
        else if (txt_start != txt_end) {
            len = txt_end - txt_start + 1;
            if (quote_end)
                len--;
        }

        if (len > 0) {
            sol_buffer_init_flags(buf, txt_start, len,
                SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
            buf->used = buf->capacity;
        } else {
            buf->flags |= SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED;
        }
    } else {
        size_t len = slice.len - (last_append + (((char *)slice.data + slice.len) - txt_end) - 1);

        r = sol_buffer_append_slice(buf,
            SOL_STR_SLICE_STR(slice.data + last_append,
            len));
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    }

    return 0;

err_exit:
    sol_buffer_fini(buf);
    return r;
}

SOL_API bool
sol_util_double_eq(double var0, double var1)
{
    double abs_var0, abs_var1, diff;

    diff = fabs(var0 - var1);

    /* when a or b are close to zero relative error isn't meaningful -
     * it handles subnormal case */
    if (fpclassify(var0) == FP_ZERO || fpclassify(var1) == FP_ZERO ||
        isless(diff, DBL_MIN)) {
        return isless(diff, (DBL_EPSILON * DBL_MIN));
    }

    /* use relative error for other cases */
    abs_var0 = fabs(var0);
    abs_var1 = fabs(var1);

    return isless(diff / fmin((abs_var0 + abs_var1), DBL_MAX), DBL_EPSILON);
}

ssize_t
sol_util_strftime(struct sol_buffer *buf, const char *format,
    const struct tm *timeptr, bool use_locale)
{
    size_t used;

    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(format, -EINVAL);
    SOL_NULL_CHECK(timeptr, -EINVAL);

#if defined(HAVE_NEWLOCALE) && defined(HAVE_STRFTIME_L)
    if (!use_locale) {
        if (!init_c_locale()) {
            int r = -errno;
            SOL_WRN("Could not init the 'C' locale");
            return r;
        }

        used = strftime_l(sol_buffer_at_end(buf),
            buf->capacity - buf->used, format, timeptr, c_locale);
        buf->used += used;
        return used;
    }
#endif

    /**
       Even with SOL_ATTR_STRFTIME() GCC still warns that the format parameter was not checked.
       This is a known GCC bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=39438
       This cast was suggested in order to make the compiler happy.
     */
    used = ((size_t (*)(char *, size_t, const char *, const struct tm *))strftime)
            (sol_buffer_at_end(buf),
            buf->capacity - buf->used, format, timeptr);
    buf->used += used;
    return used;
}
