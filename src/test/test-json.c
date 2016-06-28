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

#include "test.h"
#include "sol-json.h"
#include "sol-util-internal.h"
#include "sol-log.h"
#include "sol-util.h"
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

    for (i = 0; i < sol_util_array_size(scan_tests); i++) {
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
            if (sol_util_double_eq(itr->reference, value)) {
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
            } else if (!sol_util_double_eq(itr->reference, value)) {
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

DEFINE_TEST(test_json_serialize_memdesc);
static void
test_json_serialize_memdesc(void)
{
    enum myenum {
        enum0 = 0,
        enum1,
        enum2
    };
    const struct myst {
        int64_t i64;
        char *s;
        uint8_t u8;
        void *ptr;
    } myst_defcontent = {
        .i64 = 0x7234567890123456,
        .s = (char *)"some string \"quotes\" and \t tab",
        .u8 = 0xf2,
        .ptr = NULL
    };
    struct sol_vector int_vector = SOL_VECTOR_INIT(int32_t);
    struct sol_vector kv_vector = SOL_VECTOR_INIT(struct sol_key_value);
    struct sol_vector enum_vector = SOL_VECTOR_INIT(enum myenum);
    const struct test {
        struct sol_memdesc desc;
        const char *expected_detailed;
        const char *expected_essential;
    } *itr, tests[] = {
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_BOOL,
                .defcontent.b = true,
            },
            .expected_essential = "true",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_BOOL,
                .defcontent.b = false,
            },
            .expected_essential = "false",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_INT64,
                .defcontent.i64 = 0x7234567890123456,
            },
            .expected_essential = "8229297494925915222",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_UINT64,
                .defcontent.i64 = 0xf234567890123456,
            },
            .expected_essential = "17452669531780691030",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_STRING,
                .defcontent.s = "some string \"quotes\" and \t tab",
            },
            .expected_essential = "\"some string \\\"quotes\\\" and \\t tab\"",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct myst),
                .type = SOL_MEMDESC_TYPE_STRUCTURE,
                .defcontent.p = &myst_defcontent,
                .structure_members = (const struct sol_memdesc_structure_member[]){
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_INT64,
                        },
                        .offset = offsetof(struct myst, i64),
                        .name = "i64",
                        .detail = true,
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_STRING,
                        },
                        .offset = offsetof(struct myst, s),
                        .name = "s",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_UINT8,
                        },
                        .offset = offsetof(struct myst, u8),
                        .name = "u8",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_PTR,
                        },
                        .offset = offsetof(struct myst, ptr),
                        .name = "ptr",
                        .detail = true,
                    },
                    {}
                },
            },
            .expected_detailed = "{\"i64\":8229297494925915222,\"s\":\"some string \\\"quotes\\\" and \\t tab\",\"u8\":242,\"ptr\":null}",
            .expected_essential = "{\"s\":\"some string \\\"quotes\\\" and \\t tab\",\"u8\":242}",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_PTR,
                .defcontent.p = &myst_defcontent,
                .pointed_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct myst),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_INT64,
                            },
                            .offset = offsetof(struct myst, i64),
                            .name = "i64",
                            .detail = true,
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct myst, s),
                            .name = "s",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_UINT8,
                            },
                            .offset = offsetof(struct myst, u8),
                            .name = "u8",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_PTR,
                            },
                            .offset = offsetof(struct myst, ptr),
                            .name = "ptr",
                            .detail = true,
                        },
                        {}
                    },
                },
            },
            .expected_detailed = "{\"i64\":8229297494925915222,\"s\":\"some string \\\"quotes\\\" and \\t tab\",\"u8\":242,\"ptr\":null}",
            .expected_essential = "{\"s\":\"some string \\\"quotes\\\" and \\t tab\",\"u8\":242}",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_PTR,
                .pointed_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct myst),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_INT64,
                            },
                            .offset = offsetof(struct myst, i64),
                            .name = "i64",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct myst, s),
                            .name = "s",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_UINT8,
                            },
                            .offset = offsetof(struct myst, u8),
                            .name = "u8",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_PTR,
                            },
                            .offset = offsetof(struct myst, ptr),
                            .name = "ptr",
                        },
                        {}
                    },
                },
            },
            .expected_essential = "null",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .defcontent.p = &int_vector,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_INT32,
                },
            },
            .expected_essential = "[10,20,30,40,50,60,70,80,90,100]",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .defcontent.p = &enum_vector,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_ENUMERATION,
                    .size = sizeof(enum myenum),
                    .enumeration_mapping = (const struct sol_str_table_int64[]){
                        SOL_STR_TABLE_INT64_ITEM("enum0", enum0),
                        SOL_STR_TABLE_INT64_ITEM("enum1", enum1),
                        SOL_STR_TABLE_INT64_ITEM("enum2", enum2),
                        {}
                    },
                },
            },
            .expected_essential = "[\"enum0\",\"enum1\",\"enum2\",3]",
        },
        {
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .defcontent.p = &kv_vector,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct sol_key_value),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct sol_key_value, key),
                            .name = "key",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct sol_key_value, value),
                            .name = "value",
                        },
                        {}
                    },
                },
            },
            .expected_essential = "[{\"key\":\"akey\",\"value\":\"avalue\"},{\"key\":\"xkey\",\"value\":\"xvalue\"}]",
        },
        { }
    };
    size_t i;
    int32_t *int_items;
    enum myenum *enum_items;
    struct sol_key_value *kv_items;

    int_items = sol_vector_append_n(&int_vector, 10);
    ASSERT(int_items);
    ASSERT_INT_EQ(int_vector.len, 10);
    for (i = 0; i < int_vector.len; i++)
        int_items[i] = (i + 1) * 10;

    kv_items = sol_vector_append_n(&kv_vector, 2);
    ASSERT(kv_items);
    ASSERT_INT_EQ(kv_vector.len, 2);
    kv_items[0].key = "akey";
    kv_items[0].value = "avalue";
    kv_items[1].key = "xkey";
    kv_items[1].value = "xvalue";

    enum_items = sol_vector_append_n(&enum_vector, 4);
    ASSERT(enum_items);
    ASSERT_INT_EQ(enum_vector.len, 4);
    for (i = 0; i < enum_vector.len; i++)
        enum_items[i] = i;

    for (itr = tests; itr->expected_essential != NULL; itr++) {
        void *mem = sol_memdesc_new_with_defaults(&itr->desc);
        struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
        struct sol_str_slice out;
        int r;

        ASSERT(mem);
        r = sol_json_serialize_memdesc(&buf, &itr->desc, mem, false);
        ASSERT_INT_EQ(r, 0);

        out = sol_buffer_get_slice(&buf);
        ASSERT_STR_EQ(out.data, itr->expected_essential);

        sol_buffer_fini(&buf);

        if (itr->expected_detailed) {
            r = sol_json_serialize_memdesc(&buf, &itr->desc, mem, true);
            ASSERT_INT_EQ(r, 0);

            out = sol_buffer_get_slice(&buf);
            ASSERT_STR_EQ(out.data, itr->expected_detailed);

            sol_buffer_fini(&buf);
        }

        sol_memdesc_free(&itr->desc, mem);
    }

    sol_vector_clear(&int_vector);
    sol_vector_clear(&kv_vector);
    sol_vector_clear(&enum_vector);
}

DEFINE_TEST(test_json_load_memdesc);
static void
test_json_load_memdesc(void)
{
    enum myenum {
        enum0 = 0,
        enum1,
        enum2
    };
    const struct myst {
        int64_t i64;
        char *s;
        uint8_t u8;
        void *ptr;
    } myst_defcontent = {
        .i64 = 0x7234567890123456,
        .s = (char *)"some string \"quotes\" and \t tab",
        .u8 = 0xf2,
        .ptr = NULL
    };
    struct sol_vector int_vector = SOL_VECTOR_INIT(int32_t);
    struct sol_vector kv_vector = SOL_VECTOR_INIT(struct sol_key_value);
    struct sol_vector enum_vector = SOL_VECTOR_INIT(enum myenum);
    const struct test {
        const char *input;
        struct sol_memdesc desc;
        struct sol_memdesc desc_expected;
    } *itr, tests[] = {
        {
            .input = "true",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_BOOL,
                .defcontent.b = false,
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_BOOL,
                .defcontent.b = true,
            },
        },
        {
            .input = "false",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_BOOL,
                .defcontent.b = true,
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_BOOL,
                .defcontent.b = false,
            },
        },
        {
            .input = "8229297494925915222",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_INT64,
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_INT64,
                .defcontent.i64 = 0x7234567890123456,
            },
        },
        {
            .input = "17452669531780691030",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_UINT64,
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_UINT64,
                .defcontent.i64 = 0xf234567890123456,
            },
        },
        {
            .input = "\"some string \\\"quotes\\\" and \\t tab\"",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_STRING,
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_STRING,
                .defcontent.s = "some string \"quotes\" and \t tab",
            },
        },
        {
            .input = "{\"i64\":8229297494925915222,\"s\":\"some string \\\"quotes\\\" and \\t tab\",\"u8\":242,\"ptr\":null}",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct myst),
                .type = SOL_MEMDESC_TYPE_STRUCTURE,
                .structure_members = (const struct sol_memdesc_structure_member[]){
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_INT64,
                        },
                        .offset = offsetof(struct myst, i64),
                        .name = "i64",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_STRING,
                        },
                        .offset = offsetof(struct myst, s),
                        .name = "s",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_UINT8,
                        },
                        .offset = offsetof(struct myst, u8),
                        .name = "u8",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_PTR,
                        },
                        .offset = offsetof(struct myst, ptr),
                        .name = "ptr",
                    },
                    {}
                },
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct myst),
                .type = SOL_MEMDESC_TYPE_STRUCTURE,
                .defcontent.p = &myst_defcontent,
                .structure_members = (const struct sol_memdesc_structure_member[]){
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_INT64,
                        },
                        .offset = offsetof(struct myst, i64),
                        .name = "i64",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_STRING,
                        },
                        .offset = offsetof(struct myst, s),
                        .name = "s",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_UINT8,
                        },
                        .offset = offsetof(struct myst, u8),
                        .name = "u8",
                    },
                    {
                        .base = {
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .type = SOL_MEMDESC_TYPE_PTR,
                        },
                        .offset = offsetof(struct myst, ptr),
                        .name = "ptr",
                    },
                    {}
                },
            },
        },
        {
            .input = "{\"i64\":8229297494925915222,\"s\":\"some string \\\"quotes\\\" and \\t tab\",\"u8\":242,\"ptr\":null}",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_PTR,
                .pointed_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct myst),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_INT64,
                            },
                            .offset = offsetof(struct myst, i64),
                            .name = "i64",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct myst, s),
                            .name = "s",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_UINT8,
                            },
                            .offset = offsetof(struct myst, u8),
                            .name = "u8",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_PTR,
                            },
                            .offset = offsetof(struct myst, ptr),
                            .name = "ptr",
                        },
                        {}
                    },
                },
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct myst *),
                .type = SOL_MEMDESC_TYPE_PTR,
                .defcontent.p = &myst_defcontent,
                .pointed_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct myst),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_INT64,
                            },
                            .offset = offsetof(struct myst, i64),
                            .name = "i64",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct myst, s),
                            .name = "s",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_UINT8,
                            },
                            .offset = offsetof(struct myst, u8),
                            .name = "u8",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_PTR,
                            },
                            .offset = offsetof(struct myst, ptr),
                            .name = "ptr",
                        },
                        {}
                    },
                },
            },
        },
        {
            .input = "null",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_PTR,
                .pointed_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct myst),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_INT64,
                            },
                            .offset = offsetof(struct myst, i64),
                            .name = "i64",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct myst, s),
                            .name = "s",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_UINT8,
                            },
                            .offset = offsetof(struct myst, u8),
                            .name = "u8",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_PTR,
                            },
                            .offset = offsetof(struct myst, ptr),
                            .name = "ptr",
                        },
                        {}
                    },
                },
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .type = SOL_MEMDESC_TYPE_PTR,
                .defcontent.p = NULL,
                .pointed_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct myst),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_INT64,
                            },
                            .offset = offsetof(struct myst, i64),
                            .name = "i64",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct myst, s),
                            .name = "s",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_UINT8,
                            },
                            .offset = offsetof(struct myst, u8),
                            .name = "u8",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_PTR,
                            },
                            .offset = offsetof(struct myst, ptr),
                            .name = "ptr",
                        },
                        {}
                    },
                },
            },
        },
        {
            .input = "[10,20,30,40,50,60,70,80,90,100]",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_INT32,
                },
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .defcontent.p = &int_vector,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_INT32,
                },
            },
        },
        {
            .input = "[\"enum0\",\"enum1\",\"enum2\",3]",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_ENUMERATION,
                    .size = sizeof(enum myenum),
                    .enumeration_mapping = (const struct sol_str_table_int64[]){
                        SOL_STR_TABLE_INT64_ITEM("enum0", enum0),
                        SOL_STR_TABLE_INT64_ITEM("enum1", enum1),
                        SOL_STR_TABLE_INT64_ITEM("enum2", enum2),
                        {}
                    },
                },
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .defcontent.p = &enum_vector,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_ENUMERATION,
                    .size = sizeof(enum myenum),
                    .enumeration_mapping = (const struct sol_str_table_int64[]){
                        SOL_STR_TABLE_INT64_ITEM("enum0", enum0),
                        SOL_STR_TABLE_INT64_ITEM("enum1", enum1),
                        SOL_STR_TABLE_INT64_ITEM("enum2", enum2),
                        {}
                    },
                },
            },
        },
        {
            .input = "[{\"key\":\"akey\",\"value\":\"avalue\"},{\"key\":\"xkey\",\"value\":\"xvalue\"}]",
            .desc = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct sol_key_value),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct sol_key_value, key),
                            .name = "key",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct sol_key_value, value),
                            .name = "value",
                        },
                        {}
                    },
                },
            },
            .desc_expected = {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .defcontent.p = &kv_vector,
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .array_item = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct sol_key_value),
                    .type = SOL_MEMDESC_TYPE_STRUCTURE,
                    .structure_members = (const struct sol_memdesc_structure_member[]){
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct sol_key_value, key),
                            .name = "key",
                        },
                        {
                            .base = {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .type = SOL_MEMDESC_TYPE_STRING,
                            },
                            .offset = offsetof(struct sol_key_value, value),
                            .name = "value",
                        },
                        {}
                    },
                },
            },
        },
        {}
    };
    size_t i;
    int32_t *int_items;
    struct sol_key_value *kv_items;
    enum myenum *enum_items;

    int_items = sol_vector_append_n(&int_vector, 10);
    ASSERT(int_items);
    ASSERT_INT_EQ(int_vector.len, 10);
    for (i = 0; i < int_vector.len; i++)
        int_items[i] = (i + 1) * 10;

    kv_items = sol_vector_append_n(&kv_vector, 2);
    ASSERT(kv_items);
    ASSERT_INT_EQ(kv_vector.len, 2);
    kv_items[0].key = "akey";
    kv_items[0].value = "avalue";
    kv_items[1].key = "xkey";
    kv_items[1].value = "xvalue";

    enum_items = sol_vector_append_n(&enum_vector, 4);
    ASSERT(enum_items);
    ASSERT_INT_EQ(enum_vector.len, 4);
    for (i = 0; i < enum_vector.len; i++)
        enum_items[i] = i;

    for (itr = tests; itr->input != NULL; itr++) {
        void *mem = sol_memdesc_new_with_defaults(&itr->desc);
        void *mem_expected = sol_memdesc_new_with_defaults(&itr->desc_expected);
        struct sol_json_token token;
        int r;

        ASSERT(mem);
        ASSERT(mem_expected);

        sol_json_token_init_from_slice(&token, sol_str_slice_from_str(itr->input));
        r = sol_json_load_memdesc(&token, &itr->desc, mem);
        ASSERT_INT_EQ(r, 0);

        r = sol_memdesc_compare(&itr->desc, mem, mem_expected);
        ASSERT_INT_EQ(r, 0);

        sol_memdesc_free(&itr->desc, mem);
        sol_memdesc_free(&itr->desc_expected, mem_expected);
    }

    sol_vector_clear(&int_vector);
    sol_vector_clear(&kv_vector);
    sol_vector_clear(&enum_vector);
}

DEFINE_TEST(test_json_memdesc_complex);
static void
test_json_memdesc_complex(void)
{
    struct myst {
        uint64_t u64;
        struct sol_vector v;
        uint8_t u8;
    };
    struct myst defval = {
        .u64 = 0xf234567890123456,
        .v = SOL_VECTOR_INIT(struct sol_vector),
        .u8 = 0x72,
    };
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct myst),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .defcontent.p = &defval,
        .structure_members = (const struct sol_memdesc_structure_member[]){
            {
                .base = {
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_UINT64,
                },
                .offset = offsetof(struct myst, u64),
                .name = "u64",
            },
            {
                .base = {
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct sol_vector),
                    .type = SOL_MEMDESC_TYPE_ARRAY,
                    .ops = &SOL_MEMDESC_OPS_VECTOR,
                    .array_item = &(const struct sol_memdesc){
                        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                        .size = sizeof(struct sol_vector),
                        .type = SOL_MEMDESC_TYPE_ARRAY,
                        .ops = &SOL_MEMDESC_OPS_VECTOR,
                        .array_item = &(const struct sol_memdesc){
                            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                            .size = sizeof(struct sol_key_value),
                            .type = SOL_MEMDESC_TYPE_STRUCTURE,
                            .structure_members = (const struct sol_memdesc_structure_member[]){
                                {
                                    .base = {
                                        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                        .type = SOL_MEMDESC_TYPE_STRING,
                                    },
                                    .offset = offsetof(struct sol_key_value, key),
                                    .name = "key",
                                },
                                {
                                    .base = {
                                        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                        .type = SOL_MEMDESC_TYPE_STRING,
                                    },
                                    .offset = offsetof(struct sol_key_value, value),
                                    .name = "value",
                                },
                                {}
                            },
                        },
                    },
                },
                .offset = offsetof(struct myst, v),
                .name = "v",
            },
            {
                .base = {
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .type = SOL_MEMDESC_TYPE_UINT8,
                },
                .offset = offsetof(struct myst, u8),
                .name = "u8",
            },
            {}
        },
    };
    struct myst a;
    struct sol_key_value *kv;
    size_t i, j;
    int r;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const char expected[] = "{\"u64\":17452669531780691030,\"v\":[[{\"key\":\"key0\",\"value\":\"value0\"}],[{\"key\":\"key100\",\"value\":\"value100\"},{\"key\":\"key101\",\"value\":\"value101\"}],[{\"key\":\"key200\",\"value\":\"value200\"},{\"key\":\"key201\",\"value\":\"value201\"},{\"key\":\"key202\",\"value\":\"value202\"}],[{\"key\":\"key300\",\"value\":\"value300\"},{\"key\":\"key301\",\"value\":\"value301\"},{\"key\":\"key302\",\"value\":\"value302\"},{\"key\":\"key303\",\"value\":\"value303\"}]],\"u8\":114}";
    struct sol_json_token token;

    for (j = 0; j < 4; j++) {
        struct sol_vector *vec = sol_vector_append(&defval.v);

        ASSERT(vec);
        sol_vector_init(vec, sizeof(struct sol_key_value));
        for (i = 0; i < (j + 1); i++) {
            char *k, *v;

            r = asprintf(&k, "key%zd", i + j * 100);
            ASSERT(r > 0);

            r = asprintf(&v, "value%zd", i + j * 100);
            ASSERT(r > 0);

            kv = sol_vector_append(vec);
            ASSERT(kv);
            kv->key = k;
            kv->value = v;
        }
    }

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.v.len, defval.v.len);

    for (j = 0; j < defval.v.len; j++) {
        const struct sol_vector *vec_a = sol_vector_get(&a.v, j);
        const struct sol_vector *vec_b = sol_vector_get(&defval.v, j);

        ASSERT(vec_a);
        ASSERT(vec_b);
        ASSERT_INT_EQ(vec_a->len, vec_b->len);
    }

    r = sol_memdesc_compare(&desc, &a, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    for (j = 0; j < defval.v.len; j++) {
        const struct sol_vector *vec_a = sol_vector_get(&a.v, j);
        const struct sol_vector *vec_b = sol_vector_get(&defval.v, j);

        ASSERT(vec_a);
        ASSERT(vec_b);

        for (i = 0; i < vec_b->len; i++) {
            const struct sol_key_value *ita, *itb;

            ita = sol_vector_get(vec_a, i);
            ASSERT(ita);

            itb = sol_vector_get(vec_b, i);
            ASSERT(itb);

            ASSERT_STR_EQ(ita->key, itb->key);
            ASSERT_STR_EQ(ita->value, itb->value);
        }
    }

    r = sol_json_serialize_memdesc(&buf, &desc, &a, true);
    ASSERT_INT_EQ(r, 0);

    ASSERT_STR_EQ(buf.data, expected);

    sol_buffer_fini(&buf);

    sol_memdesc_free_content(&desc, &a);

    /* no default means an empty array, but elem_size must be set from children size */
    desc.defcontent.p = NULL;
    memset(&a, 0xff, sizeof(a));

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.v.len, 0);
    ASSERT_INT_EQ(a.v.elem_size, sizeof(struct sol_vector));
    ASSERT(!a.v.data);

    sol_json_token_init_from_slice(&token, sol_str_slice_from_str(expected));
    r = sol_json_load_memdesc(&token, &desc, &a);
    ASSERT_INT_EQ(r, 0);

    r = sol_memdesc_compare(&desc, &a, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    sol_memdesc_free_content(&desc, &a);

    for (j = 0; j < defval.v.len; j++) {
        struct sol_vector *vec = sol_vector_get(&defval.v, j);

        for (i = 0; i <  vec->len; i++) {
            kv = sol_vector_get(vec, i);
            free((void *)kv->key);
            free((void *)kv->value);
        }

        sol_vector_clear(vec);
    }
    sol_vector_clear(&defval.v);
}

TEST_MAIN();
