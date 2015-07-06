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

#include <netinet/in.h>

#include "net_if.h"

#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-network.h"

static struct sol_vector links = SOL_VECTOR_INIT(struct sol_network_link);

SOL_API const char *
sol_network_addr_to_str(const struct sol_network_link_addr *addr,
    char *buf, socklen_t len)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    return inet_ntop(addr->family, &addr->addr, buf, len);
}

SOL_API bool
sol_network_init(void)
{
    int net_if = -1;

    do {
        net_if_addr_t *if_addr = NULL;
        struct sol_network_link *link;

        net_if = net_if_iter_interfaces(net_if);
        if (net_if < 0)
            return true;

        link = sol_vector_append(&links);
        SOL_NULL_CHECK(link, false);

        sol_vector_init(&link->addrs, sizeof(struct sol_network_link_addr));

        /* FIXME: set the flags */

        link->api_version = SOL_NETWORK_LINK_API_VERSION;
        link->index = net_if;

        do {
            struct sol_network_link_addr *addr;

            if_addr = net_if_iter_addresses(net_if, &if_addr);
            if (!if_addr)
                break;

            /* FIXME: what to do with other addresses? */
            if (if_addr->addr_protocol & NET_IF_L3P_IPV6_ADDR) {
                addr = sol_vector_append(&link->addrs);
                /* returning an incomplete list */
                SOL_NULL_CHECK(addr, true);

                link->flags |= SOL_NETWORK_LINK_UP;
                addr->family = AF_INET6;
                memcpy(&addr->addr.in6, if_addr->addr_data, if_addr->addr_len);
            }

        } while (if_addr);

    } while (net_if > 0);

    return true;
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
