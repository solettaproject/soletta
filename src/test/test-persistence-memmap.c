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

#include "sol-mainloop.h"
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
    ASSERT(sol_irange_equal(&irange, &irange_not_delayed));

    r = sol_memmap_read_drange("drange", &drange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_equal(&drange, &drange_not_delayed));

    r = sol_memmap_read_double("double_only_val", &d);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_val_equal(d, 97.36));

    r = sol_memmap_read_string("string", &string);
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
    ASSERT(sol_irange_equal(&irange, &irange_delayed));

    r = sol_memmap_read_drange("drange", &drange);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_equal(&drange, &drange_delayed));

    r = sol_memmap_read_double("double_only_val", &d);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_drange_val_equal(d, 107.36));

    r = sol_memmap_read_string("string", &string);
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

    n = snprintf(command, sizeof(command), "truncate -s0 %s && truncate -s128 %s",
        _memmap0.path, _memmap0.path);
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
