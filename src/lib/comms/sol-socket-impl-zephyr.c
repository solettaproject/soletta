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

static const struct net_addr addr_any = { .family = AF_INET6,
                                          .in6_addr = IN6ADDR_ANY_INIT };

struct sol_socket_zephyr {
    struct sol_socket base;

    bool (*on_can_read)(void *data, struct sol_socket *s);
    bool (*on_can_write)(void *data, struct sol_socket *s);
    const void *data;

    struct sol_timeout *write_timeout;

    struct sol_socket_net_context *unicast_context;
    struct sol_ptr_vector mcast_contexts;
    struct sol_ptr_vector bufs;

    struct nano_sem lock;
    bool read_available;
    bool read_monitor;
    bool write_monitor;
};

#define RECV_STACKSIZE 256

struct sol_socket_net_context {
    struct sol_socket_zephyr *socket;
    struct net_context *context;
    struct net_addr bind_addr;
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

    if (s->read_monitor)
        while (count--)
            s->read_monitor = s->on_can_read((void *)s->data, &s->base);
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

static void
socket_net_context_del(struct sol_socket_net_context *ctx)
{
    /* As of now, there's no way to kill a fiber from outside of it.
     * But, there's a way to start a delayed fiber, that is, a fiber that
     * will not really start until after a specified time has passed, and
     * these fibers can be cancelled. It so happens that what the cancel
     * code does also terminates the thread of that fiber and we can use
     * it to kill ours. It doesn't feel right, but it works (for now, at least)
     */
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
sol_socket_zephyr_set_read_monitor(struct sol_socket *s, bool on)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;

    SOL_NULL_CHECK(socket->on_can_read, -EINVAL);

    socket->read_monitor = on;

    return 0;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket_zephyr *socket = data;

    if (socket->on_can_write((void *)socket->data, &socket->base))
        return true;

    socket->write_monitor = false;
    socket->write_timeout = NULL;
    return false;
}

static int
sol_socket_zephyr_set_write_monitor(struct sol_socket *s, bool on)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;

    SOL_NULL_CHECK(socket->on_can_write, -EINVAL);

    if (on && !socket->write_timeout) {
        socket->write_timeout = sol_timeout_add(0, write_timeout_cb, socket);
        SOL_NULL_CHECK(socket->write_timeout, -ENOMEM);
    } else if (!on && socket->write_timeout) {
        sol_timeout_del(socket->write_timeout);
        socket->write_timeout = NULL;
    }

    socket->write_monitor = on;

    return 0;
}

static ssize_t
sol_socket_zephyr_recvmsg(struct sol_socket *s, struct sol_buffer *buf, struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_zephyr *socket = (struct sol_socket_zephyr *)s;
    struct net_buf *netbuf;
    ssize_t buflen = -EAGAIN;
    int r = 0;

    nano_task_sem_take(&socket->lock, TICKS_UNLIMITED);

    netbuf = sol_ptr_vector_get(&socket->bufs, 0);
    if (netbuf) {
        buflen = ip_buf_appdatalen(netbuf);
        if (buf)
            sol_ptr_vector_del(&socket->bufs, 0);
    }
    nano_task_sem_give(&socket->lock);

    if (SOL_BUFFER_CAN_RESIZE(buf)) {
        r = sol_buffer_ensure(buf, buflen);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    if (buflen > buf->capacity)
        buflen = buf->capacity;

    if (cliaddr) {
        cliaddr->family = SOL_NETWORK_FAMILY_INET6;
        cliaddr->port = uip_ntohs(NET_BUF_UDP(netbuf)->srcport);
        memcpy(cliaddr->addr.in6, &NET_BUF_IP(netbuf)->srcipaddr, sizeof(cliaddr->addr.in6));
    }

    memcpy(buf->data, ip_buf_appdata(netbuf), buflen);
    buf->used = buflen;

    if (SOL_BUFFER_NEEDS_NUL_BYTE(buf)) {
        r = sol_buffer_ensure_nul_byte(buf);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    ip_buf_unref(netbuf);

    return buflen;

err:
    ip_buf_unref(netbuf);
    return r;
}

/* HACK HACK HACK HACK
 * We should not need to include this header anywhere around here, but in order
 * to make net_reply() work for our needs, the buf we pass to it must contain
 * a valid udp_conn set, and to get that one, we need to peek into the
 * declaration of simple_udp_connection.
 */
#include <ip/simple-udp.h>

static ssize_t
sol_socket_zephyr_sendmsg(struct sol_socket *s, const struct sol_buffer *buf,
    const struct sol_network_link_addr *cliaddr)
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

    ptr = net_buf_add(netbuf, buf->used);
    memcpy(ptr, buf->data, buf->used);
    ip_buf_appdatalen(netbuf) = buf->used;

    if (net_reply(ctx, netbuf) < 0) {
        ip_buf_unref(netbuf);
        return -EIO;
    }

    return buf->used;
}

static struct sol_socket_net_context *
socket_net_context_new(struct sol_socket_zephyr *s, const struct sol_network_link_addr *addr)
{
    struct sol_socket_net_context *ctx;

    ctx = calloc(1, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, NULL);

    ctx->bind_addr.family = sol_network_sol_to_af(addr->family);
    memcpy(&ctx->bind_addr.in6_addr, addr->addr.in6,
        sizeof(ctx->bind_addr.in6_addr));

    ctx->context = net_context_get
            (IPPROTO_UDP, &addr_any, 0, &ctx->bind_addr, addr->port);
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

struct sol_socket *
sol_socket_ip_default_new(const struct sol_socket_options *options)
{
    struct sol_socket_zephyr *socket;
    struct sol_socket_ip_options *opts = (struct sol_socket_ip_options *)options;
    static struct sol_socket_type type = {
        SOL_SET_API_VERSION(.api_version = SOL_SOCKET_TYPE_API_VERSION, )
        .bind = sol_socket_zephyr_bind,
        .join_group = sol_socket_zephyr_join_group,
        .sendmsg = sol_socket_zephyr_sendmsg,
        .recvmsg = sol_socket_zephyr_recvmsg,
        .set_write_monitor = sol_socket_zephyr_set_write_monitor,
        .set_read_monitor = sol_socket_zephyr_set_read_monitor,
        .del = sol_socket_zephyr_del,
    };

    SOL_INT_CHECK_ERRNO(opts->family, != SOL_NETWORK_FAMILY_INET6,
        EAFNOSUPPORT, NULL);

    socket = calloc(1, sizeof(*socket));
    if (!socket) {
        errno = ENOMEM;
        return NULL;;
    }

    socket->base.type = &type;
    sol_ptr_vector_init(&socket->mcast_contexts);
    sol_ptr_vector_init(&socket->bufs);
    nano_sem_init(&socket->lock);
    nano_sem_give(&socket->lock);
    socket->on_can_write = options->on_can_write;
    socket->on_can_read = options->on_can_read;
    socket->data = options->data;

    return &socket->base;
}
