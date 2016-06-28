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

#if MODULE_GNRC_IPV6_NETIF
#include "net/ipv6/addr.h"
#include "net/gnrc/ipv6/netif.h"
#endif
#include "net/gnrc/netif.h"

#include "sol-log.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "sol-network.h"
#include "sol-network-util.h"

static struct sol_vector links = SOL_VECTOR_INIT(struct sol_network_link);

SOL_API const char *
sol_network_link_addr_to_str(const struct sol_network_link_addr *addr,
    struct sol_buffer *buf)
{
#if MODULE_GNRC_IPV6_NETIF
    const char *r;

    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (sol_bluetooth_is_family(addr->family))
        return sol_bluetooth_addr_to_str(addr, buf);

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return NULL;

    if (buf->capacity - buf->used < IPV6_ADDR_MAX_STR_LEN) {
        int err;

        err = sol_buffer_expand(buf, IPV6_ADDR_MAX_STR_LEN);
        SOL_INT_CHECK(err, < 0, NULL);
    }

    r = ipv6_addr_to_str(sol_buffer_at_end(buf), (ipv6_addr_t *)&addr->addr,
        IPV6_ADDR_MAX_STR_LEN);

    if (r)
        buf->used += strlen(r);

    return r;

#else
    return NULL;
#endif
}

SOL_API const struct sol_network_link_addr *
sol_network_link_addr_from_str(struct sol_network_link_addr *addr, const char *buf)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (sol_bluetooth_is_addr_str(buf))
        return sol_bluetooth_addr_from_str(addr, buf);

#if MODULE_GNRC_IPV6_NETIF
    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return NULL;

    if (!ipv6_addr_from_str((ipv6_addr_t *)&addr->addr, buf))
        return NULL;
    return addr;
#else
    return NULL;
#endif
}

static int
add_ip6_link(uint16_t idx, gnrc_ipv6_netif_t *if_ip6)
{
#if MODULE_GNRC_IPV6_NETIF
    int i;
    struct sol_network_link *link;

    link = sol_vector_append(&links);
    SOL_NULL_CHECK(link, -ENOMEM);

    sol_vector_init(&link->addrs, sizeof(struct sol_network_link_addr));

    SOL_SET_API_VERSION(link->api_version = SOL_NETWORK_LINK_API_VERSION; )
    link->index = idx;
    link->flags = 0;

    for (i = 0; i < GNRC_IPV6_NETIF_ADDR_NUMOF; i++) {
        struct sol_network_link_addr *addr;
        gnrc_ipv6_netif_addr_t *netif_addr = &if_ip6->addrs[i];

        if (ipv6_addr_is_unspecified(&netif_addr->addr))
            continue;

        addr = sol_vector_append(&link->addrs);
        SOL_NULL_CHECK_GOTO(addr, addr_error);

        addr->family = SOL_NETWORK_FAMILY_INET6;
        memcpy(addr->addr.in6, netif_addr->addr.u8, sizeof(addr->addr.in6));

        link->flags |= SOL_NETWORK_LINK_UP;
        if (ipv6_addr_is_multicast(&netif_addr->addr))
            link->flags |= SOL_NETWORK_LINK_MULTICAST;
    }

    return 0;
addr_error:
    sol_vector_clear(&link->addrs);
    sol_vector_del_last(&links);
    return -ENOMEM;
#else
    return -ENOSYS;
#endif
}

int sol_network_init(void);
void sol_network_shutdown(void);

int
sol_network_init(void)
{
    size_t i, if_count;
    kernel_pid_t ifs[GNRC_NETIF_NUMOF];

    if_count = gnrc_netif_get(ifs);

    for (i = 0; i < if_count; i++) {
        gnrc_ipv6_netif_t *ip6 = gnrc_ipv6_netif_get(ifs[i]);
        if (ip6) {
            if (add_ip6_link(ifs[i], ip6)) {
                sol_vector_clear(&links);
                return -1;
            }
        }
    }

    return 0;
}

void
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

SOL_API int
sol_network_hostname_pending_cancel(
    struct sol_network_hostname_pending *handle)
{
    return -ENOTSUP;
}

SOL_API int
sol_network_link_up(uint16_t link_index)
{
    SOL_WRN("Not implemented");
    return -ENOSYS;
}

SOL_API int
sol_network_link_down(uint16_t link_index)
{
    SOL_WRN("Not implemented");
    return -ENOSYS;
}

SOL_API struct sol_network_hostname_pending *
sol_network_get_hostname_address_info(const struct sol_str_slice hostname,
    enum sol_network_family family, void (*host_info_cb)(void *data,
    const struct sol_str_slice host, const struct sol_vector *addrs_list),
    const void *data)
{
    return NULL;
}
