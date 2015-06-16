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

#include <string.h>

#include "sol-monitors.h"
#include "sol-util.h"

#include "test.h"

struct counters {
    int a_func_called;
    int b_func_called;
    int cleanup_called;
};

static struct counters counters;

static void
reset_counters(void)
{
    memset(&counters, 0, sizeof(struct counters));
}

static void
a_func(const void *data)
{
    ASSERT(data == a_func);
    counters.a_func_called++;
}

static void
b_func(const void *data)
{
    ASSERT(data == b_func);
    counters.b_func_called++;
}

DEFINE_TEST(walk_monitors_to_callback_functions);

static void
walk_monitors_to_callback_functions(void)
{
    struct sol_monitors ms_, *ms;
    struct sol_monitors_entry *e;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, NULL);
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);

    e = sol_monitors_append(ms, a_func, a_func);
    ASSERT(e);
    ASSERT_INT_EQ(sol_monitors_count(ms), 1);

    e = sol_monitors_append(ms, b_func, b_func);
    ASSERT(e);
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);

    SOL_MONITORS_WALK_AND_CALLBACK(ms);

    ASSERT_INT_EQ(counters.a_func_called, 1);
    ASSERT_INT_EQ(counters.b_func_called, 1);

    sol_monitors_clear(ms);
}

static const int the_value = 22;

static void
a_different_func(bool something, int value, const void *data)
{
    ASSERT(!data);
    ASSERT_INT_EQ(value, the_value);
    counters.a_func_called++;
}

static void
b_different_func(bool something, int value, const void *data)
{
    ASSERT(!data);
    ASSERT_INT_EQ(value, the_value);
    counters.b_func_called++;
}

DEFINE_TEST(use_different_callback_types);

static void
use_different_callback_types(void)
{
    struct sol_monitors ms_, *ms;
    struct sol_monitors_entry *e;
    uint16_t i;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, NULL);

    e = sol_monitors_append(ms, (sol_monitors_cb_t)a_different_func, NULL);
    ASSERT(e);

    e = sol_monitors_append(ms, (sol_monitors_cb_t)b_different_func, NULL);
    ASSERT(e);

    SOL_MONITORS_WALK (ms, e, i) {
        if (e->cb)
            ((void (*)(bool, int, const void *))e->cb)(true, the_value, e->data);
    }

    ASSERT_INT_EQ(counters.a_func_called, 1);
    ASSERT_INT_EQ(counters.b_func_called, 1);

    sol_monitors_clear(ms);
}

static void
cleanup(const struct sol_monitors *ms, const struct sol_monitors_entry *e)
{
    ASSERT(!e->cb);
    counters.cleanup_called++;
}

DEFINE_TEST(delete_is_not_deferred_when_not_walking);

static void
delete_is_not_deferred_when_not_walking(void)
{
    struct sol_monitors ms_, *ms;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, cleanup);
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);

    sol_monitors_append(ms, a_func, a_func);
    sol_monitors_append(ms, b_func, b_func);
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);

    sol_monitors_del(ms, 1);
    ASSERT_INT_EQ(sol_monitors_count(ms), 1);
    ASSERT_INT_EQ(counters.cleanup_called, 1);

    sol_monitors_del(ms, sol_monitors_find(ms, a_func, a_func));
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);
    ASSERT_INT_EQ(counters.cleanup_called, 2);

    sol_monitors_clear(ms);
}

DEFINE_TEST(delete_is_deferred_when_walking_monitors);

static void
delete_is_deferred_when_walking_monitors(void)
{
    struct sol_monitors ms_, *ms;
    struct sol_monitors_entry *e;
    uint16_t i;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, cleanup);

    e = sol_monitors_append(ms, a_func, a_func);
    ASSERT(e);

    e = sol_monitors_append(ms, b_func, b_func);
    ASSERT(e);

    e = sol_monitors_get(ms, 1);
    ASSERT(e->cb);

    SOL_MONITORS_WALK (ms, e, i) {
        switch (i) {
        case 0:
            /* Delete element 1, cleanup is called but count is still the same... */
            ASSERT_INT_EQ(sol_monitors_count(ms), 2);
            sol_monitors_del(ms, 1);
            ASSERT_INT_EQ(sol_monitors_count(ms), 2);
            ASSERT_INT_EQ(counters.cleanup_called, 1);
            break;

        case 1:
            /* ...but when walk continues it is already marked invalid. */
            ASSERT(!e->cb);
            break;
        }
    }

    /* After the walk, it is deleted from the list. */
    ASSERT_INT_EQ(sol_monitors_count(ms), 1);

    sol_monitors_clear(ms);
}

DEFINE_TEST(delete_is_deferred_when_walking_multiple_monitors);

static void
delete_is_deferred_when_walking_multiple_monitors(void)
{
    struct sol_monitors ms_, *ms;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, cleanup);
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);

    sol_monitors_append(ms, a_func, a_func);
    sol_monitors_append(ms, b_func, b_func);
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);

    sol_monitors_begin_walk(ms);
    sol_monitors_begin_walk(ms);

    sol_monitors_del(ms, 1);
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);
    ASSERT_INT_EQ(counters.cleanup_called, 1);

    /* Even after ending one walk, there's another, so count won't change. */
    sol_monitors_end_walk(ms);
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);

    sol_monitors_del(ms, sol_monitors_find(ms, a_func, a_func));
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);
    ASSERT_INT_EQ(counters.cleanup_called, 2);

    sol_monitors_end_walk(ms);
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);
    ASSERT_INT_EQ(counters.cleanup_called, 2);

    sol_monitors_clear(ms);
}

DEFINE_TEST(find_by_callback_and_data);

static void
find_by_callback_and_data(void)
{
    struct sol_monitors ms_, *ms;
    int i;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, NULL);
    for (i = 0; i < 10; i++)
        sol_monitors_append(ms, a_func, INT_TO_PTR(i * 10));

    ASSERT_INT_EQ(sol_monitors_find(ms, NULL, NULL), -ENOENT);
    ASSERT_INT_EQ(sol_monitors_find(ms, b_func, INT_TO_PTR(0)), -ENOENT);
    ASSERT_INT_EQ(sol_monitors_find(ms, a_func, INT_TO_PTR(0)), 0);
    ASSERT_INT_EQ(sol_monitors_find(ms, a_func, INT_TO_PTR(1)), -ENOENT);
    ASSERT_INT_EQ(sol_monitors_find(ms, a_func, INT_TO_PTR(10)), 1);
    ASSERT_INT_EQ(sol_monitors_find(ms, b_func, INT_TO_PTR(10)), -ENOENT);
    ASSERT_INT_EQ(sol_monitors_find(ms, a_func, INT_TO_PTR(90)), 9);

    sol_monitors_clear(ms);
}

DEFINE_TEST(clear_calls_cleanup);

static void
clear_calls_cleanup(void)
{
    struct sol_monitors ms_, *ms;

    reset_counters();
    ms = &ms_;

    sol_monitors_init(ms, cleanup);
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);

    sol_monitors_append(ms, a_func, a_func);
    sol_monitors_append(ms, b_func, b_func);
    ASSERT_INT_EQ(sol_monitors_count(ms), 2);

    sol_monitors_clear(ms);
    ASSERT_INT_EQ(sol_monitors_count(ms), 0);
    ASSERT_INT_EQ(counters.cleanup_called, 2);
}

struct custom_entry {
    struct sol_monitors_entry base;

    /* Enough to cause noise if traversal is wrong. */
    int noise[16];
};

DEFINE_TEST(custom_entry_type);

static void
custom_entry_type(void)
{
    struct sol_monitors ms_, *ms;
    struct custom_entry *e;
    struct sol_monitors_entry *base_entry;
    uint16_t i;

    reset_counters();
    ms = &ms_;

    sol_monitors_init_custom(ms, sizeof(struct custom_entry), NULL);

    for (i = 0; i < 16; i++) {
        e = sol_monitors_append(ms, a_func, INT_TO_PTR(i));
        memset(e->noise, -1, 16 * sizeof(int));
    }

    for (i = 0; i < sol_monitors_count(ms); i++) {
        e = sol_monitors_get(ms, i);
        ASSERT_INT_EQ(PTR_TO_INT(e->base.data), i);
    }

    SOL_MONITORS_WALK (ms, e, i)
        ASSERT_INT_EQ(PTR_TO_INT(e->base.data), i);

    /* Can walk using the pointer to base entry type, too. */
    SOL_MONITORS_WALK (ms, base_entry, i)
        ASSERT_INT_EQ(PTR_TO_INT(base_entry->data), i);

    sol_monitors_clear(ms);
}


TEST_MAIN();
