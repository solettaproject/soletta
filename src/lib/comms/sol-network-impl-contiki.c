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

#include <errno.h>

#include <contiki-net.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-network.h"
#include "sol-network-util.h"
#include "sol-util.h"
#include "sol-vector.h"

static struct sol_vector links = SOL_VECTOR_INIT(struct sol_network_link);

SOL_API const char *
sol_network_link_addr_to_str(const struct sol_network_link_addr *addr,
    struct sol_buffer *buf)
{
    char *p;
    int i;
    bool sep = false, skipping = false, treated_zeroes = false;

    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (sol_bluetooth_is_family(addr->family))
        return sol_bluetooth_addr_to_str(addr, buf);

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return NULL;

    if (buf->capacity - buf->used < SOL_NETWORK_INET_ADDR_STR_LEN)
        return NULL;

    p = sol_buffer_at_end(buf);

    for (i = 0; i < 16; i += 2) {
        uint16_t part;

        part = ((uint16_t)addr->addr.in6[i] << 8) | addr->addr.in6[i + 1];
        if (part && skipping) {
            skipping = false;
            treated_zeroes = true;
            sep = true;
        }
        if (!part && !treated_zeroes) {
            if (!skipping) {
                skipping = true;
                sep = true;
            }
        }
        if (sep) {
            sol_buffer_append_char(buf, ':');
            if (skipping)
                sep = false;
        }
        if (skipping)
            continue;
        sol_buffer_append_printf(buf, "%" PRIx16, part);
        sep = true;
    }
    if (skipping)
        sol_buffer_append_char(buf, ':');

    return p;
}

SOL_API const struct sol_network_link_addr *
sol_network_link_addr_from_str(struct sol_network_link_addr *addr, const char *buf)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (sol_bluetooth_is_addr_str(buf))
        return sol_bluetooth_addr_from_str(addr, buf);

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return NULL;

    uiplib_ip6addrconv(buf, (uip_ip6addr_t *)&addr->addr.in6);

    return addr;
}

static bool
get_local_address(struct sol_network_link_addr *addr)
{
    uip_ds6_addr_t *dsaddr;

    dsaddr = uip_ds6_get_global(-1);
    if (!dsaddr)
        dsaddr = uip_ds6_get_link_local(-1);
    SOL_NULL_CHECK(dsaddr, false);

    addr->family = SOL_NETWORK_FAMILY_INET6;
    addr->port = 0;
    memcpy(&addr->addr.in6, &dsaddr->ipaddr, sizeof(addr->addr.in6));

    return true;
}

SOL_API int
sol_network_init(void)
{
    struct sol_network_link *iface;
    struct sol_network_link_addr *addr;

    iface = sol_vector_append(&links);
    SOL_NULL_CHECK(iface, -ENOMEM);

    sol_vector_init(&iface->addrs, sizeof(struct sol_network_link_addr));

    addr = sol_vector_append(&iface->addrs);
    SOL_NULL_CHECK_GOTO(addr, addr_append_error);

    if (!get_local_address(addr))
        goto get_address_error;

    SOL_SET_API_VERSION(iface->api_version = SOL_NETWORK_LINK_API_VERSION; )
    iface->index = 0;
    iface->flags = SOL_NETWORK_LINK_UP | SOL_NETWORK_LINK_RUNNING;

    return 0;

get_address_error:
    sol_vector_del(&iface->addrs, 0);
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

SOL_API int
sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    return -ENOSYS;
}

SOL_API int
sol_network_unsubscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    return -ENOSYS;
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
