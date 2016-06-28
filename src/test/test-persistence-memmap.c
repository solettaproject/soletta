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

#include "sol-mainloop.h"
#include "sol-log.h"
#include "sol-memmap-storage.h"

#include "test.h"

SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry0, 2, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry1, 3, 1, 0, 1);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry2, 3, 4, 1, 30);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry3, 0, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry4, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry5, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry6, 0, 10, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry7, 0, 32, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry8, 0, 8, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry9, 0, 32, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry10, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry11, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry12, 0, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry13, 0, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry14, 0, 10, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry15, 0, 32, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map0_entry16, 0, 32, 0, 0);

static const struct sol_str_table_ptr _memmap0_entries[] = {
    SOL_STR_TABLE_PTR_ITEM("_version", &map0_entry0),
    SOL_STR_TABLE_PTR_ITEM("boolean", &map0_entry1),
    SOL_STR_TABLE_PTR_ITEM("int_only_val", &map0_entry2),
    SOL_STR_TABLE_PTR_ITEM("byte", &map0_entry3),
    SOL_STR_TABLE_PTR_ITEM("int", &map0_entry4),
    SOL_STR_TABLE_PTR_ITEM("irange", &map0_entry5),
    SOL_STR_TABLE_PTR_ITEM("string", &map0_entry6),
    SOL_STR_TABLE_PTR_ITEM("double", &map0_entry7),
    SOL_STR_TABLE_PTR_ITEM("double_only_val", &map0_entry8),
    SOL_STR_TABLE_PTR_ITEM("drange", &map0_entry9),
    SOL_STR_TABLE_PTR_ITEM("int_def", &map0_entry10),
    SOL_STR_TABLE_PTR_ITEM("irange_def", &map0_entry11),
    SOL_STR_TABLE_PTR_ITEM("byte_def", &map0_entry12),
    SOL_STR_TABLE_PTR_ITEM("boolean_def", &map0_entry13),
    SOL_STR_TABLE_PTR_ITEM("string_def", &map0_entry14),
    SOL_STR_TABLE_PTR_ITEM("double_def", &map0_entry15),
    SOL_STR_TABLE_PTR_ITEM("drange_def", &map0_entry16),
    { }
};

static const struct sol_memmap_map _memmap0 = {
    .version = 1,
    .path = "memmap-test.bin",
    .entries = _memmap0_entries
};

SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry0, 2, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry1, 3, 1, 0, 1);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry2, 3, 4, 1, 30);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry3, 0, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry4, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry5, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry6, 0, 10, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry7, 0, 32, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry8, 0, 8, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry9, 0, 32, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry10, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry11, 0, 16, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry12, 0, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry13, 0, 1, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry14, 0, 10, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry15, 0, 32, 0, 0);
SOL_MEMMAP_ENTRY_BIT_SIZE(map1_entry16, 0, 32, 0, 0);

static const struct sol_str_table_ptr _memmap1_entries[] = {
    SOL_STR_TABLE_PTR_ITEM("_version", &map1_entry0),
    SOL_STR_TABLE_PTR_ITEM("boolean2", &map1_entry1),
    SOL_STR_TABLE_PTR_ITEM("int_only_val2", &map1_entry2),
    SOL_STR_TABLE_PTR_ITEM("byte2", &map1_entry3),
    SOL_STR_TABLE_PTR_ITEM("int2", &map1_entry4),
    SOL_STR_TABLE_PTR_ITEM("irange2", &map1_entry5),
    SOL_STR_TABLE_PTR_ITEM("string2", &map1_entry6),
    SOL_STR_TABLE_PTR_ITEM("double2", &map1_entry7),
    SOL_STR_TABLE_PTR_ITEM("double_only_val2", &map1_entry8),
    SOL_STR_TABLE_PTR_ITEM("drange2", &map1_entry9),
    SOL_STR_TABLE_PTR_ITEM("int_def2", &map1_entry10),
    SOL_STR_TABLE_PTR_ITEM("irange_def2", &map1_entry11),
    SOL_STR_TABLE_PTR_ITEM("byte_def2", &map1_entry12),
    SOL_STR_TABLE_PTR_ITEM("boolean_def2", &map1_entry13),
    SOL_STR_TABLE_PTR_ITEM("string_def2", &map1_entry14),
    SOL_STR_TABLE_PTR_ITEM("double_def2", &map1_entry15),
    SOL_STR_TABLE_PTR_ITEM("drange_def2", &map1_entry16),
    { }
};

static const struct sol_memmap_map _memmap1 = {
    .version = 1,
    .path = "memmap-test2.bin",
    .entries = _memmap1_entries
};

static struct sol_irange irange_not_delayed = {
    .val = -23,
    .min = -1000,
    .max = 1000,
    .step = 1
};

static struct sol_drange drange_not_delayed = {
    .val = -2.3,
    .min = -100.0,
    .max = 100.0,
    .step = 0.1
};

static struct sol_irange irange_delayed = {
    .val = -33,
    .min = -10000,
    .max = 10000,
    .step = 3
};

static struct sol_drange drange_delayed = {
    .val = -9.8,
    .min = -1000.0,
    .max = 1000.0,
    .step = 0.2
};

static void
write_cb(void *data, const char *name, struct sol_blob *blob, int status)
{
    ASSERT_INT_EQ(status, 0);
}

static void
write_cancelled_cb(void *data, const char *name, struct sol_blob *blob, int status)
{
    ASSERT_INT_EQ(status, -ECANCELED);
}

static void
write_one(void)
{
    int r;

    r = sol_memmap_write_bool("boolean", true, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_uint8("byte", 78, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_int32("int_only_val", 7804, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_irange("irange", &irange_not_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_drange("drange", &drange_not_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_double("double_only_val", 97.36, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_string("string", "gama delta", write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_bool("boolean2", true, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_uint8("byte2", 78, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_int32("int_only_val2", 7804, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_irange("irange2", &irange_not_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_drange("drange2", &drange_not_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_double("double_only_val2", 97.36, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_string("string2", "gama delta", write_cb, NULL);
    ASSERT_INT_EQ(r, 0);
}

static void
read_one(void)
{
    int r, i;
    struct sol_irange irange;
    struct sol_drange drange;
    double d;
    bool b;
    uint8_t u;
    char *string;

    r = sol_memmap_read_bool("boolean", &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b);

    r = sol_memmap_read_uint8("byte", &u);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(u, 78);

    r = sol_memmap_read_int32("int_only_val", &i);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(i, 7804);

    r = sol_memmap_read_irange("irange", &irange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_irange_eq(&irange, &irange_not_delayed));

    r = sol_memmap_read_drange("drange", &drange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_eq(&drange, &drange_not_delayed));

    r = sol_memmap_read_double("double_only_val", &d);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_eq(d, 97.36));

    r = sol_memmap_read_string("string", &string);
    ASSERT_INT_EQ(r, 0);
    ASSERT_STR_EQ(string, "gama delta");
    free(string);

    r = sol_memmap_read_bool("boolean2", &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b);

    r = sol_memmap_read_uint8("byte2", &u);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(u, 78);

    r = sol_memmap_read_int32("int_only_val2", &i);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(i, 7804);

    r = sol_memmap_read_irange("irange2", &irange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_irange_eq(&irange, &irange_not_delayed));

    r = sol_memmap_read_drange("drange2", &drange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_eq(&drange, &drange_not_delayed));

    r = sol_memmap_read_double("double_only_val2", &d);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_eq(d, 97.36));

    r = sol_memmap_read_string("string2", &string);
    ASSERT_INT_EQ(r, 0);
    ASSERT_STR_EQ(string, "gama delta");
    free(string);
}

static void
write_two(void)
{
    int r;

    r = sol_memmap_write_bool("boolean", false, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_uint8("byte", 88, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_int32("int_only_val", 7814, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_irange("irange", &irange_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_drange("drange", &drange_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_double("double_only_val", 107.36, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_string("string", "alfa beta", write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_bool("boolean2", false, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_uint8("byte2", 88, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_int32("int_only_val2", 7814, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_irange("irange2", &irange_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_drange("drange2", &drange_delayed, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_double("double_only_val2", 107.36, write_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_string("string2", "alfa beta", write_cb, NULL);
    ASSERT_INT_EQ(r, 0);
}

static void
read_two(void)
{
    int r, i;
    struct sol_irange irange;
    struct sol_drange drange;
    double d;
    bool b;
    uint8_t u;
    char *string;

    r = sol_memmap_read_bool("boolean", &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(!b);

    r = sol_memmap_read_uint8("byte", &u);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(u, 88);

    r = sol_memmap_read_int32("int_only_val", &i);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(i, 7814);

    r = sol_memmap_read_irange("irange", &irange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_irange_eq(&irange, &irange_delayed));

    r = sol_memmap_read_drange("drange", &drange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_eq(&drange, &drange_delayed));

    r = sol_memmap_read_double("double_only_val", &d);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_eq(d, 107.36));

    r = sol_memmap_read_string("string", &string);
    ASSERT_INT_EQ(r, 0);
    ASSERT_STR_EQ(string, "alfa beta");
    free(string);

    r = sol_memmap_read_bool("boolean2", &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(!b);

    r = sol_memmap_read_uint8("byte2", &u);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(u, 88);

    r = sol_memmap_read_int32("int_only_val2", &i);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(i, 7814);

    r = sol_memmap_read_irange("irange2", &irange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_irange_eq(&irange, &irange_delayed));

    r = sol_memmap_read_drange("drange2", &drange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_eq(&drange, &drange_delayed));

    r = sol_memmap_read_double("double_only_val2", &d);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_eq(d, 107.36));

    r = sol_memmap_read_string("string2", &string);
    ASSERT_INT_EQ(r, 0);
    ASSERT_STR_EQ(string, "alfa beta");
    free(string);
}

static bool
read_two_after(void *data)
{
    read_two();

    return false;
}

static void
write_one_cancelled(void)
{
    int r;

    r = sol_memmap_write_bool("boolean", true, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_uint8("byte", 78, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_int32("int_only_val", 7804, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_irange("irange", &irange_not_delayed, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_drange("drange", &drange_not_delayed, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_double("double_only_val", 97.36, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_string("string", "gama delta", write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_bool("boolean2", true, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_uint8("byte2", 78, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_int32("int_only_val2", 7804, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_irange("irange2", &irange_not_delayed, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_drange("drange2", &drange_not_delayed, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_double("double_only_val2", 97.36, write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);

    r = sol_memmap_write_string("string2", "gama delta", write_cancelled_cb, NULL);
    ASSERT_INT_EQ(r, 0);
}

static bool
write_two_timeout(void *data)
{
    write_two();
    read_two();

    return false;
}

static bool
read_one_after_mainloop(void *data)
{
    read_one();

    return false;
}

static bool
write_cancelled_timeout(void *data)
{
    write_one_cancelled();
    read_one(); /* write_one_cancelled writes same values as write_one */

    write_two(); /* reuse second part of test */
    read_two();

    sol_quit();

    return false;
}

static bool
perform_tests(void *data)
{
    char command[128];
    int n;

    sol_memmap_add_map(&_memmap0);
    sol_memmap_add_map(&_memmap1);

    n = snprintf(command, sizeof(command), "truncate -s0 %s && truncate -s128 %s",
        _memmap0.path, _memmap0.path);
    ASSERT(n > 0 && (size_t)n < sizeof(command));

    n = system(command);
    ASSERT(!n);

    n = snprintf(command, sizeof(command), "truncate -s0 %s && truncate -s128 %s",
        _memmap1.path, _memmap1.path);
    ASSERT(n > 0 && (size_t)n < sizeof(command));

    n = system(command);
    ASSERT(!n);

    write_one();
    read_one(); /* This one should happen before actually writing data */
    sol_timeout_add(0, read_one_after_mainloop, NULL); /* This one should be after a main loop */
    sol_timeout_add(50, write_two_timeout, NULL); /* This, much after */
    sol_timeout_add(1000, read_two_after, NULL); /* Even later */

    sol_timeout_add(2000, write_cancelled_timeout, NULL);

    return false;
}

int
main(int argc, char *argv[])
{
    int err;

    err = sol_init();
    ASSERT(!err);

    sol_idle_add(perform_tests, NULL);

    sol_run();

    sol_shutdown();

    return 0;
}
