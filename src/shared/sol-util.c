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
#include "sol-util.h"
#include "sol-log.h"
#include "sol-random.h"
#include "sol-str-slice.h"

struct sol_uuid {
    uint8_t bytes[16];
};

#if defined(HAVE_NEWLOCALE) && defined(HAVE_STRTOD_L)
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

void *
sol_util_memdup(const void *data, size_t len)
{
    void *ptr;

    ptr = malloc(len);
    if (ptr)
        memcpy(ptr, data, len);
    return ptr;
}

long int
sol_util_strtol(const char *nptr, char **endptr, ssize_t len, int base)
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

double
sol_util_strtodn(const char *nptr, char **endptr, ssize_t len, bool use_locale)
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

    if (unlikely(len > (DBL_MANT_DIG - DBL_MIN_EXP + 3))) {
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

char *
sol_util_strerror(int errnum, char *buf, size_t buflen)
{
    char *ret;

    if (buflen < 1)
        return NULL;

    buf[0] = '\0';

    ret = (char *)strerror_r(errnum, buf, buflen);
    /* if buf was used it means it can be XSI version (so ret won't be
       pointing to msg string), or GNU version using non static string
       (in this case ret == buf already) */
    if (buf[0] != '\0')
        ret = buf;

    return ret;
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

// 37 = 2 * 16 (chars) + 4 (hyphens) + 1 (\0)
int
sol_util_uuid_gen(bool upcase,
    bool with_hyphens,
    char id[SOL_STATIC_ARRAY_SIZE(37)])
{
    static struct sol_str_slice hyphen = SOL_STR_SLICE_LITERAL("-");
    /* hyphens on positions 8, 13, 18, 23 (from 0) */
    static const int hyphens_pos[] = { 8, 13, 18, 23 };
    struct sol_uuid uuid = { { 0 } };
    unsigned i;
    int r;

    struct sol_buffer buf = { 0 };

    sol_buffer_init_flags(&buf, id, 37,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    r = uuid_gen(&uuid);
    SOL_INT_CHECK(r, < 0, r);

    for (i = 0; i < ARRAY_SIZE(uuid.bytes); i++) {
        r = sol_buffer_append_printf(&buf, upcase ? "%02hhX" : "%02hhx",
            uuid.bytes[i]);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    if (with_hyphens) {
        for (i = 0; i < ARRAY_SIZE(hyphens_pos); i++) {
            r = sol_buffer_insert_slice(&buf, hyphens_pos[i], hyphen);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
    }

err:
    sol_buffer_fini(&buf);
    return r;
}

int
sol_util_replace_str_if_changed(char **str, const char *new_str)
{
    struct sol_str_slice slice = SOL_STR_SLICE_EMPTY;

    if (new_str)
        slice = sol_str_slice_from_str(new_str);

    return sol_util_replace_str_from_slice_if_changed(str, slice);
}

int
sol_util_replace_str_from_slice_if_changed(char **str,
    const struct sol_str_slice slice)
{
    SOL_NULL_CHECK(str, -EINVAL);

    if (!slice.len) {
        free(*str);
        *str = NULL;
        return 0;
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
        *str = sol_str_slice_to_string(slice);
        SOL_NULL_CHECK(*str, -ENOMEM);
    }

    return 0;
}

ssize_t
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

    if (unlikely(!p))
        return UINT8_MAX;
    return p - base64_map;
}

ssize_t
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
    if (likely(nibble < 10))
        return '0' + nibble;
    return a + (nibble - 10);
}

ssize_t
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
    if (likely('0' <= digit && digit <= '9'))
        return digit - '0';
    else if (a <= digit && digit <= f)
        return 10 + (digit - a);
    else if (A != a && A <= digit && digit <= F)
        return 10 + (digit - A);
    else
        return UINT8_MAX;
}

ssize_t
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
    a = decode_case == SOL_DECODE_UPERCASE ? 'A' : 'a';
    f = a + 5;
    A = decode_case == SOL_DECODE_BOTH ? 'A' : a;
    F = A + 5;

    for (i = 0, o = 0; i + 2 <= slice.len; i += 2) {
        uint8_t n, b = 0;
        for (n = 0; n < 2; n++) {
            const uint8_t c = input[i + n];
            uint8_t nibble = base16_decode_digit(c, a, f, A, F);
            if (unlikely(nibble == UINT8_MAX)) {
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

int8_t
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

int32_t
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
