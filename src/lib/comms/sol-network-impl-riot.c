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

static struct sol_vector links = SOL_VECTOR_INIT(struct sol_network_link);

SOL_API const char *
sol_network_addr_to_str(const struct sol_network_link_addr *addr,
    char *buf, uint32_t len)
{
#if MODULE_GNRC_IPV6_NETIF
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return NULL;

    return ipv6_addr_to_str(buf, (ipv6_addr_t *)&addr->addr, len);
#else
    return NULL;
#endif
}

SOL_API const struct sol_network_link_addr *
sol_network_addr_from_str(struct sol_network_link_addr *addr, const char *buf)
{
#if MODULE_GNRC_IPV6_NETIF
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return NULL;

    if (!ipv6_addr_from_str((ipv6_addr_t *)&addr->addr, buf))
        return NULL;
    return addr;
#else
    return NULL;
#endif
}

SOL_API bool
sol_network_link_addr_eq(const struct sol_network_link_addr *a, const struct sol_network_link_addr *b)
{
    const uint8_t *addr_a, *addr_b;
    size_t bytes;

    if (a->family != b->family)
        return false;

    if (a->family == SOL_NETWORK_FAMILY_INET) {
        addr_a = a->addr.in;
        addr_b = b->addr.in;
        bytes = sizeof(a->addr.in);
    } else if (a->family == SOL_NETWORK_FAMILY_INET6) {
        addr_a = a->addr.in6;
        addr_b = b->addr.in6;
        bytes = sizeof(a->addr.in6);
    } else {
        SOL_WRN("Unknown family type: %d", a->family);
        return false;
    }

    return !memcmp(addr_a, addr_b, bytes);
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

SOL_API int
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

SOL_API int
sol_network_cancel_get_hostname_address_info(
    struct sol_network_hostname_handle *handle)
{
    return -ENOTSUP;
}

SOL_API struct sol_network_hostname_handle *
sol_network_get_hostname_address_info(const struct sol_str_slice hostname,
    enum sol_network_family family, void (*host_info_cb)(void *data,
    const struct sol_str_slice host, const struct sol_vector *addrs_list),
    const void *data)
{
    return NULL;
}
