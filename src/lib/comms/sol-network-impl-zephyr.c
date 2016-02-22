/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <bluetooth/bluetooth.h>
#include <stdlib.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-network.h"

static struct sol_vector links = SOL_VECTOR_INIT(struct sol_network_link);

static const char *
ble_addr_to_str(const struct sol_network_link_addr *addr, char *str,
size_t len)
{
    char type;
    int n;

    switch (addr->addr.in_ble[6]) {
        case BT_ADDR_LE_RANDOM:
            type = 'R';
            break;
        default:
            type = 'P';
            break;
    }

    n = snprintf(str, len, "%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X#%c",
                     addr->addr.in_ble[5], addr->addr.in_ble[4],
                     addr->addr.in_ble[3], addr->addr.in_ble[2],
                     addr->addr.in_ble[1], addr->addr.in_ble[0], type);

    if (n < 0 || n > len) {
        SOL_WRN("Failed to parse Bluetooth address to string");
        return NULL;
    }

    return str;
}

static int
ble_addr_check(const char *str)
{
    size_t len = strlen(str);

    if (len != 17 && len != 19) {
        return -EINVAL;
    }

    for (int i = 0; i < len; str++, i++) {
        if (isxdigit(*str)) {
            continue;
        } else if (*str == 0 || *str == '#') {
            break;
        } else if (*str != ':') {
            return -EINVAL;
        }
    }

    return len;
}

static int
add_bt_le_addr(uint8_t type, uint8_t *addr)
{
    struct sol_network_link *link;
    struct sol_network_link_addr *btaddr;

    link = sol_vector_append(&links);
    SOL_NULL_CHECK(link, -ENOMEM);

    sol_vector_init(&link->addrs, sizeof(struct sol_network_link_addr));

    SOL_SET_API_VERSION(link->api_version = SOL_NETWORK_LINK_API_VERSION;)

    link->index = 0;
    link->flags = SOL_NETWORK_LINK_UP;

    btaddr = sol_vector_append(&link->addrs);
    SOL_NULL_CHECK_GOTO(btaddr, addr_error);

    memcpy(btaddr->addr.in_ble, addr, sizeof(btaddr->addr.in_ble));
    btaddr->addr.in_ble[6] = type;

    btaddr->family = AF_BT_IOTIVITY;

    return 0;

addr_error:
    sol_vector_clear(&link->addrs);
    sol_vector_del_last(&links);
    return -ENOMEM;
}

SOL_API const char *
sol_network_addr_to_str(const struct sol_network_link_addr *addr,
    char *buf, uint32_t len)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (addr->family == AF_BT_IOTIVITY) {
        return ble_addr_to_str(addr, buf, BT_ADDR_LE_STR_LEN);
    }

    return NULL;
}

SOL_API const struct sol_network_link_addr *
sol_network_addr_from_str(struct sol_network_link_addr *addr, const char *buf)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (addr->family == AF_BT_IOTIVITY) {
        int len = ble_addr_check(buf);
        int errno;

        if (len < 0) {
            SOL_WRN("%s is not a valid address", buf);
            return NULL;
        }

        for (int i = 5; i >= 0; i--, buf += 3) {
            addr->addr.in_ble[i] = sol_util_strtol(buf, NULL, sizeof(buf), 16);
            if (errno)
                return -errno;
        }

        if (len == 19) {
            if (*buf == 'R') {
                addr->addr.in_ble[6] = BT_ADDR_LE_RANDOM;
            } else {
                addr->addr.in_ble[6] = BT_ADDR_LE_PUBLIC;
            }
        } else {
            addr->addr.in_ble[6] = BT_ADDR_LE_PUBLIC;
        }

        return addr;
    }

    return NULL;
}

SOL_API int
sol_network_init(void)
{
    if (add_bt_le_addr(BT_ADDR_LE_ANY->type, BT_ADDR_LE_ANY->val) < 0) {
        goto addr_append_error;
    }

    return 0;

addr_append_error:
    sol_vector_del(&links, 0);
    return -ENOMEM;
}

SOL_API void
sol_network_shutdown(void)
{
    struct sol_network_link *link;
    uint16_t i;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&links, link, i) {
        sol_vector_clear(&link->addrs);
    }

    sol_vector_clear(&links);

    return;
}

SOL_API bool
sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    return false;
}

SOL_API bool
sol_network_unsubscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    return false;
}

SOL_API const struct sol_vector *
sol_network_get_available_links(void)
{
    return &links;
}

SOL_API char *
sol_network_link_get_name(const struct sol_network_link *link)
{
    return NULL;
}
