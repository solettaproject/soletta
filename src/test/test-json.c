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

#include "test.h"
#include "sol-json.h"
#include "sol-util.h"
#include "sol-log.h"
#include "sol-types.h"
#include <float.h>

#define TOKENS (const enum sol_json_type[])
struct test_entry {
    const char *input;
    const enum sol_json_type *output;
    int expected_elements;
};


static struct test_entry scan_tests[] = {
    {
        "{}",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 2
    },
    {
        "{ \"string\" : \"this is a string\"}",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 5
    },
    {
        "{ \"number\" : 12345 }",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_NUMBER,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 5
    },
    {
        "{"
        "   \"menu\": {"
        "       \"id\": \"file\","
        "       \"value\": \"File\","
        "       \"popup\": {"
        "           \"menuitem\": ["
        "               {\"value\": \"New\"},"
        "               {\"value\": \"Open\"},"
        "               {\"value\": \"Close\"}"
        "           ]"
        "       }"
        "   }"
        "}",
        TOKENS {
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_ARRAY_START,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_ELEMENT_SEP,
            SOL_JSON_TYPE_OBJECT_START,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_PAIR_SEP,
            SOL_JSON_TYPE_STRING,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_ARRAY_END,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_OBJECT_END,
            SOL_JSON_TYPE_OBJECT_END
        },
        .expected_elements = 39
    }
};

DEFINE_TEST(test_json);

static void
test_json(void)
{
    unsigned int i = 0;
    int j = 0;

    for (i = 0; i < ARRAY_SIZE(scan_tests); i++) {
        struct test_entry *t;
        struct sol_json_scanner scanner;
        struct sol_json_token input;
        const enum sol_json_type *output;

        t = &scan_tests[i];
        input.start = t->input;
        input.end = t->input + strlen(t->input);
        sol_json_scanner_init(&scanner, input.start, input.end - input.start);

        output = scan_tests[i].output;

        for (j = 0; j < scan_tests[i].expected_elements; j++) {
            if (!sol_json_scanner_next(&scanner, &input)) {
                SOL_WRN("Error: Unexpected end of file.");
                ASSERT(false);
            }
            if (sol_json_token_get_type(&input) != output[j]) {
                SOL_WRN("Token: %c , Expected: %c \n", sol_json_token_get_type(&input), output[j]);
                ASSERT(false);
            }
        }
    }
}

DEFINE_TEST(test_json_token_get_uint64);

static void
test_json_token_get_uint64(void)
{
    const struct test_u64 {
        const char *str;
        uint64_t reference;
        int expected_return;
    } *itr, tests[] = {
        { "0", 0, 0 },
        { "123", 123, 0 },
        { "18446744073709551615", UINT64_MAX, 0 },
        { "0000123", 123, 0 },
        { "-132", 0, -ERANGE },
        { "184467440737095516150", UINT64_MAX, -ERANGE }, /* mul overflow */
        { "18446744073709551616", UINT64_MAX, -ERANGE }, /* add overflow */
        { "1.0", 1, -EINVAL },
        { "123.456", 123, -EINVAL },
        { "345e+12", 345, -EINVAL },
        { "x", 0, -EINVAL },
        { "", 0, -EINVAL },
        {}
    };

    for (itr = tests; itr->str != NULL; itr++) {
        uint64_t value;
        char buf[512];
        struct sol_json_token token;
        int retval;

        snprintf(buf, sizeof(buf), "%s123garbage", itr->str);
        token.start = buf;
        token.end = buf + strlen(itr->str);
        retval = sol_json_token_get_uint64(&token, &value);
        if (itr->expected_return == 0 && retval == 0) {
            if (itr->reference == value) {
                SOL_DBG("OK: parsed '%s' as %" PRIu64, itr->str, value);
            } else {
                SOL_WRN("FAILED: parsed '%s' as %" PRIu64 " where %" PRIu64
                    " was expected", itr->str, value, itr->reference);
                FAIL();
            }
        } else if (itr->expected_return == 0 && retval < 0) {
            SOL_WRN("FAILED: parsing '%s' failed with errno = %d (%s)",
                itr->str, retval, sol_util_strerrora(-retval));
            FAIL();
        } else if (itr->expected_return != 0 && retval == 0) {
            SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                ", but got success with errno = %d (%s), value = %" PRIu64,
                itr->str,
                itr->expected_return, sol_util_strerrora(-itr->expected_return),
                retval, sol_util_strerrora(-retval), value);
            FAIL();
        } else if (itr->expected_return != 0 && retval < 0) {
            if (itr->expected_return != retval) {
                SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                    ", but got errno = %d (%s), value = %" PRIu64,
                    itr->str,
                    itr->expected_return, sol_util_strerrora(-itr->expected_return),
                    retval, sol_util_strerrora(-retval), value);
                FAIL();
            } else if (itr->reference != value) {
                SOL_WRN("FAILED: parsing '%s' should result in %" PRIu64
                    ", but got %" PRIu64, itr->str, itr->reference, value);
                FAIL();
            } else {
                SOL_DBG("OK: parsed '%s' as %" PRIu64
                    ", setting errno = %d (%s)",
                    itr->str, value, retval, sol_util_strerrora(-retval));
            }
        }
    }

}

DEFINE_TEST(test_json_token_get_uint32);

static void
test_json_token_get_uint32(void)
{
    const struct test_u32 {
        const char *str;
        uint32_t reference;
        int expected_return;
    } *itr, tests[] = {
        { "0", 0, 0 },
        { "123", 123, 0 },
        { "4294967295", UINT32_MAX, 0 },
        { "0000123", 123, 0 },
        { "-132", 0, -ERANGE },
        { "184467440737095516150", UINT32_MAX, -ERANGE },
        { "1.0", 1, -EINVAL },
        { "123.456", 123, -EINVAL },
        { "345e+12", 345, -EINVAL },
        { "x", 0, -EINVAL },
        { "", 0, -EINVAL },
        {}
    };

    for (itr = tests; itr->str != NULL; itr++) {
        uint32_t value;
        char buf[512];
        struct sol_json_token token;
        int retval;

        snprintf(buf, sizeof(buf), "%s123garbage", itr->str);
        token.start = buf;
        token.end = buf + strlen(itr->str);
        retval = sol_json_token_get_uint32(&token, &value);
        if (itr->expected_return == 0 && retval == 0) {
            if (itr->reference == value) {
                SOL_DBG("OK: parsed '%s' as %" PRIu32, itr->str, value);
            } else {
                SOL_WRN("FAILED: parsed '%s' as %" PRIu32 " where %" PRIu32
                    " was expected", itr->str, value, itr->reference);
                FAIL();
            }
        } else if (itr->expected_return == 0 && retval < 0) {
            SOL_WRN("FAILED: parsing '%s' failed with errno = %d (%s)",
                itr->str, retval, sol_util_strerrora(-retval));
            FAIL();
        } else if (itr->expected_return != 0 && retval == 0) {
            SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                ", but got success with errno = %d (%s), value = %" PRIu32,
                itr->str,
                itr->expected_return, sol_util_strerrora(-itr->expected_return),
                retval, sol_util_strerrora(-retval), value);
            FAIL();
        } else if (itr->expected_return != 0 && retval < 0) {
            if (itr->expected_return != retval) {
                SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                    ", but got errno = %d (%s), value = %" PRIu32,
                    itr->str,
                    itr->expected_return, sol_util_strerrora(-itr->expected_return),
                    retval, sol_util_strerrora(-retval), value);
                FAIL();
            } else if (itr->reference != value) {
                SOL_WRN("FAILED: parsing '%s' should result in %" PRIu32
                    ", but got %" PRIu32, itr->str, itr->reference, value);
                FAIL();
            } else {
                SOL_DBG("OK: parsed '%s' as %" PRIu32
                    ", setting errno = %d (%s)",
                    itr->str, value, retval, sol_util_strerrora(-retval));
            }
        }
    }

}

DEFINE_TEST(test_json_token_get_int64);

static void
test_json_token_get_int64(void)
{
    const struct test_i64 {
        const char *str;
        int64_t reference;
        int expected_return;
    } *itr, tests[] = {
        { "0", 0, 0 },
        { "123", 123, 0 },
        { "9223372036854775807", INT64_MAX, 0 },
        { "-9223372036854775808", INT64_MIN, 0 },
        { "0000123", 123, 0 },
        { "-132", -132, 0 },
        { "-0000345", -345, 0 },
        { "92233720368547758070", INT64_MAX, -ERANGE },
        { "-92233720368547758080", INT64_MIN, -ERANGE },
        { "9223372036854775808", INT64_MAX, -ERANGE },
        { "-9223372036854775809", INT64_MIN, -ERANGE },
        { "1.0", 1, -EINVAL },
        { "123.456", 123, -EINVAL },
        { "345e+12", 345, -EINVAL },
        { "-1.0", -1, -EINVAL },
        { "-123.456", -123, -EINVAL },
        { "-345e+12", -345, -EINVAL },
        { "x", 0, -EINVAL },
        { "", 0, -EINVAL },
        {}
    };

    for (itr = tests; itr->str != NULL; itr++) {
        int64_t value;
        char buf[512];
        struct sol_json_token token;
        int retval;

        snprintf(buf, sizeof(buf), "%s123garbage", itr->str);
        token.start = buf;
        token.end = buf + strlen(itr->str);
        retval = sol_json_token_get_int64(&token, &value);
        if (itr->expected_return == 0 && retval == 0) {
            if (itr->reference == value) {
                SOL_DBG("OK: parsed '%s' as %" PRIi64, itr->str, value);
            } else {
                SOL_WRN("FAILED: parsed '%s' as %" PRIi64 " where %" PRIi64
                    " was expected", itr->str, value, itr->reference);
                FAIL();
            }
        } else if (itr->expected_return == 0 && retval < 0) {
            SOL_WRN("FAILED: parsing '%s' failed with errno = %d (%s)",
                itr->str, retval, sol_util_strerrora(-retval));
            FAIL();
        } else if (itr->expected_return != 0 && retval == 0) {
            SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                ", but got success with errno = %d (%s), value = %" PRIi64,
                itr->str,
                itr->expected_return, sol_util_strerrora(-itr->expected_return),
                retval, sol_util_strerrora(-retval), value);
            FAIL();
        } else if (itr->expected_return != 0 && retval < 0) {
            if (itr->expected_return != retval) {
                SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                    ", but got errno = %d (%s), value = %" PRIi64,
                    itr->str,
                    itr->expected_return, sol_util_strerrora(-itr->expected_return),
                    retval, sol_util_strerrora(-retval), value);
                FAIL();
            } else if (itr->reference != value) {
                SOL_WRN("FAILED: parsing '%s' should result in %" PRIi64
                    ", but got %" PRIi64, itr->str, itr->reference, value);
                FAIL();
            } else {
                SOL_DBG("OK: parsed '%s' as %" PRIi64
                    ", setting errno = %d (%s)",
                    itr->str, value, retval, sol_util_strerrora(-retval));
            }
        }
    }

}

DEFINE_TEST(test_json_token_get_int32);

static void
test_json_token_get_int32(void)
{
    const struct test_i32 {
        const char *str;
        int32_t reference;
        int expected_return;
    } *itr, tests[] = {
        { "0", 0, 0 },
        { "123", 123, 0 },
        { "2147483647", INT32_MAX, 0 },
        { "-2147483648", INT32_MIN, 0 },
        { "0000123", 123, 0 },
        { "-132", -132, 0 },
        { "-0000345", -345, 0 },
        { "21474836470", INT32_MAX, -ERANGE },
        { "-21474836480", INT32_MIN, -ERANGE },
        { "2147483648", INT32_MAX, -ERANGE },
        { "-2147483649", INT32_MIN, -ERANGE },
        { "1.0", 1, -EINVAL },
        { "123.456", 123, -EINVAL },
        { "345e+12", 345, -EINVAL },
        { "-1.0", -1, -EINVAL },
        { "-123.456", -123, -EINVAL },
        { "-345e+12", -345, -EINVAL },
        { "x", 0, -EINVAL },
        { "", 0, -EINVAL },
        {}
    };

    for (itr = tests; itr->str != NULL; itr++) {
        int32_t value;
        char buf[512];
        struct sol_json_token token;
        int retval;

        snprintf(buf, sizeof(buf), "%s123garbage", itr->str);
        token.start = buf;
        token.end = buf + strlen(itr->str);
        retval = sol_json_token_get_int32(&token, &value);
        if (itr->expected_return == 0 && retval == 0) {
            if (itr->reference == value) {
                SOL_DBG("OK: parsed '%s' as %" PRIi32, itr->str, value);
            } else {
                SOL_WRN("FAILED: parsed '%s' as %" PRIi32 " where %" PRIi32
                    " was expected", itr->str, value, itr->reference);
                FAIL();
            }
        } else if (itr->expected_return == 0 && retval < 0) {
            SOL_WRN("FAILED: parsing '%s' failed with errno = %d (%s)",
                itr->str, retval, sol_util_strerrora(-retval));
            FAIL();
        } else if (itr->expected_return != 0 && retval == 0) {
            SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                ", but got success with errno = %d (%s), value = %" PRIi32,
                itr->str,
                itr->expected_return, sol_util_strerrora(-itr->expected_return),
                retval, sol_util_strerrora(-retval), value);
            FAIL();
        } else if (itr->expected_return != 0 && retval < 0) {
            if (itr->expected_return != retval) {
                SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                    ", but got errno = %d (%s), value = %" PRIi32,
                    itr->str,
                    itr->expected_return, sol_util_strerrora(-itr->expected_return),
                    retval, sol_util_strerrora(-retval), value);
                FAIL();
            } else if (itr->reference != value) {
                SOL_WRN("FAILED: parsing '%s' should result in %" PRIi32
                    ", but got %" PRIi32, itr->str, itr->reference, value);
                FAIL();
            } else {
                SOL_DBG("OK: parsed '%s' as %" PRIi32
                    ", setting errno = %d (%s)",
                    itr->str, value, retval, sol_util_strerrora(-retval));
            }
        }
    }

}

DEFINE_TEST(test_json_token_get_double);

static void
test_json_token_get_double(void)
{
    char dbl_max_str[256], neg_dbl_max_str[256];
    char dbl_max_str_overflow[256], neg_dbl_max_str_overflow[256];
    const struct test_double {
        const char *str;
        double reference;
        int expected_return;
    } *itr, tests[] = {
        { "0", 0.0, 0 },
        { "123", 123.0, 0 },
        { "1.0", 1.0, 0 },
        { "123.456", 123.456, 0 },
        { "345e+12", 345e12, 0 },
        { "345e-12", 345e-12, 0 },
        { "345E+12", 345e12, 0 },
        { "345E-12", 345e-12, 0 },
        { "-1.0", -1.0, 0 },
        { "-123.456", -123.456, 0 },
        { "-345e+12", -345e12, 0 },
        { "-345e-12", -345e-12, 0 },
        { "-345E+12", -345e12, 0 },
        { "-345E-12", -345e-12, 0 },
        { "-345.678e+12", -345.678e12, 0 },
        { "-345.678e-12", -345.678e-12, 0 },
        { "-345.678E+12", -345.678e12, 0 },
        { "-345.678E-12", -345.678e-12, 0 },
        { dbl_max_str, DBL_MAX, 0 },
        { neg_dbl_max_str, -DBL_MAX, 0 },
        { dbl_max_str_overflow, DBL_MAX, -ERANGE },
        { neg_dbl_max_str_overflow, -DBL_MAX, -ERANGE },
        { "x", 0, -EINVAL },
        { "", 0, -EINVAL },
        {}
    };

    snprintf(dbl_max_str, sizeof(dbl_max_str), "%.64g", DBL_MAX);
    snprintf(neg_dbl_max_str, sizeof(neg_dbl_max_str), "%.64g", -DBL_MAX);
    snprintf(dbl_max_str_overflow, sizeof(dbl_max_str_overflow), "%.64g0", DBL_MAX);
    snprintf(neg_dbl_max_str_overflow, sizeof(neg_dbl_max_str_overflow), "%.64g0", -DBL_MAX);

    for (itr = tests; itr->str != NULL; itr++) {
        double value;
        char buf[512];
        struct sol_json_token token;
        int retval;

        snprintf(buf, sizeof(buf), "%s123garbage", itr->str);
        token.start = buf;
        token.end = buf + strlen(itr->str);
        retval = sol_json_token_get_double(&token, &value);
        if (itr->expected_return == 0 && retval == 0) {
            if (sol_drange_val_equal(itr->reference, value)) {
                SOL_DBG("OK: parsed '%s' as %g", itr->str, value);
            } else {
                SOL_WRN("FAILED: parsed '%s' as %.64g where %.64g was expected"
                    " (difference = %g)",
                    itr->str, value, itr->reference, itr->reference - value);
                FAIL();
            }
        } else if (itr->expected_return == 0 && retval < 0) {
            SOL_WRN("FAILED: parsing '%s' failed with errno = %d (%s)",
                itr->str, retval, sol_util_strerrora(-retval));
            FAIL();
        } else if (itr->expected_return != 0 && retval == 0) {
            SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                ", but got success with errno = %d (%s), value = %g",
                itr->str,
                itr->expected_return, sol_util_strerrora(-itr->expected_return),
                retval, sol_util_strerrora(-retval), value);
            FAIL();
        } else if (itr->expected_return != 0 && retval < 0) {
            if (itr->expected_return != retval) {
                SOL_WRN("FAILED: parsing '%s' should fail with errno = %d (%s)"
                    ", but got errno = %d (%s), value = %g",
                    itr->str,
                    itr->expected_return, sol_util_strerrora(-itr->expected_return),
                    retval, sol_util_strerrora(-retval), value);
                FAIL();
            } else if (!sol_drange_val_equal(itr->reference, value)) {
                SOL_WRN("FAILED: parsing '%s' should result in %.64g"
                    ", but got %.64g (difference = %g)",
                    itr->str, itr->reference, value, itr->reference - value);
                FAIL();
            } else {
                SOL_DBG("OK: parsed '%s' as %g, setting errno = %d (%s)",
                    itr->str, value, retval, sol_util_strerrora(-retval));
            }
        }
    }
}

TEST_MAIN();
