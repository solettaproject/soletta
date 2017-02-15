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

#include "sol-network.h"

#include "test.h"

DEFINE_TEST(test_network_valid_ipv4_addresses);

static void
test_network_valid_ipv4_addresses(void)
{
    char str[] = "127.0.0.1";
    struct sol_network_link_addr addr;
    const struct sol_network_link_addr *a;

    addr.family = SOL_NETWORK_FAMILY_INET;

    a = sol_network_link_addr_from_str(&addr, str);

    ASSERT(a);
    ASSERT_INT_EQ(addr.family, SOL_NETWORK_FAMILY_INET);
}

DEFINE_TEST(test_network_invalid_ipv4_addresses);

static void
test_network_invalid_ipv4_addresses(void)
{
    char str[] = "257.320.-1.foo";
    struct sol_network_link_addr addr;
    const struct sol_network_link_addr *a;

    addr.family = SOL_NETWORK_FAMILY_INET;

    a = sol_network_link_addr_from_str(&addr, str);

    ASSERT(!a);
}

DEFINE_TEST(test_network_valid_ipv6_addresses);

static void
test_network_valid_ipv6_addresses(void)
{
    char str[] = "fe80::221:ccff:fed6:52b8";
    struct sol_network_link_addr addr;
    const struct sol_network_link_addr *a;

    addr.family = SOL_NETWORK_FAMILY_INET6;
    a = sol_network_link_addr_from_str(&addr, str);

    ASSERT(a);
    ASSERT_INT_EQ(addr.family, SOL_NETWORK_FAMILY_INET6);
}

DEFINE_TEST(test_network_invalid_ipv6_addresses);

static void
test_network_invalid_ipv6_addresses(void)
{
    const char *table[] = { ":::1", "", "test:test", NULL };
    const char **str;
    struct sol_network_link_addr addr;

    addr.family = SOL_NETWORK_FAMILY_INET6;

    for (str = table; str && *str; str++) {
        const struct sol_network_link_addr *a =
            sol_network_link_addr_from_str(&addr, *str);
        ASSERT(!a);
    }
}

DEFINE_TEST(test_network_valid_bluetooth_addresses);

static void
test_network_valid_bluetooth_addresses(void)
{
    char str[] = "11:22:33:44:55:66";
    struct sol_network_link_addr addr;
    const struct sol_network_link_addr *a;

    a = sol_network_link_addr_from_str(&addr, str);

    ASSERT(a);
    ASSERT_INT_EQ(addr.family, SOL_NETWORK_FAMILY_BLUETOOTH);
}

DEFINE_TEST(test_network_invalid_bluetooth_addresses);

static void
test_network_invalid_bluetooth_addresses(void)
{
    char str[] = "1:2:3:4:5:6";
    struct sol_network_link_addr addr = { 0 };
    const struct sol_network_link_addr *a;

    a = sol_network_link_addr_from_str(&addr, str);

    ASSERT(!a);
}

TEST_MAIN();
