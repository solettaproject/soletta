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

#pragma once

#include <ctype.h>

#include <sol-buffer.h>
#include <sol-network.h>

enum sol_network_family sol_network_af_to_sol(int af);
int sol_network_sol_to_af(enum sol_network_family snf);

static inline struct sol_network_link_addr *
sol_bluetooth_addr_from_str(struct sol_network_link_addr *addr, const char *str)
{
    int i;
    uint8_t *ba = addr->addr.bt_addr;
    const char *ptr;
    char *endptr = NULL;

    /* FIXME */
    addr->family = SOL_NETWORK_FAMILY_BLUETOOTH;
    addr->addr.bt_type = SOL_NETWORK_BT_ADDR_BASIC_RATE;

    ptr = str;

    for (i = sizeof(addr->addr.bt_addr) - 1; i >= 0; i--) {
        ba[i] = strtoul(ptr, &endptr, 16);
        ptr = endptr + 1;
        endptr = NULL;
    }

    return addr;
}

static inline bool
sol_bluetooth_is_addr_str(const char *str)
{
    const char *p = str;

    while (*str) {
        if (!isxdigit((int)*str++))
            return false;

        if (!isxdigit((int)*str++))
            return false;

        if (*str == '0')
            break;

        if (*str == '\0')
            break;

        if (*str++ != ':')
            return false;
    }

    return (str - p) == 17;
}

static inline const char *
sol_bluetooth_addr_to_str(const struct sol_network_link_addr *addr,
    struct sol_buffer *buffer)
{
    const uint8_t *ba = addr->addr.bt_addr;
    int r;

    r = sol_buffer_append_printf(buffer, "%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
        ba[5], ba[4], ba[3], ba[2], ba[1], ba[0]);
    if (r < 0)
        return NULL;

    return buffer->data;
}

static inline bool
sol_bluetooth_is_family(int family)
{
    switch (family) {
    case SOL_NETWORK_FAMILY_BLUETOOTH:
    case SOL_NETWORK_FAMILY_BLUETOOTH_RFCOMM:
    case SOL_NETWORK_FAMILY_BLUETOOTH_L2CAP:
        return true;
    }

    return false;
}
