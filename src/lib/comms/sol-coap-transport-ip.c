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
#include <stdbool.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-coap-transport.h"
#include "sol-coap-transport-ip.h"
#include "sol-network.h"
#include "sol-socket.h"
#include "sol-util-internal.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "coap-transport-ip");

#define IPV4_ALL_COAP_NODES_GROUP "224.0.1.187"

#define IPV6_ALL_COAP_NODES_SCOPE_LOCAL "ff02::fd"
#define IPV6_ALL_COAP_NODES_SCOPE_SITE "ff05::fd"

struct sol_coap_transport_ip {
    struct sol_coap_transport base;
    struct sol_socket *sock;
    struct {
        bool (*cb)(void *data, struct sol_coap_transport *transport);
        const void *cb_data;
    } on_can_read;

    struct {
        bool (*cb)(void *data, struct sol_coap_transport *transport);
        const void *cb_data;
    } on_can_write;
};

static bool
transport_ip_on_can_read(void *data, struct sol_socket *s)
{
    struct sol_coap_transport_ip *tip = data;

    if (tip->on_can_read.cb)
        return tip->on_can_read.cb((void *)tip->on_can_read.cb_data, data);

    return true;
}

static bool
transport_ip_on_can_write(void *data, struct sol_socket *s)
{
    struct sol_coap_transport_ip *tip = data;

    if (tip->on_can_write.cb)
        return tip->on_can_write.cb((void *)tip->on_can_write.cb_data, data);

    return true;
}

static int
join_mcast_groups(struct sol_socket *s, const struct sol_network_link *link)
{
    struct sol_network_link_addr groupaddr = { };
    struct sol_network_link_addr *addr;
    uint16_t i;

    if (!(link->flags & SOL_NETWORK_LINK_RUNNING) && !(link->flags & SOL_NETWORK_LINK_MULTICAST))
        return 0;

    SOL_VECTOR_FOREACH_IDX (&link->addrs, addr, i) {
        groupaddr.family = addr->family;

        if (addr->family == SOL_NETWORK_FAMILY_INET) {
            sol_network_link_addr_from_str(&groupaddr, IPV4_ALL_COAP_NODES_GROUP);
            if (sol_socket_join_group(s, link->index, &groupaddr) < 0)
                return -errno;

            continue;
        }

        sol_network_link_addr_from_str(&groupaddr, IPV6_ALL_COAP_NODES_SCOPE_LOCAL);
        if (sol_socket_join_group(s, link->index, &groupaddr) < 0)
            return -errno;

        sol_network_link_addr_from_str(&groupaddr, IPV6_ALL_COAP_NODES_SCOPE_SITE);
        if (sol_socket_join_group(s, link->index, &groupaddr) < 0)
            return -errno;
    }

    return 0;
}

static void
network_event(void *data, const struct sol_network_link *link, enum sol_network_event event)
{
    struct sol_coap_transport_ip *transport = data;

    if (event != SOL_NETWORK_LINK_ADDED && event != SOL_NETWORK_LINK_CHANGED)
        return;

    if (!(link->flags & SOL_NETWORK_LINK_RUNNING) && !(link->flags & SOL_NETWORK_LINK_MULTICAST))
        return;

    join_mcast_groups(transport->sock, link);
}

static int
transport_ip_sendmsg(struct sol_coap_transport *transport, const void *buf,
    size_t len, const struct sol_network_link_addr *addr)
{
    struct sol_coap_transport_ip *tip;

    SOL_NULL_CHECK(transport, -EINVAL);

    tip = (struct sol_coap_transport_ip *)transport;

    return sol_socket_sendmsg(tip->sock, buf, len, addr);
}

static int
transport_ip_recvmsg(struct sol_coap_transport *transport, void *buf,
    size_t len, struct sol_network_link_addr *addr)
{
    int ret;
    struct sol_coap_transport_ip *tip;

    SOL_NULL_CHECK(transport, -EINVAL);

    tip = (struct sol_coap_transport_ip *)transport;
    ret = sol_socket_recvmsg(tip->sock, buf, len, addr);

    return ret;
}

static int
transport_ip_set_on_read(struct sol_coap_transport *transport,
    bool (*on_can_read)(void *data, struct sol_coap_transport *transport),
    const void *user_data)
{
    int err;
    struct sol_coap_transport_ip *tip;

    SOL_NULL_CHECK(transport, -EINVAL);

    tip = (struct sol_coap_transport_ip *)transport;
    tip->on_can_read.cb = on_can_read;
    tip->on_can_read.cb_data = user_data;

    err = sol_socket_set_on_read(tip->sock, transport_ip_on_can_read, tip);
    SOL_INT_CHECK_GOTO(err, < 0, err);

    return 0;

err:
    tip->on_can_read.cb = NULL;
    return err;
}

static int
transport_ip_set_on_write(struct sol_coap_transport *transport,
    bool (*on_can_write)(void *data, struct sol_coap_transport *transport),
    const void *user_data)
{
    int err;
    struct sol_coap_transport_ip *tip;

    SOL_NULL_CHECK(transport, -EINVAL);

    tip = (struct sol_coap_transport_ip *)transport;
    tip->on_can_write.cb = on_can_write;
    tip->on_can_write.cb_data = user_data;

    err = sol_socket_set_on_write(tip->sock, transport_ip_on_can_write, tip);
    SOL_INT_CHECK_GOTO(err, < 0, err);

    return 0;

err:
    tip->on_can_read.cb = NULL;
    return err;
}

static struct sol_coap_transport *
sol_coap_transport_ip_new_full(enum sol_socket_type type, const struct sol_network_link_addr *addr)
{
    struct sol_coap_transport_ip *transport;

    errno = EINVAL;
    SOL_NULL_CHECK(addr, NULL);

    errno = ENOMEM;
    transport = sol_util_memdup(&(struct sol_coap_transport_ip) {
        .base = {
            SOL_SET_API_VERSION(.api_version = SOL_COAP_TRANSPORT_API_VERSION, )
            .sendmsg = transport_ip_sendmsg,
            .recvmsg = transport_ip_recvmsg,
            .set_on_read = transport_ip_set_on_read,
            .set_on_write = transport_ip_set_on_write,
        }
    }, sizeof(*transport));
    SOL_NULL_CHECK(transport, NULL);

    transport->sock = sol_socket_new(addr->family, type, 0);
    SOL_NULL_CHECK_GOTO(transport->sock, err);

    if (sol_socket_bind(transport->sock, addr) < 0) {
        SOL_WRN("Could not bind socket (%d): %s", errno, sol_util_strerrora(errno));
        goto bind_err;
    }

    if (type == SOL_SOCKET_UDP && addr->port) {
        const struct sol_vector *links;
        struct sol_network_link *link;
        uint16_t i;

        /* From man 7 ip:
         *
         *   imr_address is the address of the local interface with which the
         *   system should join the  multicast  group;  if  it  is  equal  to
         *   INADDR_ANY,  an  appropriate  interface is chosen by the system.
         *
         * We can't join a multicast group on every interface. In the future
         * we may want to add a default multicast route to the system and use
         * that interface.
         */
        links = sol_network_get_available_links();

        if (links) {
            SOL_VECTOR_FOREACH_IDX (links, link, i) {
                /* Not considering an error,
                 * because direct packets will work still.
                 */
                if (join_mcast_groups(transport->sock, link) < 0) {
                    char *name = sol_network_link_get_name(link);
                    SOL_WRN("Could not join multicast group, iface %s (%d): %s",
                        name, errno, sol_util_strerrora(errno));
                    free(name);
                }
            }
        }
    }
    sol_network_subscribe_events(network_event, transport);

    SOL_DBG("New coap transport %p on port %d%s", transport, addr->port,
        type == SOL_SOCKET_UDP ? "" : " (secure)");

    errno = 0;
    return &transport->base;

bind_err:
    sol_socket_del(transport->sock);
err:
    free(transport);
    return NULL;
}

SOL_API struct sol_coap_transport *
sol_coap_transport_ip_secure_new(const struct sol_network_link_addr *addr)
{
#ifdef DTLS
    return sol_coap_transport_ip_new_full(SOL_SOCKET_DTLS, addr);
#else
    errno = ENOSYS;
    return NULL;
#endif
}

SOL_API struct sol_coap_transport *
sol_coap_transport_ip_new(const struct sol_network_link_addr *addr)
{
    return sol_coap_transport_ip_new_full(SOL_SOCKET_UDP, addr);
}

SOL_API void
sol_coap_transport_ip_del(struct sol_coap_transport *transport)
{
    struct sol_coap_transport_ip *t = (struct sol_coap_transport_ip *)transport;

    SOL_NULL_CHECK(t);

    sol_socket_del(t->sock);
    free(t);
}
