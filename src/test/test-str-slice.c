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

#include "sol-str-slice.h"
#include "sol-util.h"

#include "test.h"

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

    for (i = 0; i < ARRAY_SIZE(table); i++) {
        int error, value = 0;
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

#define TEST_EQUAL(X, CMP) { SOL_STR_SLICE_LITERAL(X), CMP, true }
#define TEST_NOT_EQUAL(X, CMP) { SOL_STR_SLICE_LITERAL(X), CMP, false }

    static const struct {
        struct sol_str_slice input;
        const char *cmp;
        bool output_value;
    } table[] = {
        TEST_EQUAL("0", "0"),
        TEST_EQUAL("wat", "wat"),
        TEST_NOT_EQUAL("this", "that"),
        TEST_NOT_EQUAL("thi", "this"),
        TEST_NOT_EQUAL("whatever", NULL),
    };

    for (i = 0; i < ARRAY_SIZE(table); i++) {
        bool ret;
        ret = sol_str_slice_str_eq(table[i].input, table[i].cmp);
        ASSERT_INT_EQ(ret, table[i].output_value);
    }
#undef TEST_EQUAL
#undef TEST_NOT_EQUAL
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
            "have no more memory availble, a slice that can't be coverted "
            "to raw C string, the infamous array of char, is not application "
            "main concern. I think that it's long enought, but maybe not. "
            "In hindsight, I believed that I've should used some lorem ipsum "
            "generator. Maybe I'll do that. Or not. Not sure really."),
        SOL_STR_SLICE_LITERAL("")
    };

    for (i = 0; i < ARRAY_SIZE(input); i++) {
        char *s = sol_str_slice_to_string(input[i]);
        ASSERT(sol_str_slice_str_eq(input[i], s));
        free(s);
    }
}

TEST_MAIN();
