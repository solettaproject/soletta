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

#include "sol-interrupt_scheduler_riot.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-socket.h"
#include "sol-socket-impl.h"
#include "sol-vector.h"
#include "sol-util.h"
#include "sol-util-internal.h"

#include "net/gnrc/ipv6.h"
#include "net/gnrc/udp.h"

struct sol_socket_riot {
    struct sol_socket base;

    bool (*on_can_read)(void *data, struct sol_socket *s);
    bool (*on_can_write)(void *data, struct sol_socket *s);
    const void *data;
    struct sol_timeout *write_timeout;
    gnrc_pktsnip_t *curr_pkt;
    gnrc_netreg_entry_t entry;
    gnrc_nettype_t type;
    bool read_monitor;
    bool write_monitor;
};

static struct sol_ptr_vector ipv6_udp_bound_sockets = SOL_PTR_VECTOR_INIT;

static ssize_t
ipv6_udp_recvmsg(struct sol_socket_riot *s, struct sol_buffer *buf, struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *pkt = s->curr_pkt, *udp, *ipv6;
    ipv6_hdr_t *iphdr;
    udp_hdr_t *udphdr;
    size_t copysize;

    SOL_NULL_CHECK(pkt, -EAGAIN);

    if (SOL_BUFFER_CAN_RESIZE(buf)) {
        int r = sol_buffer_ensure(buf, pkt->size);
        SOL_INT_CHECK(r, < 0, r);
    }

    LL_SEARCH_SCALAR(pkt, ipv6, type, GNRC_NETTYPE_IPV6);
    iphdr = ipv6->data;

    LL_SEARCH_SCALAR(pkt, udp, type, GNRC_NETTYPE_UDP);
    udphdr = udp->data;

    cliaddr->family = SOL_NETWORK_FAMILY_INET6;
    memcpy(cliaddr->addr.in6, iphdr->src.u8, sizeof(iphdr->src.u8));
    cliaddr->port = byteorder_ntohs(udphdr->src_port);

    copysize = sol_util_min(pkt->size, buf->capacity);
    memcpy(buf->data, pkt->data, copysize);
    buf->used = copysize;

    if (SOL_BUFFER_NEEDS_NUL_BYTE(buf)) {
        int r = sol_buffer_ensure_nul_byte(buf);
        SOL_INT_CHECK(r, < 0, r);
    }

    return copysize;
}

static gnrc_pktsnip_t *
ipv6_udp_sendmsg(struct sol_socket_riot *s, const struct sol_buffer *buf,
    const struct sol_network_link_addr *cliaddr)
{
    gnrc_pktsnip_t *payload, *udp, *ipv6;
    ipv6_addr_t addr;
    uint16_t srcport;

    srcport = (uint16_t)s->entry.demux_ctx;

    memcpy(addr.u8, cliaddr->addr.in6, sizeof(addr.u8));

    payload = gnrc_pktbuf_add(NULL, (void *)buf->data, buf->used, GNRC_NETTYPE_UNDEF);
    SOL_NULL_CHECK(payload, NULL);

    udp = gnrc_udp_hdr_build(payload, srcport, cliaddr->port);
    SOL_NULL_CHECK_GOTO(udp, udp_error);

    ipv6 = gnrc_ipv6_hdr_build(udp, NULL, &addr);
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
    if (s->on_can_read && s->read_monitor) {
        if (!s->on_can_read((void *)s->data, &s->base))
            s->read_monitor = false;
    }

    s->curr_pkt = NULL;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket_riot *s = data;

    if (s->on_can_write((void *)s->data, &s->base))
        return true;

    s->write_timeout = NULL;
    s->write_monitor = false;
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
sol_socket_riot_set_read_monitor(struct sol_socket *s, bool on)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(socket->on_can_read, -EINVAL);

    socket->read_monitor = on;

    return 0;
}

static int
sol_socket_riot_set_write_monitor(struct sol_socket *s, bool on)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(socket, -EINVAL);
    SOL_NULL_CHECK(socket->on_can_write, -EINVAL);

    if (on && !socket->write_timeout) {
        socket->write_timeout = sol_timeout_add(0, write_timeout_cb, socket);
        SOL_NULL_CHECK(socket->write_timeout, -ENOMEM);
    } else if (!on) {
        sol_timeout_del(socket->write_timeout);
        socket->write_timeout = NULL;
    }

    socket->write_monitor = on;

    return 0;
}

static ssize_t
sol_socket_riot_recvmsg(struct sol_socket *s, struct sol_buffer *buf,
    struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;

    SOL_NULL_CHECK(socket, -EINVAL);

    return ipv6_udp_recvmsg(socket, buf, cliaddr);
}

static ssize_t
sol_socket_riot_sendmsg(struct sol_socket *s, const struct sol_buffer *buf,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_riot *socket = (struct sol_socket_riot *)s;
    gnrc_netreg_entry_t *sendto;
    gnrc_pktsnip_t *pkt;

    SOL_NULL_CHECK(socket, -EINVAL);

    pkt = ipv6_udp_sendmsg(socket, buf, cliaddr);
    SOL_NULL_CHECK(pkt, -ENOMEM);

    sendto = gnrc_netreg_lookup(socket->type, GNRC_NETREG_DEMUX_CTX_ALL);
    gnrc_pktbuf_hold(pkt, gnrc_netreg_num(socket->type, GNRC_NETREG_DEMUX_CTX_ALL) - 1);
    while (sendto != NULL) {
        gnrc_netapi_send(sendto->pid, pkt);
        sendto = gnrc_netreg_getnext(sendto);
    }

    return buf->used;
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

struct sol_socket *
sol_socket_ip_default_new(const struct sol_socket_options *options)
{
    struct sol_socket_riot *socket;
    struct sol_socket_ip_options *opts = (struct sol_socket_ip_options *)options;
    static struct sol_socket_type type = {
        SOL_SET_API_VERSION(.api_version = SOL_SOCKET_TYPE_API_VERSION, )
        .bind = sol_socket_riot_bind,
        .join_group = sol_socket_riot_join_group,
        .sendmsg = sol_socket_riot_sendmsg,
        .recvmsg = sol_socket_riot_recvmsg,
        .set_write_monitor = sol_socket_riot_set_write_monitor,
        .set_read_monitor = sol_socket_riot_set_read_monitor,
        .del = sol_socket_riot_del
    };

    SOL_INT_CHECK_ERRNO(opts->family, != SOL_NETWORK_FAMILY_INET6,
        EAFNOSUPPORT, NULL);

    socket = calloc(1, sizeof(*socket));
    if (!socket) {
        errno = ENOMEM;
        goto socket_error;
    }

    socket->base.type = &type;
    socket->type = GNRC_NETTYPE_UDP;
    socket->entry.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    socket->entry.pid = KERNEL_PID_UNDEF;
    socket->on_can_read = options->on_can_read;
    socket->on_can_write = options->on_can_write;
    socket->data = options->data;

    return &socket->base;

socket_error:
    return NULL;
}
