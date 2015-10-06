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

#include "sol-interrupt_scheduler_riot.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-socket.h"
#include "sol-vector.h"
#include "sol-util.h"

#include "net/gnrc/ipv6.h"
#include "net/gnrc/udp.h"

struct sol_socket_class {
    gnrc_nettype_t type;
    int (*recvmsg)(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *addr);
    gnrc_pktsnip_t *(*sendmsg)(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *addr);
    int (*join_group)(struct sol_socket *s, kernel_pid_t iface, const struct sol_network_link_addr *group);
    int (*bind)(struct sol_socket *s, const struct sol_network_link_addr *addr);
    void (*delete)(struct sol_socket *s);
};

struct sol_socket {
    const struct sol_socket_class *class;
    struct {
        bool (*cb)(void *data, struct sol_socket *s);
        const void *data;
    } read, write;
    struct sol_timeout *write_timeout;
    gnrc_pktsnip_t *curr_pkt;
    gnrc_netreg_entry_t entry;
};

static struct sol_ptr_vector ipv6_udp_bound_sockets = SOL_PTR_VECTOR_INIT;

static int
ipv6_udp_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *pkt = s->curr_pkt, *udp, *ipv6;
    ipv6_hdr_t *iphdr;
    udp_hdr_t *udphdr;
    size_t copysize;

    SOL_NULL_CHECK(pkt, -EAGAIN);

    LL_SEARCH_SCALAR(pkt, ipv6, type, GNRC_NETTYPE_IPV6);
    iphdr = ipv6->data;

    LL_SEARCH_SCALAR(pkt, udp, type, GNRC_NETTYPE_UDP);
    udphdr = udp->data;

    cliaddr->family = AF_INET6;
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
ipv6_udp_sendmsg(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
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
ipv6_udp_join_group(struct sol_socket *s, kernel_pid_t iface, const struct sol_network_link_addr *group)
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
    const struct sol_socket *s1 = a, *s2 = b;

    return s1->entry.demux_ctx < s2->entry.demux_ctx ? -1 : 1;
}

static int
ipv6_udp_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    uint32_t first_unused = 1025;
    int ret;
    uint16_t i, port = addr->port;

    SOL_PTR_VECTOR_FOREACH_IDX (&ipv6_udp_bound_sockets, s, i) {
        if (port && (s->entry.demux_ctx == (uint32_t)port))
            return -EADDRINUSE;
        if (s->entry.demux_ctx == first_unused)
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
ipv6_udp_delete(struct sol_socket *s)
{
    if (s->entry.demux_ctx != GNRC_NETREG_DEMUX_CTX_ALL)
        sol_ptr_vector_remove(&ipv6_udp_bound_sockets, s);
}

static void
socket_udp_recv(struct sol_socket *s, gnrc_pktsnip_t *pkt)
{
    s->curr_pkt = pkt;
    if (s->read.cb)
        s->read.cb((void *)s->read.data, s);
    s->curr_pkt = NULL;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket *s = data;

    if (s->write.cb((void *)s->write.data, s))
        return true;

    s->write_timeout = NULL;
    return false;
}

static void
udp_dispatch(gnrc_pktsnip_t *udp, gnrc_pktsnip_t *pkt)
{
    struct sol_socket *s;
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

static const struct sol_socket_class *
ipv6_get_socket_class(enum sol_socket_type type, int protocol)
{
    static const struct sol_socket_class ipv6_udp_socket_class = {
        .type = GNRC_NETTYPE_UDP,
        .recvmsg = ipv6_udp_recvmsg,
        .sendmsg = ipv6_udp_sendmsg,
        .join_group = ipv6_udp_join_group,
        .bind = ipv6_udp_bind,
        .delete = ipv6_udp_delete,
    };

    switch (type) {
    case SOL_SOCKET_UDP:
        return &ipv6_udp_socket_class;
    default:
        errno = EPROTOTYPE;
    }

    return NULL;
}

static const struct sol_socket_class *
get_socket_class(int domain, enum sol_socket_type type, int protocol)
{
    switch (domain) {
    case AF_INET6:
        return ipv6_get_socket_class(type, protocol);
    default:
        errno = EAFNOSUPPORT;
    }

    return NULL;
}

SOL_API struct sol_socket *
sol_socket_new(int domain, enum sol_socket_type type, int protocol)
{
    struct sol_socket *socket;
    const struct sol_socket_class *class;

    class = get_socket_class(domain, type, protocol);
    SOL_NULL_CHECK(class, NULL);

    socket = calloc(1, sizeof(*socket));
    SOL_NULL_CHECK_GOTO(socket, socket_error);

    socket->class = class;
    socket->entry.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    socket->entry.pid = KERNEL_PID_UNDEF;

    return socket;
socket_error:
    errno = ENOMEM;
    return NULL;
}

SOL_API void
sol_socket_del(struct sol_socket *s)
{
    if (s->entry.pid != KERNEL_PID_UNDEF)
        gnrc_netreg_unregister(s->class->type, &s->entry);
    s->class->delete(s);
    free(s);
}

SOL_API int
sol_socket_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);

    s->read.cb = cb;
    s->read.data = data;

    return 0;
}

SOL_API int
sol_socket_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);

    if (cb && !s->write_timeout) {
        s->write_timeout = sol_timeout_add(0, write_timeout_cb, s);
        SOL_NULL_CHECK(s->write_timeout, -ENOMEM);
    } else if (!cb && s->write_timeout) {
        sol_timeout_del(s->write_timeout);
        s->write_timeout = NULL;
    }

    s->write.cb = cb;
    s->write.data = data;

    return 0;
}

SOL_API int
sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    return s->class->recvmsg(s, buf, len, cliaddr);
}

SOL_API int
sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
{
    gnrc_netreg_entry_t *sendto;
    gnrc_pktsnip_t *pkt;

    SOL_NULL_CHECK(s, -EINVAL);

    pkt = s->class->sendmsg(s, buf, len, cliaddr);
    SOL_NULL_CHECK(pkt, -ENOMEM);

    sendto = gnrc_netreg_lookup(s->class->type, GNRC_NETREG_DEMUX_CTX_ALL);
    gnrc_pktbuf_hold(pkt, gnrc_netreg_num(s->class->type, GNRC_NETREG_DEMUX_CTX_ALL) - 1);
    while (sendto != NULL) {
        gnrc_netapi_send(sendto->pid, pkt);
        sendto = gnrc_netreg_getnext(sendto);
    }

    return len;
}

SOL_API int
sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    SOL_NULL_CHECK(s, -EINVAL);

    if (!s->class->join_group)
        return -ENOSYS;
    return s->class->join_group(s, (kernel_pid_t)ifindex, group);
}

SOL_API int
sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    int ret;

    SOL_NULL_CHECK(s, -EINVAL);

    ret = s->class->bind(s, addr);
    SOL_INT_CHECK(ret, < 0, ret);

    s->entry.pid = sol_interrupt_scheduler_get_pid();
    gnrc_netreg_register(s->class->type, &s->entry);

    return 0;
}
