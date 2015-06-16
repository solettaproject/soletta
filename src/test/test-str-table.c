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
#include <inttypes.h>

#include "sol-str-table.h"
#include "sol-str-slice.h"
#include "sol-util.h"

#include "test.h"

DEFINE_TEST(test_str_table_simple);

static void
test_str_table_simple(void)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("t", -4),
        SOL_STR_TABLE_ITEM("te", -3),
        SOL_STR_TABLE_ITEM("tes", -2),
        SOL_STR_TABLE_ITEM("test", -1),
        SOL_STR_TABLE_ITEM("test0", 0),
        SOL_STR_TABLE_ITEM("test1", 1),
        { }
    };
    int16_t v;

    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test0"), &v));
    ASSERT_INT_EQ(v, 0);

    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test1"), &v));
    ASSERT_INT_EQ(v, 1);

    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test"), &v));
    ASSERT_INT_EQ(v, -1);

    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("tes"), &v));
    ASSERT_INT_EQ(v, -2);

    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("te"), &v));
    ASSERT_INT_EQ(v, -3);

    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("t"), &v));
    ASSERT_INT_EQ(v, -4);

    /* assign a default value first */
    v = 100;
    ASSERT(!sol_str_table_lookup(table, sol_str_slice_from_str("test9"), &v));
    ASSERT_INT_EQ(v, 100);
}

enum test1 {
    TEST_0,
    TEST_1,
    TEST_2,
    TEST_3,
    TEST_4,
    TEST_5,
    TEST_6,
    TEST_UNKNOWN = -1
};

DEFINE_TEST(test_str_table_enum_with_i16_lookup);

static void
test_str_table_enum_with_i16_lookup(void)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("test0", TEST_0),
        SOL_STR_TABLE_ITEM("test1", TEST_1),
        { }
    };
    int16_t v;

    v = TEST_UNKNOWN;
    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test0"), &v));
    ASSERT_INT_EQ(v, TEST_0);

    v = TEST_UNKNOWN;
    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test1"), &v));
    ASSERT_INT_EQ(v, TEST_1);

    v = TEST_UNKNOWN;
    ASSERT(!sol_str_table_lookup(table, sol_str_slice_from_str("test9"), &v));
    ASSERT_INT_EQ(v, TEST_UNKNOWN);
}

DEFINE_TEST(test_str_table_enum_with_enum_lookup);

static void
test_str_table_enum_with_enum_lookup(void)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("test0", TEST_0),
        SOL_STR_TABLE_ITEM("test1", TEST_1),
        { }
    };
    enum test1 v;

    v = TEST_UNKNOWN;
    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test0"), &v));
    ASSERT_INT_EQ(v, TEST_0);

    v = TEST_UNKNOWN;
    ASSERT(sol_str_table_lookup(table, sol_str_slice_from_str("test1"), &v));
    ASSERT_INT_EQ(v, TEST_1);

    v = TEST_UNKNOWN;
    ASSERT(!sol_str_table_lookup(table, sol_str_slice_from_str("test9"), &v));
    ASSERT_INT_EQ(v, TEST_UNKNOWN);
}

enum test2 {
    TEST2_UNKNOWN = 0,
    TEST2_0,
    TEST2_1,
    TEST2_2,
    TEST2_3,
    TEST2_4,
    TEST2_5,
    TEST2_6,
};

static const struct sol_str_table test_enum_table2[] = {
    SOL_STR_TABLE_ITEM("test0", TEST2_0),
    SOL_STR_TABLE_ITEM("test1", TEST2_1),
    SOL_STR_TABLE_ITEM("test2", TEST2_2),
    SOL_STR_TABLE_ITEM("test3", TEST2_3),
    SOL_STR_TABLE_ITEM("test4", TEST2_4),
    SOL_STR_TABLE_ITEM("test5", TEST2_5),
    { }
};

DEFINE_TEST(test_str_table_fallback);

static void
test_str_table_fallback(void)
{
    enum test2 v;

    v = sol_str_table_lookup_fallback(test_enum_table2,
        sol_str_slice_from_str("test0"),
        TEST2_UNKNOWN);
    ASSERT_INT_EQ(v, TEST2_0);

    v = sol_str_table_lookup_fallback(test_enum_table2,
        sol_str_slice_from_str("test1"),
        TEST2_UNKNOWN);
    ASSERT_INT_EQ(v, TEST2_1);

    v = sol_str_table_lookup_fallback(test_enum_table2,
        sol_str_slice_from_str("test9"),
        TEST2_UNKNOWN);
    ASSERT_INT_EQ(v, TEST2_UNKNOWN);

}


TEST_MAIN();
