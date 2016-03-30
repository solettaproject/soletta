/*
 * This file is part of the Soletta Project
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

#include "sol-interrupt_scheduler_riot.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-socket.h"
#include "sol-socket-impl.h"
#include "sol-vector.h"
#include "sol-util-internal.h"

#include "net/gnrc/ipv6.h"
#include "net/gnrc/udp.h"

struct sol_socket_riot {
    struct sol_socket base;

    struct {
        bool (*cb)(void *data, struct sol_socket *s);
        const void *data;
    } read, write;
    struct sol_timeout *write_timeout;
    gnrc_pktsnip_t *curr_pkt;
    gnrc_netreg_entry_t entry;
    gnrc_nettype_t type;
};

static struct sol_ptr_vector ipv6_udp_bound_sockets = SOL_PTR_VECTOR_INIT;

static ssize_t
ipv6_udp_recvmsg(struct sol_socket_riot *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *pkt = s->curr_pkt, *udp, *ipv6;
    ipv6_hdr_t *iphdr;
    udp_hdr_t *udphdr;
    size_t copysize;

    SOL_NULL_CHECK(pkt, -EAGAIN);

    /* When more types of sockets (not just datagram) are accepted,
     * remember to err if !buf && type != DGRAM */
    if (!buf)
        return pkt->size;

    LL_SEARCH_SCALAR(pkt, ipv6, type, GNRC_NETTYPE_IPV6);
    iphdr = ipv6->data;

    LL_SEARCH_SCALAR(pkt, udp, type, GNRC_NETTYPE_UDP);
    udphdr = udp->data;

    cliaddr->family = SOL_NETWORK_FAMILY_INET6;
    memcpy(cliaddr->addr.in6, iphdr->src.u8, sizeof(iphdr->src.u8));
    cliaddr->port = byteorder_ntohs(udphdr->src_port);

    copysize = pkt->size < len ? pkt->size : len;
    memcpy(buf, pkt->data, copysize);

    return copysize;
}

static inline void
riotize_port(uint16_t port, uint8_t buf[2])
{
    buf[0] = (uint8_t)port;
    buf[1] = (uint8_t)(port >> 8);
}

static gnrc_pktsnip_t *
ipv6_udp_sendmsg(struct sol_socket_riot *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *payload, *udp, *ipv6;
    ipv6_addr_t addr;
    uint8_t dstport[2], srcport[2];

    riotize_port(cliaddr->port, dstport);
    riotize_port((uint16_t)s->entry.demux_ctx, srcport);

    memcpy(addr.u8, cliaddr->addr.in6, sizeof(addr.u8));

    payload = gnrc_pktbuf_add(NULL, (void *)buf, len, GNRC_NETTYPE_UNDEF);
    SOL_NULL_CHECK(payload, NULL);

    udp = gnrc_udp_hdr_build(payload, srcport, sizeof(srcport), dstport, sizeof(dstport));
    SOL_NULL_CHECK_GOTO(udp, udp_error);

    ipv6 = gnrc_ipv6_hdr_build(udp, NULL, 0, (uint8_t *)&addr, sizeof(addr));
    SOL_NULL_CHECK_GOTO(ipv6, ipv6_error);

    return ipv6;
ipv6_error:
    gnrc_pktbuf_release(udp);
    return NULL;
udp_error:
    gnrc_pktbuf_release(payload);
    return NULL;
}

#define IPV6_MULTICAST_PREFIX_LEN 16

static int
ipv6_udp_join_group(struct sol_socket_riot *s, kernel_pid_t iface, const struct sol_network_link_addr *group)
{
    ipv6_addr_t *addr;

    addr = gnrc_ipv6_netif_add_addr(iface, (ipv6_addr_t *)group->addr.in6,
        IPV6_MULTICAST_PREFIX_LEN, 0);
    SOL_NULL_CHECK(addr, -ENOMEM);

    return 0;
}

static int
udp_bind_cmp_cb(const void *a, const void *b)
{
    const struct sol_socket_riot *s1 = a, *s2 = b;

    return s1->entry.demux_ctx < s2->entry.demux_ctx ? -1 : 1;
}

static int
ipv6_udp_bind(struct sol_socket_riot *s, const struct sol_network_link_addr *addr)
{
    struct sol_socket_riot *socket;
    uint32_t first_unused = 1025;
    int ret;
    uint16_t i, port = addr->port;

    SOL_PTR_VECTOR_FOREACH_IDX (&ipv6_udp_bound_sockets, socket, i) {
        if (port && (socket->entry.demux_ctx == (uint32_t)port))
            return -EADDRINUSE;
        if (socket->entry.demux_ctx == first_unused)
            first_unused++;
    }

    s->entry.demux_ctx = port ? port : first_unused;
    ret = sol_ptr_vector_insert_sorted(&ipv6_udp_bound_sockets, s, udp_bind_cmp_cb);
    SOL_INT_CHECK_GOTO(ret, < 0, append_error);

    return 0;
append_error:
    s->entry.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    return -ENOMEM;
}

static void
ipv6_udp_delete(struct sol_socket_riot *s)
{
    if (s->entry.demux_ctx != GNRC_NETREG_DEMUX_CTX_ALL)
        sol_ptr_vector_remove(&ipv6_udp_bound_sockets, s);
}

static void
socket_udp_recv(struct sol_socket_riot *s, gnrc_pktsnip_t *pkt)
{
    s->curr_pkt = pkt;
    if (s->read.cb)
        s->read.cb((void *)s->read.data, &s->base);
    s->curr_pkt = NULL;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket_riot *s = data;

    if (s->write.cb((void *)s->write.data, &s->base))
        return true;

    s->write_timeout = NULL;
    return false;
}

static void
udp_dispatch(gnrc_pktsnip_t *udp, gnrc_pktsnip_t *pkt)
{
    struct sol_socket_riot *s;
    udp_hdr_t *udphdr;
    uint32_t port;
    uint16_t i;

    udphdr = udp->data;
    port = (uint32_t)byteorder_ntohs(udphdr->dst_port);

    SOL_PTR_VECTOR_FOREACH_IDX (&ipv6_udp_bound_sockets, s, i) {
        if (s->entry.demux_ctx == port) {
            socket_udp_recv(s, pkt);
            break;
        }
    }
}

void
sol_network_msg_dispatch(msg_t *msg)
{
    gnrc_pktsnip_t *pkt = (gnrc_pktsnip_t *)msg->content.ptr;
    gnrc_pktsnip_t *udp;

    if (msg->type != GNRC_NETAPI_MSG_TYPE_RCV)
        goto the_end;

    LL_SEARCH_SCALAR(pkt, udp, type, GNRC_NETTYPE_UDP);

    if (udp)
        udp_dispatch(udp, pkt);

the_end:
    gnrc_pktbuf_release(pkt);
}

struct sol_socket *
sol_socket_riot_new(int domain, enum sol_socket_type type, int protocol)
{
    struct sol_socket_riot *socket;

    SOL_INT_CHECK_GOTO(domain, != SOL_NETWORK_FAMILY_INET6, unsupported_family);

    socket = calloc(1, sizeof(*socket));
    SOL_NULL_CHECK_GOTO(socket, socket_error);

    switch (type) {
    case SOL_SOCKET_UDP:
        socket->type = GNRC_NETTYPE_UDP;
        break;
    default:
        SOL_WRN("Unsupported socket type: %d", type);
        goto unsupported_type;
    }

    socket->entry.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    socket->entry.pid = KERNEL_PID_UNDEF;
    socket->type = GNRC_NETTYPE_UDP;

    return &socket->base;

socket_error:
    errno = ENOMEM;
    return NULL;

unsupported_family:
    errno = EAFNOSUPPORT;
    return NULL;

unsupported_type:
    errno = EPROTOTYPE;
    free(socket);
    return NULL;
}

static void
sol_socket_riot_del(struct sol_socket *s)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    if (socket->entry.pid != KERNEL_PID_UNDEF)
        gnrc_netreg_unregister(socket->type, &socket->entry);
    ipv6_udp_delete(socket);
    free(s);
}

static int
sol_socket_riot_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(s, -EINVAL);

    socket->read.cb = cb;
    socket->read.data = data;

    return 0;
}

static int
sol_socket_riot_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(socket, -EINVAL);

    if (cb && !socket->write_timeout) {
        socket->write_timeout = sol_timeout_add(0, write_timeout_cb, socket);
        SOL_NULL_CHECK(socket->write_timeout, -ENOMEM);
    } else if (!cb && socket->write_timeout) {
        sol_timeout_del(socket->write_timeout);
        socket->write_timeout = NULL;
    }

    socket->write.cb = cb;
    socket->write.data = data;

    return 0;
}

static ssize_t
sol_socket_riot_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(socket, -EINVAL);

    return ipv6_udp_recvmsg(socket, buf, len, cliaddr);
}

static int
sol_socket_riot_sendmsg(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;
    gnrc_netreg_entry_t *sendto;
    gnrc_pktsnip_t *pkt;

    SOL_NULL_CHECK(socket, -EINVAL);

    pkt = ipv6_udp_sendmsg(socket, buf, len, cliaddr);
    SOL_NULL_CHECK(pkt, -ENOMEM);

    sendto = gnrc_netreg_lookup(socket->type, GNRC_NETREG_DEMUX_CTX_ALL);
    gnrc_pktbuf_hold(pkt, gnrc_netreg_num(socket->type, GNRC_NETREG_DEMUX_CTX_ALL) - 1);
    while (sendto != NULL) {
        gnrc_netapi_send(sendto->pid, pkt);
        sendto = gnrc_netreg_getnext(sendto);
    }

    return len;
}

static int
sol_socket_riot_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(socket, -EINVAL);

    return ipv6_udp_join_group(socket, (kernel_pid_t)ifindex, group);
}

static int
sol_socket_riot_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;
    int ret;

    SOL_NULL_CHECK(socket, -EINVAL);

    ret = ipv6_udp_bind(socket, addr);
    SOL_INT_CHECK(ret, < 0, ret);

    socket->entry.pid = sol_interrupt_scheduler_get_pid();
    gnrc_netreg_register(socket->type, &socket->entry);

    return 0;
}

static int
sol_socket_riot_setsockopt(struct sol_socket *socket, enum sol_socket_level level,
    enum sol_socket_option optname, const void *optval, size_t optlen)
{
    SOL_DBG("Not implemented");
    return 0;
}

static int
sol_socket_riot_getsockopt(struct sol_socket *socket, enum sol_socket_level level,
    enum sol_socket_option optname, void *optval, size_t *optlen)
{
    SOL_DBG("Not implemented");
    return 0;
}

const struct sol_socket_impl *
sol_socket_get_impl(void)
{
    static struct sol_socket_impl impl = {
        .bind = sol_socket_riot_bind,
        .join_group = sol_socket_riot_join_group,
        .sendmsg = sol_socket_riot_sendmsg,
        .recvmsg = sol_socket_riot_recvmsg,
        .set_on_write = sol_socket_riot_set_on_write,
        .set_on_read = sol_socket_riot_set_on_read,
        .del = sol_socket_riot_del,
        .new = sol_socket_riot_new,
        .setsockopt = sol_socket_riot_setsockopt,
        .getsockopt = sol_socket_riot_getsockopt
    };

    return &impl;
}
