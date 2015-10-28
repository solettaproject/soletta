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
#include <limits.h>
#include <stdbool.h>
#include <float.h>
#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "sol-util.h"
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

    for (i = 0; i < ARRAY_SIZE(table); i++) {
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
    ASSERT_INT_EQ(out, half_double_size);

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
    ASSERT_INT_EQ(out, half_double_ssize);

    ASSERT_INT_EQ(sol_util_ssize_mul(half_ssize, 4, &out), -EOVERFLOW);

    half_ssize *= -1;
    half_double_ssize *= -1;
    ASSERT(sol_util_ssize_mul(half_ssize, 2, &out) == 0);
    ASSERT_INT_EQ(out, half_double_ssize);

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

        value = sol_util_strtodn(buf, &endptr, slen, itr->use_locale);
        reterr = errno;

        endptr_offset = endptr - buf;

        if (comma_locale)
            setlocale(LC_ALL, oldloc);

        wanted_endptr_offset = itr->endptr_offset;
        if (wanted_endptr_offset < 0)
            wanted_endptr_offset = slen;

        if (itr->expected_errno == 0 && reterr == 0) {
            if (sol_drange_val_equal(itr->reference, value)) {
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
            } else if (!sol_drange_val_equal(itr->reference, value)) {
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

    for (i = 0; i < ARRAY_SIZE(expectstrs); i++) {
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

    for (i = 0; i < ARRAY_SIZE(instrs); i++) {
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
            decode_case = SOL_DECODE_UPERCASE;
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
            SOL_DECODE_UPERCASE : SOL_DECODE_LOWERCASE);
    }

    /* short sequence (not multiple of 2) */
    slice = sol_str_slice_from_str("1");
    memset(outstr, 0xff, sizeof(outstr));
    r = sol_util_base16_decode(outstr, sizeof(outstr), slice,
        SOL_DECODE_UPERCASE);
    ASSERT_INT_EQ(r, -EINVAL);
}

TEST_MAIN();
