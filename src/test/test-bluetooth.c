/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>

#include "sol-buffer.h"
#include "sol-str-slice.h"
#include "sol-bluetooth.h"

#include "test.h"

DEFINE_TEST(test_bluetooth_valid_uuid);

static void
test_bluetooth_valid_uuid(void)
{
    const char *list[] = { "1801", "FFFFFFFF", "00001800-0000-1000-8000-00805f9b34fb", NULL };
    const char **p;
    int r;

    for (p = list; p && *p; p++) {
        struct sol_bt_uuid uuid;

        r = sol_bt_uuid_from_str(&uuid, sol_str_slice_from_str(*p));
        ASSERT_INT_EQ(r, 0);
    }
}

DEFINE_TEST(test_bluetooth_invalid_uuid);

static void
test_bluetooth_invalid_uuid(void)
{
    const char *list[] = { "181", "FFFFFFFFG", "00001800-0000-1000-800000805f9b34fb1", NULL };
    const char **p;
    int r;

    for (p = list; p && *p; p++) {
        struct sol_bt_uuid uuid;

        r = sol_bt_uuid_from_str(&uuid, sol_str_slice_from_str(*p));
        ASSERT_INT_NE(r, 0);
    }
}

DEFINE_TEST(test_bluetooth_uuid_comparison);

static void
test_bluetooth_uuid_comparison(void)
{
    struct sol_bt_uuid u1, u2;
    struct sol_str_slice slice = SOL_STR_SLICE_LITERAL("00001801-0000-1000-8000-00805f9b34fb");
    struct sol_str_slice s;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    int r;

    u1.type = SOL_BT_UUID_TYPE_16;
    u1.val16 = 0x1801;

    r = sol_bt_uuid_from_str(&u2, slice);
    ASSERT_INT_EQ(r, 0);

    ASSERT(sol_bt_uuid_eq(&u1, &u2));

    u1.type = SOL_BT_UUID_TYPE_16;
    u1.val16 = 0x1802;

    ASSERT(!sol_bt_uuid_eq(&u1, &u2));

    r = sol_bt_uuid_to_str(&u2, &buffer);
    ASSERT_INT_EQ(r, 0);

    s = sol_buffer_get_slice(&buffer);

    ASSERT(sol_str_slice_eq(slice, s));

    sol_buffer_fini(&buffer);
}

TEST_MAIN();
