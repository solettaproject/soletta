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

#include <inttypes.h>
#include <stdio.h>

#include <sol-bluetooth.h>
#include <sol-buffer.h>
#include <sol-log.h>
#include <sol-str-table.h>
#include <sol-util.h>

#define BASE_UUID { .type = SOL_BT_UUID_TYPE_128, \
                    .val128 = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, \
                                0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB, } \
}

static int
string_to_uuid128(struct sol_bt_uuid *uuid, const char *str)
{
    uint32_t data0, data4;
    uint16_t data1, data2, data3, data5;
    uint8_t *val = uuid->val128;

    if (sscanf(str, "%08" SCNx32 "-%04" SCNx16 "-%04" SCNx16 "-%04" SCNx16 "-%08" SCNx32 "%04" SCNx16,
        &data0, &data1, &data2,
        &data3, &data4, &data5) != 6)
        return -EINVAL;

    data0 = sol_util_be32_to_cpu(data0);
    data1 = sol_util_be16_to_cpu(data1);
    data2 = sol_util_be16_to_cpu(data2);
    data3 = sol_util_be16_to_cpu(data3);
    data4 = sol_util_be32_to_cpu(data4);
    data5 = sol_util_be16_to_cpu(data5);

    memcpy(&val[0], &data0, 4);
    memcpy(&val[4], &data1, 2);
    memcpy(&val[6], &data2, 2);
    memcpy(&val[8], &data3, 2);
    memcpy(&val[10], &data4, 4);
    memcpy(&val[14], &data5, 2);

    return 0;
}

static void
uuid16_to_uuid128(const struct sol_bt_uuid *u16, struct sol_bt_uuid *u128)
{
    uint16_t val;

    val = sol_util_cpu_to_be16(u16->val16);
    memcpy(&u128->val128[2], &val, sizeof(val));
}

static void
uuid32_to_uuid128(const struct sol_bt_uuid *u32, struct sol_bt_uuid *u128)
{
    uint32_t val;

    val = sol_util_cpu_to_be32(u32->val32);
    memcpy(&u128->val128[0], &val, sizeof(val));
}

static void
uuid_to_uuid128(const struct sol_bt_uuid *u, struct sol_bt_uuid *u128)
{
    switch (u->type) {
    case SOL_BT_UUID_TYPE_128:
        *u128 = *u;
        break;
    case SOL_BT_UUID_TYPE_32:
        uuid32_to_uuid128(u, u128);
        break;
    case SOL_BT_UUID_TYPE_16:
        uuid16_to_uuid128(u, u128);
        break;
    }
}

SOL_API int
sol_bt_uuid_from_str(struct sol_bt_uuid *uuid, const struct sol_str_slice str)
{
    int r;

    SOL_NULL_CHECK(uuid, -EINVAL);

    switch (str.len) {
    case 4:
        uuid->type = SOL_BT_UUID_TYPE_16;
        uuid->val16 = strtoul(str.data, NULL, 16);
        break;
    case 8:
        uuid->type = SOL_BT_UUID_TYPE_32;
        uuid->val32 = strtoull(str.data, NULL, 16);
        break;
    case 36:
        uuid->type = SOL_BT_UUID_TYPE_128;
        r = string_to_uuid128(uuid, str.data);
        SOL_INT_CHECK(r, < 0, r);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

SOL_API int
sol_bt_uuid_to_str(const struct sol_bt_uuid *uuid, struct sol_buffer *buffer)
{
    uint32_t tmp0, tmp4;
    uint16_t tmp1, tmp2, tmp3, tmp5;
    struct sol_bt_uuid u = BASE_UUID;
    int r;

    SOL_NULL_CHECK(uuid, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    uuid_to_uuid128(uuid, &u);

    memcpy(&tmp0, &u.val128[0], sizeof(tmp0));
    memcpy(&tmp1, &u.val128[4], sizeof(tmp1));
    memcpy(&tmp2, &u.val128[6], sizeof(tmp2));
    memcpy(&tmp3, &u.val128[8], sizeof(tmp3));
    memcpy(&tmp4, &u.val128[10], sizeof(tmp4));
    memcpy(&tmp5, &u.val128[14], sizeof(tmp5));

    r = sol_buffer_append_printf(buffer, "%.8" PRIx32 "-%.4" PRIx16 "-%.4" PRIx16 "-%.4" PRIx16 "-%.8" PRIx32 "%.4" PRIx16,
        sol_util_cpu_to_be32(tmp0), sol_util_cpu_to_be16(tmp1),
        sol_util_cpu_to_be16(tmp2), sol_util_cpu_to_be16(tmp3),
        sol_util_cpu_to_be32(tmp4), sol_util_cpu_to_be16(tmp5));

    return r ? -errno : 0;
}

SOL_API bool
sol_bt_uuid_eq(const struct sol_bt_uuid *u1, const struct sol_bt_uuid *u2)
{
    struct sol_bt_uuid u1_128 = BASE_UUID;
    struct sol_bt_uuid u2_128 = BASE_UUID;

    if (!u1 && !u2)
        return true;

    if (!u1 || !u2)
        return false;

    if (u1->type == u2->type)
        return memcmp(u1->val, u2->val, u1->type) == 0;

    uuid_to_uuid128(u1, &u1_128);
    uuid_to_uuid128(u2, &u2_128);

    return memcmp(u1_128.val128, u2_128.val128, u1_128.type) == 0;
}

SOL_API const char *
sol_bt_transport_to_str(enum sol_bt_transport transport)
{
    static const char *transports[] = {
        [SOL_BT_TRANSPORT_ALL] = "all",
        [SOL_BT_TRANSPORT_LE] = "le",
        [SOL_BT_TRANSPORT_BREDR] = "bredr",
    };

    if (transport < sol_util_array_size(transports))
        return transports[transport];

    return NULL;
}

enum sol_bt_transport
sol_bt_transport_from_str(const char *str)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("all", SOL_BT_TRANSPORT_ALL),
        SOL_STR_TABLE_ITEM("le", SOL_BT_TRANSPORT_LE),
        SOL_STR_TABLE_ITEM("bredr", SOL_BT_TRANSPORT_BREDR),
        { },
    };

    return sol_str_table_lookup_fallback(table, sol_str_slice_from_str(str),
        SOL_BT_TRANSPORT_ALL);
}
