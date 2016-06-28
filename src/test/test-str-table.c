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
#include <inttypes.h>

#include "sol-str-table.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"

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
