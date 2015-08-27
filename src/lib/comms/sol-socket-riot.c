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
#include "sol-socket.h"
#include "sol-vector.h"
#include "sol-util.h"

#include "net/gnrc/ipv6.h"
#include "net/gnrc/udp.h"

struct sol_socket {
    gnrc_nettype_t type;
    gnrc_netreg_entry_t entry;
    bool (*read_cb)(void *data, struct sol_socket *s);
    const void *data;
    gnrc_pktsnip_t *curr_pkt;
};

static struct sol_ptr_vector sockets = SOL_PTR_VECTOR_INIT;

static int
find_port(gnrc_nettype_t type, uint16_t port)
{
    struct sol_socket *s;
    uint32_t first_unused = 1025;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&sockets, s, i) {
        if (s->type != type)
            continue;
        if (s->entry.demux_ctx == GNRC_NETREG_DEMUX_CTX_ALL)
            continue;
        if (port && (s->entry.demux_ctx == (uint32_t)port))
            return -EADDRINUSE;
        if (s->entry.demux_ctx == first_unused)
            first_unused++;
    }

    return port ? port : (int)first_unused;
}

static void
socket_udp_recv(struct sol_socket *s, gnrc_pktsnip_t *pkt)
{
    s->curr_pkt = pkt;
    s->read_cb((void *)s->data, s);
    s->curr_pkt = NULL;
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

    SOL_PTR_VECTOR_FOREACH_IDX (&sockets, s, i) {
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

SOL_API struct sol_socket *
sol_socket_new(int domain, enum sol_socket_type type, int protocol)
{
    int idx;
    struct sol_socket *socket;

    if (domain != AF_INET6) {
        errno = EAFNOSUPPORT;
        return NULL;
    }

    if (type != SOL_SOCKET_UDP) {
        errno = EPROTOTYPE;
        return NULL;
    }

    socket = calloc(1, sizeof(*socket));
    SOL_NULL_CHECK_GOTO(socket, socket_error);

    idx = sol_ptr_vector_append(&sockets, socket);
    SOL_INT_CHECK_GOTO(idx, < 0, append_error);

    socket->type = GNRC_NETTYPE_UDP;
    socket->entry.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    socket->entry.pid = KERNEL_PID_UNDEF;

    return socket;
append_error:
    free(socket);
socket_error:
    errno = ENOMEM;
    return NULL;
}

SOL_API void
sol_socket_del(struct sol_socket *s)
{
    if (s->entry.pid != KERNEL_PID_UNDEF) {
        gnrc_netreg_unregister(s->type, &s->entry);
    }
    sol_ptr_vector_remove(&sockets, s);
}

SOL_API int
sol_socket_on_read_set(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);

    s->read_cb = cb;
    s->data = data;

    return 0;
}

SOL_API int
sol_socket_on_write_set(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    while (cb(data, s));
    return 0;
}

SOL_API int
sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *pkt = s->curr_pkt, *udp, *ipv6;
    ipv6_hdr_t *iphdr;
    udp_hdr_t *udphdr;
    size_t copysize;

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

SOL_API int
sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *payload, *udp, *ipv6;
    gnrc_netreg_entry_t *sendto;
    ipv6_addr_t addr;
    uint8_t dstport[2], srcport[2];

    dstport[0] = (uint8_t)cliaddr->port;
    dstport[1] = (uint8_t)(cliaddr->port >> 8);

    srcport[0] = (uint8_t)s->entry.demux_ctx;
    srcport[1] = (uint8_t)(s->entry.demux_ctx >> 8);

    memcpy(addr.u8, cliaddr->addr.in6, sizeof(addr.u8));

    payload = gnrc_pktbuf_add(NULL, (void *)buf, len, GNRC_NETTYPE_UNDEF);
    udp = gnrc_udp_hdr_build(payload, srcport, 2, dstport, 2);
    ipv6 = gnrc_ipv6_hdr_build(udp, NULL, 0, (uint8_t *)&addr, sizeof(addr));

    sendto = gnrc_netreg_lookup(GNRC_NETTYPE_UDP, GNRC_NETREG_DEMUX_CTX_ALL);
    gnrc_pktbuf_hold(ipv6, gnrc_netreg_num(GNRC_NETTYPE_UDP, GNRC_NETREG_DEMUX_CTX_ALL) - 1);
    while (sendto != NULL) {
        gnrc_netapi_send(sendto->pid, ipv6);
        sendto = gnrc_netreg_getnext(sendto);
    }

    return len;
}

SOL_API int
sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    return -ENOSYS;
}

SOL_API int
sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    int ret;

    ret = find_port(s->type, addr->port);
    if (ret < 0)
        return ret;
    s->entry.demux_ctx = (uint32_t)ret;
    s->entry.pid = sol_interrupt_scheduler_get_pid();
    gnrc_netreg_register(s->type, &s->entry);

    return 0;
}
