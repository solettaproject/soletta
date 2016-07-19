/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <contiki-net.h>

#include "sol-event-handler-contiki.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-socket-impl.h"
#include "sol-network-util.h"
#include "sol-vector.h"

struct sol_socket_contiki {
    struct sol_socket base;

    bool (*on_can_read)(void *data, struct sol_socket *s);
    bool (*on_can_write)(void *data, struct sol_socket *s);
    const void *data;

    struct simple_udp_connection udpconn;

    struct sol_ptr_vector pending_buffers;
    struct sol_timeout *write_timeout;

    bool read_monitor;
    bool write_monitor;
};

struct pending_buffer {
    struct sol_network_link_addr addr;
    uint16_t datalen;
    uint8_t data[];
};

extern struct process soletta_app_process;
process_event_t sol_udp_socket_event;
static bool registered_process = false;

#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - offsetof(type, member))

static void
receive_cb(struct simple_udp_connection *conn, const uip_ipaddr_t *srcaddr,
    uint16_t srcport, const uip_ipaddr_t *dstaddr, uint16_t dstport,
    const uint8_t *data, uint16_t datalen)
{
    struct sol_socket_contiki *socket;
    struct pending_buffer *buf;
    int ret;

    socket = container_of(conn, struct sol_socket_contiki, udpconn);

    buf = malloc(sizeof(*buf) + datalen);
    SOL_NULL_CHECK(buf);

    buf->addr.family = SOL_NETWORK_FAMILY_INET6;
    memcpy(&buf->addr.addr.in6, srcaddr, sizeof(buf->addr.addr.in6));
    buf->addr.port = srcport;

    buf->datalen = datalen;
    memcpy(buf->data, data, datalen);

    ret = sol_ptr_vector_append(&socket->pending_buffers, buf);
    SOL_INT_CHECK_GOTO(ret, < 0, append_error);

    if (sol_ptr_vector_get_len(&socket->pending_buffers) > 1)
        return;

    ret = process_post(&soletta_app_process, sol_udp_socket_event, socket);
    SOL_INT_CHECK_GOTO(ret, != PROCESS_ERR_OK, post_error);

    return;

post_error:
    sol_ptr_vector_del_last(&socket->pending_buffers);
append_error:
    free(buf);
}

static void
receive_process_cb(void *data, process_event_t ev, process_data_t ev_data)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)ev_data;
    uint16_t count;

    count = sol_ptr_vector_get_len(&socket->pending_buffers);

    if (socket->on_can_read && socket->read_monitor)
        while (count--)
            socket->on_can_read((void *)socket->data, &socket->base);
}

static void
sol_socket_contiki_del(struct sol_socket *s)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)s;
    struct pending_buffer *buf;
    uint16_t idx;

    if (socket->udpconn.udp_conn)
        uip_udp_remove(socket->udpconn.udp_conn);

    sol_mainloop_contiki_event_handler_del(&sol_udp_socket_event, socket,
        receive_process_cb, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&socket->pending_buffers, buf, idx)
        free(buf);
    sol_ptr_vector_clear(&socket->pending_buffers);

    free(socket);
}

static int
sol_socket_contiki_set_read_monitor(struct sol_socket *s, bool on)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)s;

    SOL_NULL_CHECK(socket->on_can_read, -EINVAL);

    socket->read_monitor = on;

    return 0;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket_contiki *socket = data;

    if (socket->on_can_write((void *)socket->data, &socket->base))
        return true;

    socket->write_timeout = NULL;
    socket->write_monitor = false;
    return false;
}

static int
sol_socket_contiki_set_write_monitor(struct sol_socket *s, bool on)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)s;

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
sol_socket_contiki_recvmsg(struct sol_socket *s, struct sol_buffer *buf, struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)s;
    struct pending_buffer *pending_buf;
    size_t buflen = -EAGAIN;
    int r = 0;

    pending_buf = sol_ptr_vector_get(&socket->pending_buffers, 0);
    if (pending_buf) {
        buflen = pending_buf->datalen;
        if (buf)
            sol_ptr_vector_del(&socket->pending_buffers, 0);
    }

    if (SOL_BUFFER_CAN_RESIZE(buf)) {
        r = sol_buffer_ensure(buf, buflen);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    if (buflen > buf->capacity)
        buflen = buf->capacity;

    if (cliaddr)
        *cliaddr = pending_buf->addr;

    memcpy(buf->data, pending_buf->data, buflen);
    buf->used = buflen;

    if (SOL_BUFFER_NEEDS_NUL_BYTE(buf)) {
        r = sol_buffer_ensure_nul_byte(buf);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    free(pending_buf);

    return buflen;

err:
    free(pending_buf);
    return r;
}

static ssize_t
sol_socket_contiki_sendmsg(struct sol_socket *s, const struct sol_buffer *buf, const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)s;

    simple_udp_sendto_port(&socket->udpconn, buf->data, buf->used,
        (uip_ipaddr_t *)&cliaddr->addr.in6, cliaddr->port);

    return buf->used;
}

static int
sol_socket_contiki_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    if (group->family != SOL_NETWORK_FAMILY_INET6)
        return -EAFNOSUPPORT;

    uip_ds6_maddr_add((uip_ipaddr_t *)&group->addr.in6);

    return 0;
}

static int
sol_socket_contiki_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    struct sol_socket_contiki *socket = (struct sol_socket_contiki *)s;

    if (addr->family != SOL_NETWORK_FAMILY_INET6)
        return -EAFNOSUPPORT;

    if (socket->udpconn.udp_conn)
        return -EALREADY;

    if (!simple_udp_register(&socket->udpconn, addr->port, NULL, 0, receive_cb))
        return -EINVAL;

    return 0;
}

struct sol_socket *
sol_socket_ip_default_new(const struct sol_socket_options *options)
{
    struct sol_socket_contiki *socket;
    struct sol_socket_ip_options *opts = (struct sol_socket_ip_options *)options;
    static struct sol_socket_type type = {
        SOL_SET_API_VERSION(.api_version = SOL_SOCKET_TYPE_API_VERSION, )
        .bind = sol_socket_contiki_bind,
        .join_group = sol_socket_contiki_join_group,
        .sendmsg = sol_socket_contiki_sendmsg,
        .recvmsg = sol_socket_contiki_recvmsg,
        .set_write_monitor = sol_socket_contiki_set_write_monitor,
        .set_read_monitor = sol_socket_contiki_set_read_monitor,
        .del = sol_socket_contiki_del
    };
    int r = 0;

    SOL_INT_CHECK_ERRNO(opts->family, != SOL_NETWORK_FAMILY_INET6,
        EAFNOSUPPORT, NULL);

    socket = calloc(1, sizeof(*socket));
    if (!socket) {
        r = -ENOMEM;
        goto socket_error;
    }

    socket->base.type = &type;
    socket->on_can_read = options->on_can_read;
    socket->on_can_write = options->on_can_write;
    socket->data = options->data;

    sol_ptr_vector_init(&socket->pending_buffers);

    if (!registered_process) {
        sol_udp_socket_event = process_alloc_event();
    }

    r = sol_mainloop_contiki_event_handler_add(&sol_udp_socket_event, socket,
        receive_process_cb, NULL);
    SOL_EXP_CHECK_GOTO(r, handler_add_error);

    return &socket->base;

handler_add_error:
    free(socket);
socket_error:
    errno = -r;
    return NULL;
}
