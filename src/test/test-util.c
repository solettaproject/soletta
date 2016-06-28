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
#include <limits.h>
#include <stdbool.h>
#include <float.h>
#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "sol-util-internal.h"
#include "sol-log.h"

#include "test.h"


DEFINE_TEST(test_align_power2);

static void
test_align_power2(void)
{
    unsigned int i;

    static const struct {
        unsigned int input;
        unsigned int output;
    } table[] = {
        { 0, 0 },
        { 1, 1 },
        { 2, 2 },
        { 3, 4 },
        { 4, 4 },
        { 5, 8 },
        { 6, 8 },
        { 7, 8 },
        { 8, 8 },
        { 15, 16 },
        { 16, 16 },
        { 17, 32 },
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        unsigned int actual;
        actual = align_power2(table[i].input);

        if (actual != table[i].output) {
            fprintf(stderr, "Error calling align_power2(%u), got %u but expected %u\n",
                table[i].input, actual, table[i].output);
            ASSERT(false);
        }
    }
}


DEFINE_TEST(test_size_mul);

static void
test_size_mul(void)
{
    const size_t half_size = SIZE_MAX / 2;
    const size_t half_double_size = (SIZE_MAX % 2) ? (SIZE_MAX - 1) : SIZE_MAX;
    size_t out;

    ASSERT(sol_util_size_mul(half_size, 2, &out) == 0);
    ASSERT(out == half_double_size);

    ASSERT_INT_EQ(sol_util_size_mul(half_size, 4, &out), -EOVERFLOW);
}

DEFINE_TEST(test_ssize_mul);

static void
test_ssize_mul(void)
{
    ssize_t half_ssize = SSIZE_MAX / 2;
    ssize_t half_double_ssize = (SSIZE_MAX % 2) ?
        (SSIZE_MAX - 1) : SSIZE_MAX;
    ssize_t out;

    ASSERT(sol_util_ssize_mul(half_ssize, 2, &out) == 0);
    ASSERT(out == half_double_ssize);

    ASSERT_INT_EQ(sol_util_ssize_mul(half_ssize, 4, &out), -EOVERFLOW);

    half_ssize *= -1;
    half_double_ssize *= -1;
    ASSERT(sol_util_ssize_mul(half_ssize, 2, &out) == 0);
    ASSERT(out == half_double_ssize);

    ASSERT_INT_EQ(sol_util_ssize_mul(half_ssize, 4, &out), -EOVERFLOW);
}

DEFINE_TEST(test_strtodn);

static void
test_strtodn(void)
{
#ifdef HAVE_LOCALE
    char *oldloc;
    const char *comma_locales[] = {
        "pt", "pt_BR", "de", "it", "ru", NULL
    };
    const char *comma_locale;
#endif
    char dbl_max_str[256], neg_dbl_max_str[256];
    char dbl_max_str_overflow[256], neg_dbl_max_str_overflow[256];
    const struct test {
        const char *str;
        double reference;
        int expected_errno;
        bool use_locale;
        int endptr_offset;
    } *itr, tests[] = {
        { "0", 0.0, 0, false, -1 },
        { "123", 123.0, 0, false, -1 },
        { "1.0", 1.0, 0, false, -1 },
        { "123.456", 123.456, 0, false, -1 },
        { "345e+12", 345e12, 0, false, -1 },
        { "345e-12", 345e-12, 0, false, -1 },
        { "345E+12", 345e12, 0, false, -1 },
        { "345E-12", 345e-12, 0, false, -1 },
        { "-1.0", -1.0, 0, false, -1 },
        { "-123.456", -123.456, 0, false, -1 },
        { "-345e+12", -345e12, 0, false, -1 },
        { "-345e-12", -345e-12, 0, false, -1 },
        { "-345E+12", -345e12, 0, false, -1 },
        { "-345E-12", -345e-12, 0, false, -1 },
        { "-345.678e+12", -345.678e12, 0, false, -1 },
        { "-345.678e-12", -345.678e-12, 0, false, -1 },
        { "-345.678E+12", -345.678e12, 0, false, -1 },
        { "-345.678E-12", -345.678e-12, 0, false, -1 },
        { dbl_max_str, DBL_MAX, 0, false, -1 },
        { neg_dbl_max_str, -DBL_MAX, 0, false, -1 },
        { dbl_max_str_overflow, DBL_MAX, ERANGE, false, -1 },
        { neg_dbl_max_str_overflow, -DBL_MAX, ERANGE, false, -1 },
        { "x", 0, 0, false, 0 },
        { "1x", 1.0, 0, false, 1 },
        { "12,3", 12.0, 0, false, 2 },
        { "", 0, 0, false, 0 },
#ifdef HAVE_LOCALE
        /* commas as decimal separators */
        { "1,0", 1.0, 0, true, -1 },
        { "123,456", 123.456, 0, true, -1 },
        { "345e+12", 345e12, 0, true, -1 },
        { "345e-12", 345e-12, 0, true, -1 },
        { "345E+12", 345e12, 0, true, -1 },
        { "345E-12", 345e-12, 0, true, -1 },
        { "-1,0", -1.0, 0, true, -1 },
        { "-123,456", -123.456, 0, true, -1 },
        { "-345e+12", -345e12, 0, true, -1 },
        { "-345e-12", -345e-12, 0, true, -1 },
        { "-345E+12", -345e12, 0, true, -1 },
        { "-345E-12", -345e-12, 0, true, -1 },
        { "-345,678e+12", -345.678e12, 0, true, -1 },
        { "-345,678e-12", -345.678e-12, 0, true, -1 },
        { "-345,678E+12", -345.678e12, 0, true, -1 },
        { "-345,678E-12", -345.678e-12, 0, true, -1 },
        { "12.3", 12.0, 0, true, 2 },
#endif
        {}
    };

#ifdef HAVE_LOCALE
    oldloc = setlocale(LC_ALL, NULL);
    if (oldloc)
        oldloc = strdupa(oldloc);
    setlocale(LC_ALL, "C");
#endif

    snprintf(dbl_max_str, sizeof(dbl_max_str), "%.64g", DBL_MAX);
    snprintf(neg_dbl_max_str, sizeof(neg_dbl_max_str), "%.64g", -DBL_MAX);
    snprintf(dbl_max_str_overflow, sizeof(dbl_max_str_overflow), "%.64g0", DBL_MAX);
    snprintf(neg_dbl_max_str_overflow, sizeof(neg_dbl_max_str_overflow), "%.64g0", -DBL_MAX);

#ifdef HAVE_LOCALE
    {
        const char **loc;
        comma_locale = NULL;
        for (loc = comma_locales; *loc != NULL; loc++) {
            if (setlocale(LC_ALL, *loc)) {
                setlocale(LC_ALL, oldloc);
                SOL_DBG("Using locale '%s' to produce commas as "
                    "decimal separator. Ex: %0.2f", *loc, 1.23);
                comma_locale = *loc;
                break;
            }
        }
        if (!comma_locale) {
            setlocale(LC_ALL, oldloc);
            SOL_WRN("Couldn't find a locale with decimal commas");
        }
    }
#endif

    for (itr = tests; itr->str != NULL; itr++) {
        double value;
        char buf[512];
        char *endptr;
        size_t slen = strlen(itr->str);
        int endptr_offset, wanted_endptr_offset;
        int reterr;

        snprintf(buf, sizeof(buf), "%s123garbage", itr->str);

        if (comma_locale)
            setlocale(LC_ALL, comma_locale);
        else if (itr->use_locale) {
            SOL_DBG("SKIP (no comma locale): '%s'", itr->str);
            continue;
        }

        value = sol_util_strtod_n(buf, &endptr, slen, itr->use_locale);
        reterr = errno;

        endptr_offset = endptr - buf;

        if (comma_locale)
            setlocale(LC_ALL, oldloc);

        wanted_endptr_offset = itr->endptr_offset;
        if (wanted_endptr_offset < 0)
            wanted_endptr_offset = slen;

        if (itr->expected_errno == 0 && reterr == 0) {
            if (sol_util_double_eq(itr->reference, value)) {
                SOL_DBG("OK: parsed '%s' as %g (locale:%u)", itr->str, value,
                    itr->use_locale);
            } else {
                SOL_WRN("FAILED: parsed '%s' as %.64g where %.64g was expected"
                    " (difference = %g) (locale:%u)",
                    itr->str, value, itr->reference, itr->reference - value,
                    itr->use_locale);
                FAIL();
            }
        } else if (itr->expected_errno == 0 && reterr < 0) {
            SOL_WRN("FAILED: parsing '%s' failed with errno = %d (%s) (locale:%u)",
                itr->str, reterr, sol_util_strerrora(reterr), itr->use_locale);
            FAIL();
        } else if (itr->expected_errno != 0 && reterr == 0) {
            SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                ", but got success with errno = %d (%s), value = %g (locale:%u)",
                itr->str,
                itr->expected_errno, sol_util_strerrora(itr->expected_errno),
                reterr, sol_util_strerrora(reterr), value, itr->use_locale);
            FAIL();
        } else if (itr->expected_errno != 0 && reterr < 0) {
            if (itr->expected_errno != reterr) {
                SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                    ", but got errno = %d (%s), value = %g (locale:%u)",
                    itr->str,
                    itr->expected_errno, sol_util_strerrora(itr->expected_errno),
                    reterr, sol_util_strerrora(reterr), value, itr->use_locale);
                FAIL();
            } else if (!sol_util_double_eq(itr->reference, value)) {
                SOL_WRN("FAILED: parsing '%s' should result in %.64g"
                    ", but got %.64g (difference = %g) (locale:%u)",
                    itr->str, itr->reference, value, itr->reference - value,
                    itr->use_locale);
                FAIL();
            } else {
                SOL_DBG("OK: parsed '%s' as %g, setting errno = %d (%s) (locale:%u)",
                    itr->str, value, reterr, sol_util_strerrora(reterr), itr->use_locale);
            }
        }

        if (wanted_endptr_offset != endptr_offset) {
            SOL_WRN("FAILED: parsing '%s' should stop at offset %d, but got %d  (locale:%u)",
                itr->str, wanted_endptr_offset, endptr_offset, itr->use_locale);
            FAIL();
        }
    }
}

DEFINE_TEST(test_base64_encode);

static void
test_base64_encode(void)
{
    const char base64_map[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    const char instr[] = "This is a message that is multiple of 3 chars";
    const char *expectstrs[] = {
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNoYXJz",
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNoYXI=",
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNoYQ==",
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNo"
    };
    struct sol_str_slice slice;
    char outstr[(sizeof(instr) / 3 + 1) * 4 + 1];
    size_t r, i;

    slice = sol_str_slice_from_str(instr);

    for (i = 0; i < sol_util_array_size(expectstrs); i++) {
        struct sol_str_slice exp = sol_str_slice_from_str(expectstrs[i]);

        memset(outstr, 0xff, sizeof(outstr));
        r = sol_util_base64_encode(outstr, sizeof(outstr), slice, base64_map);
        ASSERT_INT_EQ(r, exp.len);
        ASSERT_INT_EQ(outstr[r], (char)0xff);
        outstr[r] = '\0';
        ASSERT_STR_EQ(outstr, exp.data);

        slice.len--;
    }
}

DEFINE_TEST(test_base64_decode);

static void
test_base64_decode(void)
{
    const char base64_map[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    char expstr[] = "This is a message that is multiple of 3 chars";
    const char *instrs[] = {
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNoYXJz",
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNoYXI=",
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNoYQ==",
        "VGhpcyBpcyBhIG1lc3NhZ2UgdGhhdCBpcyBtdWx0aXBsZSBvZiAzIGNo"
    };
    struct sol_str_slice exp, slice;
    char outstr[sizeof(expstr)];
    size_t r, i;

    exp = sol_str_slice_from_str(expstr);

    for (i = 0; i < sol_util_array_size(instrs); i++) {
        slice = sol_str_slice_from_str(instrs[i]);

        memset(outstr, 0xff, sizeof(outstr));
        r = sol_util_base64_decode(outstr, sizeof(outstr), slice, base64_map);
        ASSERT_INT_EQ(r, exp.len);
        ASSERT_INT_EQ(outstr[r], (char)0xff);
        outstr[r] = '\0';
        ASSERT_STR_EQ(outstr, exp.data);

        exp.len--;
        expstr[exp.len] = '\0';
    }

    /* negative test (invalid char) */
    slice = sol_str_slice_from_str("****");
    memset(outstr, 0xff, sizeof(outstr));
    r = sol_util_base64_decode(outstr, sizeof(outstr), slice, base64_map);
    ASSERT_INT_EQ(r, -EINVAL);

    /* short sequence (not multiple of 4) */
    slice = sol_str_slice_from_str("123");
    memset(outstr, 0xff, sizeof(outstr));
    r = sol_util_base64_decode(outstr, sizeof(outstr), slice, base64_map);
    ASSERT_INT_EQ(r, -EINVAL);
}

DEFINE_TEST(test_base16_encode);

static void
test_base16_encode(void)
{
    const char instr[] = "Test \x01\x09\x0a\x0f Hello";
    const char *expstrs[] = {
        "546573742001090a0f2048656c6c6f",
        "546573742001090A0F2048656C6C6F"
    };
    char outstr[(sizeof(instr) - 1) * 2 + 1];
    struct sol_str_slice slice;
    ssize_t r, i;

    slice = sol_str_slice_from_str(instr);

    for (i = 0; i < 2; i++) {

        memset(outstr, 0xff, sizeof(outstr));
        r = sol_util_base16_encode(outstr, sizeof(outstr), slice, !!i);
        ASSERT_INT_EQ(r, strlen(expstrs[i]));
        ASSERT_INT_EQ(outstr[r], (char)0xff);
        outstr[r] = '\0';
        ASSERT_STR_EQ(outstr, expstrs[i]);
    }
}

DEFINE_TEST(test_base16_decode);

static void
test_base16_decode(void)
{
    const char expstr[] = "Test \x01\x09\x0a\x0f Hello";
    const char *instrs[] = {
        "546573742001090a0f2048656c6c6f",
        "546573742001090A0F2048656C6C6F"
    };
    char outstr[sizeof(expstr)];
    struct sol_str_slice slice;
    ssize_t r, i;
    enum sol_decode_case decode_case;

    for (i = 0; i < 4; i++) {
        slice = sol_str_slice_from_str(instrs[i % 2]);

        if (i == 0)
            decode_case = SOL_DECODE_LOWERCASE;
        else if (i == 1)
            decode_case = SOL_DECODE_UPPERCASE;
        else
            decode_case = SOL_DECODE_BOTH;

        memset(outstr, 0xff, sizeof(outstr));
        r = sol_util_base16_decode(outstr, sizeof(outstr), slice, decode_case);
        ASSERT_INT_EQ(r, strlen(expstr));
        ASSERT_INT_EQ(outstr[r], (char)0xff);
        outstr[r] = '\0';
        ASSERT_STR_EQ(outstr, expstr);
    }

    /* negative test (case swap) */
    for (i = 0; i < 2; i++) {
        slice = sol_str_slice_from_str(instrs[i]);

        memset(outstr, 0xff, sizeof(outstr));
        r = sol_util_base16_decode(outstr, sizeof(outstr), slice, !i ?
            SOL_DECODE_UPPERCASE : SOL_DECODE_LOWERCASE);
    }

    /* short sequence (not multiple of 2) */
    slice = sol_str_slice_from_str("1");
    memset(outstr, 0xff, sizeof(outstr));
    r = sol_util_base16_decode(outstr, sizeof(outstr), slice,
        SOL_DECODE_UPPERCASE);
    ASSERT_INT_EQ(r, -EINVAL);
}

DEFINE_TEST(test_unicode_utf_conversion);

static void
test_unicode_utf_conversion(void)
{
    const char utf8_string[] = "Unicode ÀÊÍöúĎǧɵܢވ࡞शཌ❤♎☀⚑€♫𐄣𐿿鿿𐀀";
    const uint8_t invalid_utf8[][4] = {
        { 0xA0, 0x01, 0x0, 0x0 },
        { 0xA0, 0xFF, 0x0, 0x0 },
        { 0xE5, 0x01, 0x80, 0x0 },
        { 0xE5, 0xFF, 0x80, 0x0 },
        { 0xE5, 0x80, 0x01, 0x0 },
        { 0xE5, 0x80, 0xFF, 0x0 },
        { 0xF2, 0x0, 0x0, 0x0 },
        { 0xF0, 0x0, 0x0, 0x0 },
        { 0xF0, 0x90, 0x0, 0x0 },
        { 0xF0, 0x90, 0x80, 0x0 },
    };
    uint8_t utf8_buf[4];
    const int32_t unicode_codes[] = {
        0x0055, 0x006E, 0x0069, 0x0063, 0x006f, 0x0064, 0x0065, 0x0020, 0x00c0,
        0x00CA, 0x00CD, 0x00f6, 0x00FA, 0x010e, 0x01e7, 0x0275, 0x0722, 0x0788,
        0x085E, 0x0936, 0x0f4c, 0x2764, 0x264e, 0x2600, 0x2691, 0x20ac, 0x266b,
        0x10123, 0x10fff, 0x9fff, 0x10000, 0x0
    };

    size_t i, str_len, r;
    const uint8_t *p = (const uint8_t *)utf8_string;
    int32_t code;
    uint8_t read, written;

    str_len = sizeof(utf8_string);
    for (i = 0; i < sol_util_array_size(unicode_codes); i++) {
        code = sol_util_unicode_code_from_utf8(p, str_len, &read);
        ASSERT_INT_EQ(code, unicode_codes[i]);

        written = sol_util_utf8_from_unicode_code(utf8_buf, 4,
            unicode_codes[i]);
        ASSERT_INT_EQ(read, written);
        ASSERT_INT_EQ(memcmp(utf8_buf, p, written), 0);

        p += read;
        str_len += read;

    }

    //Invalid values
    r = sol_util_utf8_from_unicode_code(utf8_buf, 4, 0x110000);
    ASSERT_INT_EQ(r, -EINVAL);
    r = sol_util_utf8_from_unicode_code(utf8_buf, 3, 0x10000);
    ASSERT_INT_EQ(r, -EINVAL);
    r = sol_util_utf8_from_unicode_code(utf8_buf, 2, 0x0800);
    ASSERT_INT_EQ(r, -EINVAL);
    r = sol_util_utf8_from_unicode_code(utf8_buf, 1, 0x080);
    ASSERT_INT_EQ(r, -EINVAL);
    r = sol_util_utf8_from_unicode_code(utf8_buf, 0, 0x0);
    ASSERT_INT_EQ(r, -EINVAL);

    for (i = 0; i < sol_util_array_size(invalid_utf8); i++) {
        code = sol_util_unicode_code_from_utf8(invalid_utf8[i],
            sizeof(invalid_utf8[i]), NULL);
        ASSERT_INT_EQ(code, -EINVAL);
    }
}

DEFINE_TEST(test_escape_quotes);

static void
test_escape_quotes(void)
{
    static const struct {
        const struct sol_str_slice input;
        const struct sol_str_slice output;
        ssize_t int_result;
        enum sol_buffer_flags flags;
    } escape_tests[] = {
        //Cases where that copy is not necessary.
        { SOL_STR_SLICE_LITERAL("x"), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("    x"), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("x    "), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("'x'"), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("\"x\""), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("    \"x\""), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("\"x\"     "), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED  },
        { SOL_STR_SLICE_LITERAL("    \"x\"    "), SOL_STR_SLICE_LITERAL("x"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("'Locale'"), SOL_STR_SLICE_LITERAL("Locale"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("\"My String\""), SOL_STR_SLICE_LITERAL("My String"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("      \"My Stri    ng\" "), SOL_STR_SLICE_LITERAL("My Stri    ng"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("       "), SOL_STR_SLICE_LITERAL(""), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("I'm good"), SOL_STR_SLICE_LITERAL("I'm good"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },
        { SOL_STR_SLICE_LITERAL("Hello"), SOL_STR_SLICE_LITERAL("Hello"), 0,
          SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED },

        { SOL_STR_SLICE_LITERAL("I 'like' you"), SOL_STR_SLICE_LITERAL("I like you"), 0,
          SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("x'y'"), SOL_STR_SLICE_LITERAL("xy"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("x\\\"y"), SOL_STR_SLICE_LITERAL("x\"y"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\\\'x"), SOL_STR_SLICE_LITERAL("'x"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\\\"x"), SOL_STR_SLICE_LITERAL("\"x"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("    \\\"x"), SOL_STR_SLICE_LITERAL("\"x"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("x\\\'y\\\"zd"), SOL_STR_SLICE_LITERAL("x'y\"zd"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("x\"y\""), SOL_STR_SLICE_LITERAL("xy"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("x\"y\"z\\\"f"), SOL_STR_SLICE_LITERAL("xyz\"f"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\\'Locale\\'"), SOL_STR_SLICE_LITERAL("'Locale'"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("MyQuo\\\"tes"), SOL_STR_SLICE_LITERAL("MyQuo\"tes"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("MyQuo\\'tes2"), SOL_STR_SLICE_LITERAL("MyQuo'tes2"), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\\\"Hi, I'm good\\\"   "), SOL_STR_SLICE_LITERAL("\"Hi, I'm good\""), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("    \\\"Hi, I'm good\\\"   "), SOL_STR_SLICE_LITERAL("\"Hi, I'm good\""), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("   \\\"Hi, I'm good\\\"   "), SOL_STR_SLICE_LITERAL("\"Hi, I'm good\""), 0, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\\\"Hi, I'm good\\\""), SOL_STR_SLICE_LITERAL("\"Hi, I'm good\""), 0, SOL_BUFFER_FLAGS_DEFAULT },

        //Cases that should fail
        { SOL_STR_SLICE_LITERAL("Wrong\\a"), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("Wrong\\ba"), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("'x\""), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\"x'"), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\"x'"), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("'x\""), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("'x"), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
        { SOL_STR_SLICE_LITERAL("\"x"), SOL_STR_SLICE_LITERAL(""), -EINVAL, SOL_BUFFER_FLAGS_DEFAULT },
    };
    size_t i;

    for (i = 0; i < sol_util_array_size(escape_tests); i++) {
        struct sol_buffer buf;
        int r;

        r = sol_util_unescape_quotes(escape_tests[i].input, &buf);
        ASSERT_INT_EQ(r, escape_tests[i].int_result);
        if (r < 0)
            continue;
        ASSERT(sol_str_slice_eq(escape_tests[i].output, sol_buffer_get_slice(&buf)));
        ASSERT(buf.flags == escape_tests[i].flags);
        sol_buffer_fini(&buf);
    }
}

DEFINE_TEST(test_uuid_functions);

static void
test_uuid_functions(void)
{
    SOL_BUFFER_DECLARE_STATIC(buf, 37);
    struct sol_str_slice
        uuid_uh = SOL_STR_SLICE_LITERAL("9FD636DD-FF84-4075-8AE7-D55F2F7BA190"),
        uuid_lh = SOL_STR_SLICE_LITERAL("9fd636dd-ff84-4075-8ae7-d55f2f7ba190"),
        uuid_u = SOL_STR_SLICE_LITERAL("9FD636DDFF8440758AE7D55F2F7BA190"),
        uuid_l = SOL_STR_SLICE_LITERAL("9fd636ddff8440758ae7d55f2f7ba190"),
        uuid_invalid = SOL_STR_SLICE_LITERAL("9fd6-6dd1ff841407518ae71d5-f2f7ba190"),
        uuid_invalid2 = SOL_STR_SLICE_LITERAL("9fd636ddff8440758ae7d55-2f7ba190"),
        uuid_invalid3 = SOL_STR_SLICE_LITERAL("9fd636ddff8440758ae7d552f7ba190");
    const uint8_t uuid_bytes[16] = { 0x9F, 0xD6, 0x36, 0xDD, 0xFF, 0x84, 0x40,
                                     0x75, 0x8A, 0xE7, 0xD5, 0x5F, 0x2F, 0x7B,
                                     0xA1, 0x90 };

    //UUID string to bytes
    ASSERT_INT_EQ(sol_util_uuid_bytes_from_string(uuid_uh, &buf), 0);
    ASSERT_INT_EQ(buf.used, sizeof(uuid_bytes));
    ASSERT_INT_EQ(memcmp(buf.data, uuid_bytes, sizeof(uuid_bytes)), 0);

    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_bytes_from_string(uuid_lh, &buf), 0);
    ASSERT_INT_EQ(buf.used, sizeof(uuid_bytes));
    ASSERT_INT_EQ(memcmp(buf.data, uuid_bytes, sizeof(uuid_bytes)), 0);

    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_bytes_from_string(uuid_u, &buf), 0);
    ASSERT_INT_EQ(buf.used, sizeof(uuid_bytes));
    ASSERT_INT_EQ(memcmp(buf.data, uuid_bytes, sizeof(uuid_bytes)), 0);

    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_bytes_from_string(uuid_l, &buf), 0);
    ASSERT_INT_EQ(buf.used, sizeof(uuid_bytes));
    ASSERT_INT_EQ(memcmp(buf.data, uuid_bytes, sizeof(uuid_bytes)), 0);

    //UUID bytes to string
    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_string_from_bytes(true, true, uuid_bytes,
        &buf), 0);
    ASSERT(sol_str_slice_str_eq(uuid_uh, buf.data));

    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_string_from_bytes(true, false, uuid_bytes,
        &buf), 0);
    ASSERT(sol_str_slice_str_eq(uuid_u, buf.data));

    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_string_from_bytes(false, true, uuid_bytes,
        &buf), 0);
    ASSERT(sol_str_slice_str_eq(uuid_lh, buf.data));

    buf.used = 0;
    ASSERT_INT_EQ(sol_util_uuid_string_from_bytes(false, false, uuid_bytes,
        &buf), 0);
    ASSERT(sol_str_slice_str_eq(uuid_l, buf.data));

    //UUID validation
    ASSERT(sol_util_uuid_str_is_valid(uuid_uh));
    ASSERT(sol_util_uuid_str_is_valid(uuid_lh));
    ASSERT(sol_util_uuid_str_is_valid(uuid_u));
    ASSERT(sol_util_uuid_str_is_valid(uuid_l));

    ASSERT(!sol_util_uuid_str_is_valid(uuid_invalid));
    ASSERT(!sol_util_uuid_str_is_valid(uuid_invalid2));
    ASSERT(!sol_util_uuid_str_is_valid(uuid_invalid3));
}

TEST_MAIN();
