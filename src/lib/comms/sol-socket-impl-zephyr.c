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

#include <errno.h>

#include <net/ip_buf.h>
#include <sections.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-mainloop-zephyr.h"
#include "sol-socket-impl.h"
#include "sol-vector.h"
#include "sol-network-util.h"

struct sol_socket_net_context;

struct sol_socket_zephyr {
    struct sol_socket base;

    struct {
        bool (*cb)(void *data, struct sol_socket *s);
        const void *data;
    } read, write;

    struct sol_timeout *write_timeout;

    struct sol_socket_net_context *unicast_context;
    struct sol_ptr_vector mcast_contexts;
    struct sol_ptr_vector bufs;

    struct nano_sem lock;
    bool read_available;
};

#define RECV_STACKSIZE 256

struct sol_socket_net_context {
    struct sol_socket_zephyr *socket;
    struct net_context *context;
    nano_thread_id_t fiber;
    uint8_t stack[RECV_STACKSIZE] __stack;
};

static void
socket_read_available(void *data)
{
    struct sol_socket_zephyr *s = data;
    uint16_t count;

    nano_task_sem_take(&s->lock, TICKS_UNLIMITED);
    count = sol_ptr_vector_get_len(&s->bufs);
    s->read_available = false;
    nano_task_sem_give(&s->lock);

    if (s->read.cb)
        while (count--)
            s->read.cb((void *)s->read.data, &s->base);
}

static void
socket_signal_mainloop(struct sol_socket_zephyr *s, struct net_buf *buf)
{
    struct mainloop_event me = {
        .cb = socket_read_available,
        .data = s
    };
    int ret;

    nano_fiber_sem_take(&s->lock, TICKS_UNLIMITED);

    ret = sol_ptr_vector_append(&s->bufs, buf);
    SOL_INT_CHECK_GOTO(ret, < 0, err);

    if (!s->read_available) {
        sol_mainloop_event_post(&me);
        s->read_available = true;
    }

err:
    nano_fiber_sem_give(&s->lock);
}

static void
socket_recv_fiber(int arg1, int arg2)
{
    struct sol_socket_net_context *ctx = (struct sol_socket_net_context *)arg1;

    while (1) {
        struct net_buf *buf;

        buf = net_receive(ctx->context, TICKS_UNLIMITED);
        if (buf)
            socket_signal_mainloop(ctx->socket, buf);
    }
}

static struct sol_socket *
sol_socket_zephyr_new(int domain, enum sol_socket_type type, int protocol)
{
    struct sol_socket_zephyr *socket;

    SOL_INT_CHECK_GOTO(domain, != SOL_NETWORK_FAMILY_INET6,
        unsupported_family);

    socket = calloc(1, sizeof(*socket));
    SOL_NULL_CHECK_GOTO(socket, socket_error);

    sol_ptr_vector_init(&socket->mcast_contexts);
    sol_ptr_vector_init(&socket->bufs);
    nano_sem_init(&socket->lock);
    nano_sem_give(&socket->lock);

    return &socket->base;

socket_error:
    errno = ENOMEM;
    return NULL;

unsupported_family:
    errno = EAFNOSUPPORT;
    return NULL;
}

static void
socket_net_context_del(struct sol_socket_net_context *ctx)
{
    if (ctx->fiber)
        fiber_fiber_delayed_start_cancel(ctx->fiber);

    net_context_put(ctx->context);

    free(ctx);
}

static void
sol_socket_zephyr_del(struct sol_socket *s)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;
    struct sol_socket_net_context *ctx;
    struct net_buf *buf;
    uint16_t idx;

    if (socket->unicast_context) {
        int key;

        key = irq_lock();

        socket_net_context_del(socket->unicast_context);

        SOL_PTR_VECTOR_FOREACH_IDX (&socket->mcast_contexts, ctx, idx)
            socket_net_context_del(ctx);
        sol_ptr_vector_clear(&socket->mcast_contexts);

        irq_unlock(key);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&socket->bufs, buf, idx)
        ip_buf_unref(buf);
    sol_ptr_vector_clear(&socket->bufs);

    free(socket);
}

static int
sol_socket_zephyr_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;

    socket->read.cb = cb;
    socket->read.data = data;

    return 0;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket_zephyr *socket = data;

    if (socket->write.cb((void *)socket->write.data, &socket->base))
        return true;

    socket->write_timeout = NULL;
    return false;
}

static int
sol_socket_zephyr_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;

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
sol_socket_zephyr_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;
    struct net_buf *netbuf;
    size_t buflen = -EAGAIN;

    nano_task_sem_take(&socket->lock, TICKS_UNLIMITED);

    netbuf = sol_ptr_vector_get(&socket->bufs, 0);
    if (netbuf) {
        buflen = ip_buf_appdatalen(netbuf);
        if (buf)
            sol_ptr_vector_del(&socket->bufs, 0);
    }
    nano_task_sem_give(&socket->lock);

    if (!buf)
        return buflen;

    if (buflen > len)
        buflen = len;

    if (cliaddr) {
        cliaddr->family = SOL_NETWORK_FAMILY_INET6;
        cliaddr->port = uip_ntohs(NET_BUF_UDP(netbuf)->srcport);
        memcpy(cliaddr->addr.in6, &NET_BUF_IP(netbuf)->srcipaddr, sizeof(cliaddr->addr.in6));
    }

    memcpy(buf, ip_buf_appdata(netbuf), buflen);

    ip_buf_unref(netbuf);

    return buflen;
}

/* HACK HACK HACK HACK
 * We should not need to include this header anywhere around here, but in order
 * to make net_reply() work for our needs, the buf we pass to it must contain
 * a valid udp_conn set, and to get that one, we need to peek into the
 * declaration of simple_udp_connection.
 */
#include <ip/simple-udp.h>

static int
sol_socket_zephyr_sendmsg(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;
    struct net_context *ctx;
    struct net_tuple *tuple;
    struct net_buf *netbuf;
    uint8_t *ptr;

    SOL_NULL_CHECK(socket->unicast_context, -ENOTCONN);

    ctx = socket->unicast_context->context;

    tuple = net_context_get_tuple(ctx);

    netbuf = ip_buf_get_tx(ctx);
    SOL_NULL_CHECK(netbuf, -ENOMEM);

    /* We set address and port on the packet as if we had received this from
     * the client we want to send to because net_reply() will revert them
     * before sending */
    memcpy(&NET_BUF_IP(netbuf)->srcipaddr, cliaddr->addr.in6, sizeof(cliaddr->addr.in6));
    memcpy(&NET_BUF_IP(netbuf)->destipaddr, &tuple->local_addr->in6_addr, sizeof(tuple->local_addr->in6_addr));
    NET_BUF_UDP(netbuf)->srcport = uip_htons(cliaddr->port);
    NET_BUF_UDP(netbuf)->destport = uip_htons(tuple->local_port);
    uip_set_udp_conn(netbuf) = net_context_get_udp_connection(ctx)->udp_conn;

    ptr = net_buf_add(netbuf, len);
    memcpy(ptr, buf, len);
    ip_buf_appdatalen(netbuf) = len;

    if (net_reply(ctx, netbuf) < 0) {
        ip_buf_unref(netbuf);
        return -EIO;
    }

    return len;
}

static struct sol_socket_net_context *
socket_net_context_new(struct sol_socket_zephyr *s, const struct sol_network_link_addr *addr)
{
    struct sol_socket_net_context *ctx;
    struct net_addr bindaddr;

    ctx = calloc(1, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, NULL);

    bindaddr.family = sol_network_sol_to_af(addr->family);
    memcpy(&bindaddr.in6_addr, addr->addr.in6, sizeof(bindaddr.in6_addr));

    ctx->context = net_context_get(IPPROTO_UDP, NULL, 0, &bindaddr, addr->port);
    SOL_NULL_CHECK_GOTO(ctx->context, err);
    ctx->socket = s;

    return ctx;

err:
    free(ctx);
    return NULL;
}

static void
socket_fiber_launch(struct sol_socket_net_context *ctx)
{
    ctx->fiber = fiber_start(ctx->stack, RECV_STACKSIZE, socket_recv_fiber,
        (intptr_t)ctx, 0, 7, 0);
}

static int
sol_socket_zephyr_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;
    struct sol_socket_net_context *ctx;
    struct net_tuple *tuple;
    struct sol_network_link_addr addr;
    int ret;

    if (group->family != SOL_NETWORK_FAMILY_INET6)
        return -EAFNOSUPPORT;

    SOL_NULL_CHECK(socket->unicast_context, -EINVAL);

    tuple = net_context_get_tuple(socket->unicast_context->context);
    SOL_NULL_CHECK(tuple, -EINVAL);

    addr = *group;
    addr.port = tuple->local_port;

    ctx = socket_net_context_new(socket, &addr);
    SOL_NULL_CHECK(ctx, -ENOBUFS);

    ret = sol_ptr_vector_append(&socket->mcast_contexts, ctx);
    SOL_INT_CHECK_GOTO(ret, < 0, err);

    socket_fiber_launch(ctx);

    return 0;

err:
    socket_net_context_del(ctx);
    return ret;
}

static int
sol_socket_zephyr_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return -EAFNOSUPPORT;

    if (socket->unicast_context)
        return -EALREADY;

    socket->unicast_context = socket_net_context_new(socket, addr);
    SOL_NULL_CHECK(socket->unicast_context, -ENOBUFS);

    socket_fiber_launch(socket->unicast_context);

    return 0;
}

static int
sol_socket_zephyr_setsockopt(struct sol_socket *socket, enum sol_socket_level level,
    enum sol_socket_option optname, const void *optval, size_t optlen)
{
    SOL_DBG("Not implemented");
    return 0;
}

static int
sol_socket_zephyr_getsockopt(struct sol_socket *socket, enum sol_socket_level level,
    enum sol_socket_option optname, void *optval, size_t *optlen)
{
    SOL_DBG("Not implemented");
    return 0;
}

const struct sol_socket_impl *
sol_socket_zephyr_get_impl(void)
{
    static struct sol_socket_impl impl = {
        .bind = sol_socket_zephyr_bind,
        .join_group = sol_socket_zephyr_join_group,
        .sendmsg = sol_socket_zephyr_sendmsg,
        .recvmsg = sol_socket_zephyr_recvmsg,
        .set_on_write = sol_socket_zephyr_set_on_write,
        .set_on_read = sol_socket_zephyr_set_on_read,
        .del = sol_socket_zephyr_del,
        .new = sol_socket_zephyr_new,
        .setsockopt = sol_socket_zephyr_setsockopt,
        .getsockopt = sol_socket_zephyr_getsockopt
    };

    return &impl;
}
