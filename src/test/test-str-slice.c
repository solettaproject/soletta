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

#include "sol-str-slice.h"
#include "sol-util-internal.h"

#include "test.h"

DEFINE_TEST(test_str_slice_split_iterate);

static void
test_str_slice_split_iterate(void)
{
    size_t i;

    static const struct {
        struct sol_str_slice to_split;
        const char *delim;
        size_t iterations;
        const char *tokens[10];
    } table[] = {
        { SOL_STR_SLICE_LITERAL("something"), ";", 1, { "something", NULL } },
        { SOL_STR_SLICE_LITERAL("something;i like it"), ";", 2, { "something", "i like it", NULL } },
        { SOL_STR_SLICE_LITERAL("something;i like it;"), ";", 3, { "something", "i like it", "", NULL } },
        { SOL_STR_SLICE_LITERAL("something;i like it;&&;1233;2;31"), ";", 6, { "something", "i like it", "&&", "1233", "2", "31", NULL } },
        { SOL_STR_SLICE_LITERAL("something;i like it;&&;1233;2;31"), "&&", 2, { "something;i like it;", ";1233;2;31", NULL } },
        { SOL_STR_SLICE_LITERAL("HelloThisIsMyDelimiterByeThisIsMyDelimiterWhatAHugeDelimiter"), "ThisIsMyDelimiter",
          3, { "Hello", "Bye", "WhatAHugeDelimiter", NULL } }
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        struct sol_str_slice token = SOL_STR_SLICE_EMPTY;
        const char *itr = NULL;
        size_t iterations = 0;

        while (sol_str_slice_str_split_iterate(table[i].to_split, &token, &itr, table[i].delim)) {
            ASSERT(table[i].tokens[iterations]);
            ASSERT(sol_str_slice_str_eq(token, table[i].tokens[iterations++]));
        }

        ASSERT_INT_EQ(iterations, table[i].iterations);
    }
}

DEFINE_TEST(test_str_slice_to_int);

static void
test_str_slice_to_int(void)
{
    unsigned int i;

#define CONVERT_OK(X) { SOL_STR_SLICE_LITERAL(#X), 0, X }
#define CONVERT_FAIL(X, ERR) { SOL_STR_SLICE_LITERAL(#X), ERR, 0 }

    static const struct {
        struct sol_str_slice input;
        int output_error;
        int output_value;
    } table[] = {
        CONVERT_OK(0),
        CONVERT_OK(100),
        CONVERT_OK(-1),
        CONVERT_OK(100000),
        CONVERT_OK(0xFF),
        CONVERT_OK(0755),
        CONVERT_FAIL(20000000000, -ERANGE),
        CONVERT_FAIL(abc, -EINVAL),
        CONVERT_FAIL(10abc, -EINVAL),
        CONVERT_FAIL(-abc, -EINVAL),
        CONVERT_FAIL(10000000000000000000, -ERANGE),
        CONVERT_FAIL(100000000000000000000000000000, -ERANGE),
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        int error;
        long int value = 0;
        error = sol_str_slice_to_int(table[i].input, &value);
        ASSERT_INT_EQ(error, table[i].output_error);
        ASSERT_INT_EQ(value, table[i].output_value);
    }

#undef CONVERT_OK
#undef CONVERT_FAIL
}

DEFINE_TEST(test_str_slice_str_eq);

static void
test_str_slice_str_eq(void)
{
    unsigned int i;

#define TEST_EQ(X, CMP) { SOL_STR_SLICE_LITERAL(X), CMP, true }
#define TEST_NOT_EQ(X, CMP) { SOL_STR_SLICE_LITERAL(X), CMP, false }

    static const struct {
        struct sol_str_slice input;
        const char *cmp;
        bool output_value;
    } table[] = {
        TEST_EQ("0", "0"),
        TEST_EQ("wat", "wat"),
        TEST_NOT_EQ("this", "that"),
        TEST_NOT_EQ("thi", "this"),
        TEST_NOT_EQ("whatever", NULL),
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        bool ret;
        ret = sol_str_slice_str_eq(table[i].input, table[i].cmp);
        ASSERT_INT_EQ(ret, table[i].output_value);
    }
#undef TEST_EQ
#undef TEST_NOT_EQ
}

DEFINE_TEST(test_str_slice_remove_leading_whitespace);

static void
test_str_slice_remove_leading_whitespace(void)
{
    unsigned int i;

#define TEST_EQ(X) { SOL_STR_SLICE_LITERAL(X), true }
#define TEST_NOT_EQ(X) { SOL_STR_SLICE_LITERAL(X), false }
#define TEST_NOT_EQ_SHIFT(X, S) { SOL_STR_SLICE_LITERAL(X + S), false }

    static const struct {
        struct sol_str_slice input;
        bool equal;
    } table[] = {
        TEST_NOT_EQ(" with one leading whitespace"),
        TEST_NOT_EQ("  with two leading whitespace"),
        TEST_NOT_EQ(" "),
        TEST_NOT_EQ("\twith one leading whitespace"),
        TEST_NOT_EQ("\t\twith two leading whitespace"),
        TEST_NOT_EQ("\t"),
        TEST_NOT_EQ("\nwith one leading whitespace"),
        TEST_NOT_EQ("\n\nwith two leading whitespace"),
        TEST_NOT_EQ("\n"),
        TEST_NOT_EQ_SHIFT("        with leading whitespace and shifted", 4),
        TEST_EQ(""),
        TEST_EQ("without leading whitespace"),
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        struct sol_str_slice slice;
        slice = sol_str_slice_remove_leading_whitespace(table[i].input);
        ASSERT(sol_str_slice_eq(table[i].input, slice) == table[i].equal);
    }

#undef TEST_EQ
#undef TEST_NOT_EQ
}

DEFINE_TEST(test_str_slice_remove_trailing_whitespace);

static void
test_str_slice_remove_trailing_whitespace(void)
{
    unsigned int i;

#define TEST_EQ(X) { SOL_STR_SLICE_LITERAL(X), true }
#define TEST_NOT_EQ(X) { SOL_STR_SLICE_LITERAL(X), false }

    static const struct {
        struct sol_str_slice input;
        bool equal;
    } table[] = {
        TEST_NOT_EQ("with one trailing whitespace "),
        TEST_NOT_EQ("with two trailing whitespace  "),
        TEST_NOT_EQ(" "),
        TEST_NOT_EQ("with one trailing whitespace\t"),
        TEST_NOT_EQ("with two trailing whitespace\t\t"),
        TEST_NOT_EQ("\t"),
        TEST_NOT_EQ("with one trailing whitespace\n"),
        TEST_NOT_EQ("with two trailing whitespace\n\n"),
        TEST_NOT_EQ("\n"),
        TEST_EQ(""),
        TEST_EQ("without trailing whitespace"),
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        struct sol_str_slice slice;
        slice = sol_str_slice_remove_trailing_whitespace(table[i].input);
        ASSERT(sol_str_slice_eq(table[i].input, slice) == table[i].equal);
    }

#undef TEST_EQ
#undef TEST_NOT_EQ
}

DEFINE_TEST(test_str_slice_trim);

static void
test_str_slice_trim(void)
{
    unsigned int i;

#define TEST_EQ(X) { SOL_STR_SLICE_LITERAL(X), true }
#define TEST_NOT_EQ(X) { SOL_STR_SLICE_LITERAL(X), false }

    static const struct {
        struct sol_str_slice input;
        bool equal;
    } table[] = {
        TEST_NOT_EQ("with one trailing whitespace "),
        TEST_NOT_EQ("with two trailing whitespace  "),
        TEST_NOT_EQ(" "),
        TEST_NOT_EQ("with one trailing whitespace\t"),
        TEST_NOT_EQ("with two trailing whitespace\t\t"),
        TEST_NOT_EQ("\t"),
        TEST_NOT_EQ("with one trailing whitespace\n"),
        TEST_NOT_EQ("with two trailing whitespace\n\n"),
        TEST_NOT_EQ("\n"),
        TEST_NOT_EQ(" with one whitespace "),
        TEST_NOT_EQ("  with two whitespace  "),
        TEST_NOT_EQ("\twith one whitespace\t"),
        TEST_NOT_EQ("\t\twith two whitespace\t\t"),
        TEST_NOT_EQ("\nwith one whitespace\n"),
        TEST_NOT_EQ("\n\nwith two whitespace\n\n"),
        TEST_EQ(""),
        TEST_EQ("without trailing whitespace"),
    };

    for (i = 0; i < sol_util_array_size(table); i++) {
        struct sol_str_slice slice;
        slice = sol_str_slice_trim(table[i].input);
        ASSERT(sol_str_slice_eq(table[i].input, slice) == table[i].equal);
    }

#undef TEST_EQ
#undef TEST_NOT_EQ
}

DEFINE_TEST(test_str_slice_to_string);

static void
test_str_slice_to_string(void)
{
    unsigned int i;

    struct sol_str_slice input[] = {
        SOL_STR_SLICE_LITERAL("alfa"),
        SOL_STR_SLICE_LITERAL("a a"),
        SOL_STR_SLICE_LITERAL("This is supposed to be a big string, "
            "spanning long enought that it could be considered a long string, "
            "whose only purpose is to test if a long slice can yeld to a"
            "correct string. But why not? Maybe allocation problems, however, "
            "are allocations problems something to be concerned at? If we "
            "have no more memory available, a slice that can't be converted "
            "to raw C string, the infamous array of char, is not application "
            "main concern. I think that it's long enought, but maybe not. "
            "In hindsight, I believed that I've should used some lorem ipsum "
            "generator. Maybe I'll do that. Or not. Not sure really."),
        SOL_STR_SLICE_LITERAL("")
    };

    for (i = 0; i < sol_util_array_size(input); i++) {
        char *s = sol_str_slice_to_str(input[i]);
        ASSERT(sol_str_slice_str_eq(input[i], s));
        free(s);
    }
}

TEST_MAIN();
