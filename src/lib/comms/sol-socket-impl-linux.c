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

#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-network-util.h"
#include "sol-util.h"
#include "sol-util-internal.h"

#include "sol-socket.h"
#include "sol-socket-impl.h"

struct sol_socket_linux {
    struct sol_socket base;

    bool (*on_can_read)(void *data, struct sol_socket *s);
    bool (*on_can_write)(void *data, struct sol_socket *s);
    struct sol_fd *watch;
    const void *data;
    int fd;
};

static bool
on_socket_event(void *data, int fd, unsigned int flags)
{
    struct sol_socket_linux *s = data;
    uint32_t socket_flags = 0;
    bool ret;

    if (flags & SOL_FD_FLAGS_IN) {
        ret = s->on_can_read((void *)s->data, &s->base);
        if (!ret) {
            socket_flags = SOL_FD_FLAGS_IN;
        }
    }

    if (flags & SOL_FD_FLAGS_OUT) {
        ret = s->on_can_write((void *)s->data, &s->base);
        if (!ret) {
            socket_flags |= SOL_FD_FLAGS_OUT;
        }
    }

    /*
     * It's not possible to get the flags at the beginning and apply a ~ mask
     * because the flags can change when the user callback is called.
     */
    if (socket_flags) {
        if (socket_flags == (SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT)) {
            s->watch = NULL;
            return false;
        } else {
            sol_fd_remove_flags(s->watch, socket_flags);
        }
    }

    return true;
}

static int
from_sockaddr(const struct sockaddr *sockaddr, socklen_t socklen,
    struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(sockaddr, -EINVAL);
    SOL_NULL_CHECK(addr, -EINVAL);

    if (sockaddr->sa_family != AF_INET && sockaddr->sa_family != AF_INET6)
        return -EINVAL;

    addr->family = sol_network_af_to_sol(sockaddr->sa_family);

    if (sockaddr->sa_family == AF_INET) {
        struct sockaddr_in *sock4 = (struct sockaddr_in *)sockaddr;
        if (socklen < sizeof(struct sockaddr_in))
            return -EINVAL;

        addr->port = ntohs(sock4->sin_port);
        memcpy(&addr->addr.in, &sock4->sin_addr, sizeof(sock4->sin_addr));
    } else {
        struct sockaddr_in6 *sock6 = (struct sockaddr_in6 *)sockaddr;
        if (socklen < sizeof(struct sockaddr_in6))
            return -EINVAL;

        addr->port = ntohs(sock6->sin6_port);
        memcpy(&addr->addr.in6, &sock6->sin6_addr, sizeof(sock6->sin6_addr));
    }

    return 0;
}

static int
to_sockaddr(const struct sol_network_link_addr *addr, struct sockaddr *sockaddr, socklen_t *socklen)
{
    SOL_NULL_CHECK(addr, -EINVAL);
    SOL_NULL_CHECK(sockaddr, -EINVAL);
    SOL_NULL_CHECK(socklen, -EINVAL);

    if (addr->family == SOL_NETWORK_FAMILY_INET) {
        struct sockaddr_in *sock4 = (struct sockaddr_in *)sockaddr;
        if (*socklen < sizeof(struct sockaddr_in))
            return -EINVAL;

        memcpy(&sock4->sin_addr, addr->addr.in, sizeof(addr->addr.in));
        sock4->sin_port = htons(addr->port);
        sock4->sin_family = AF_INET;
        *socklen = sizeof(*sock4);
    } else if (addr->family == SOL_NETWORK_FAMILY_INET6) {
        struct sockaddr_in6 *sock6 = (struct sockaddr_in6 *)sockaddr;
        if (*socklen < sizeof(struct sockaddr_in6))
            return -EINVAL;

        memcpy(&sock6->sin6_addr, addr->addr.in6, sizeof(addr->addr.in6));
        sock6->sin6_port = htons(addr->port);
        sock6->sin6_family = AF_INET6;
        *socklen = sizeof(*sock6);
    } else {
        return -EINVAL;
    }

    return *socklen;
}

static void
sol_socket_linux_del(struct sol_socket *socket)
{
    struct sol_socket_linux *s = (struct sol_socket_linux *)socket;

    if (s->watch)
        sol_fd_del(s->watch);
    close(s->fd);
    free(s);
}

static ssize_t
sol_socket_linux_recvmsg(struct sol_socket *socket, struct sol_buffer *buf,
    struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_linux *s = (struct sol_socket_linux *)socket;
    uint8_t sockaddr[sizeof(struct sockaddr_in6)] = { };
    struct iovec iov = { };
    struct msghdr msg = {
        .msg_name = &sockaddr,
        .msg_namelen = sizeof(sockaddr),
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };
    ssize_t r;

    if (SOL_BUFFER_CAN_RESIZE(buf)) {
        msg.msg_iov->iov_len = 0;
        r = recvmsg(s->fd, &msg, MSG_TRUNC | MSG_PEEK);

        r = sol_buffer_ensure(buf, r);
        SOL_INT_CHECK(r, < 0, r);
    }

    iov.iov_len = buf->capacity;
    iov.iov_base = buf->data;

    r = recvmsg(s->fd, &msg, 0);
    SOL_INT_CHECK(r, < 0, r);

    buf->used = sol_util_min(r, (ssize_t)buf->capacity);
    if (SOL_BUFFER_NEEDS_NUL_BYTE(buf)) {
        int ret;

        ret = sol_buffer_ensure_nul_byte(buf);
        SOL_INT_CHECK(ret, < 0, ret);
    }

    if (from_sockaddr((struct sockaddr *)sockaddr, sizeof(sockaddr), cliaddr) < 0)
        return -EINVAL;

    return r;
}

static ssize_t
sendmsg_multicast_addrs(int fd, struct sol_network_link *net_link,
    struct msghdr *msg)
{
    struct ip_mreqn ip4_mreq = { .imr_ifindex = net_link->index };
    struct ipv6_mreq ip6_mreq = { .ipv6mr_interface = net_link->index };
    struct ip_mreqn orig_ip4_mreq;
    struct ipv6_mreq orig_ip6_mreq;
    struct sol_network_link_addr *addr;
    uint16_t idx;
    ssize_t ret = 0;

    SOL_VECTOR_FOREACH_IDX (&net_link->addrs, addr, idx) {
        void *p_orig, *p_new;
        int level, option;
        socklen_t l, l_orig;

        if (addr->family == SOL_NETWORK_FAMILY_INET) {
            level = IPPROTO_IP;
            option = IP_MULTICAST_IF;
            p_orig = &orig_ip4_mreq;
            p_new = &ip4_mreq;
            l = sizeof(orig_ip4_mreq);
        } else if (addr->family == SOL_NETWORK_FAMILY_INET6) {
            level = IPPROTO_IPV6;
            option = IPV6_MULTICAST_IF;
            p_orig = &orig_ip6_mreq;
            p_new = &ip6_mreq;
            l = sizeof(orig_ip6_mreq);
        } else {
            SOL_WRN("Unknown address family: %d", addr->family);
            continue;
        }

        l_orig = l;
        if (getsockopt(fd, level, option, p_orig, &l_orig) < 0) {
            SOL_DBG("Error while getting socket interface: %s",
                sol_util_strerrora(errno));
            continue;
        }

        if (setsockopt(fd, level, option, p_new, l) < 0) {
            SOL_DBG("Error while setting socket interface: %s",
                sol_util_strerrora(errno));
            continue;
        }

        ret = sendmsg(fd, msg, 0);
        if (ret < 0) {
            SOL_DBG("Error while sending multicast message: %s",
                sol_util_strerrora(errno));
            continue;
        }

        if (setsockopt(fd, level, option, p_orig, l_orig) < 0) {
            SOL_DBG("Error while restoring socket interface: %s",
                sol_util_strerrora(errno));
            continue;
        }
    }

    return ret >= 0 ? ret : -errno;
}

static int
sendmsg_multicast(int fd, struct msghdr *msg)
{
    const unsigned int running_multicast = SOL_NETWORK_LINK_RUNNING | SOL_NETWORK_LINK_MULTICAST;
    const struct sol_vector *net_links = sol_network_get_available_links();
    struct sol_network_link *net_link;
    uint16_t idx;
    ssize_t ret = 0;

    if (!net_links || !net_links->len)
        return -ENOTCONN;

    SOL_VECTOR_FOREACH_IDX (net_links, net_link, idx) {
        if ((net_link->flags & running_multicast) == running_multicast) {
            ret = sendmsg_multicast_addrs(fd, net_link, msg);
            SOL_INT_CHECK(ret, < 0, ret);
        }
    }

    return ret;
}

static bool
is_multicast(enum sol_network_family family, const struct sockaddr *sockaddr)
{
    if (family == SOL_NETWORK_FAMILY_INET6) {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)sockaddr;

        return IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr);
    }

    if (family == SOL_NETWORK_FAMILY_INET) {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)sockaddr;

        return IN_MULTICAST(htonl(addr4->sin_addr.s_addr));
    }

    SOL_WRN("Unknown address family (%d)", family);
    return false;
}

static ssize_t
sol_socket_linux_sendmsg(struct sol_socket *socket, const struct sol_buffer *buf,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_linux *s = (struct sol_socket_linux *)socket;
    uint8_t sockaddr[sizeof(struct sockaddr_in6)] = { };
    struct iovec iov = { .iov_base = (void *)buf->data,
                         .iov_len = buf->used };
    struct msghdr msg = { .msg_iov = &iov,
                          .msg_iovlen = 1 };
    socklen_t l;

    l = sizeof(struct sockaddr_in6);
    if (to_sockaddr(cliaddr, (struct sockaddr *)sockaddr, &l) < 0)
        return -EINVAL;

    msg.msg_name = &sockaddr;
    msg.msg_namelen = l;

    if (is_multicast(cliaddr->family, (struct sockaddr *)sockaddr))
        return sendmsg_multicast(s->fd, &msg);

    return sendmsg(s->fd, &msg, 0);
}

static int
sol_socket_linux_join_group(struct sol_socket *socket, int ifindex, const struct sol_network_link_addr *group)
{
    struct sol_socket_linux *s = (struct sol_socket_linux *)socket;
    struct ip_mreqn ip_join = { };
    struct ipv6_mreq ip6_join = { };
    const void *p;
    int level, option;
    socklen_t l;

    if (group->family != SOL_NETWORK_FAMILY_INET && group->family != SOL_NETWORK_FAMILY_INET6)
        return -EINVAL;

    if (group->family == SOL_NETWORK_FAMILY_INET) {
        memcpy(&ip_join.imr_multiaddr, group->addr.in, sizeof(group->addr.in));
        ip_join.imr_ifindex = ifindex;

        p = &ip_join;
        l = sizeof(ip_join);
        level = IPPROTO_IP;
        option = IP_ADD_MEMBERSHIP;
    } else {
        memcpy(&ip6_join.ipv6mr_multiaddr, group->addr.in6, sizeof(group->addr.in6));
        ip6_join.ipv6mr_interface = ifindex;

        p = &ip6_join;
        l = sizeof(ip6_join);
        level = IPPROTO_IPV6;
        option = IPV6_JOIN_GROUP;
    }

    if (setsockopt(s->fd, level, option, p, l) < 0)
        return -errno;

    return 0;
}

static int
sol_socket_linux_bind(struct sol_socket *socket, const struct sol_network_link_addr *addr)
{
    struct sol_socket_linux *s = (struct sol_socket_linux *)socket;
    uint8_t sockaddr[sizeof(struct sockaddr_in6)] = { };
    socklen_t l;

    l = sizeof(struct sockaddr_in6);
    if (to_sockaddr(addr, (void *)sockaddr, &l) < 0)
        return -EINVAL;

    if (bind(s->fd, (struct sockaddr *)sockaddr, l) < 0) {
        return -errno;
    }

    return 0;
}

static int
sol_socket_linux_set_read_monitor(struct sol_socket *s, bool on)
{
    bool ret;
    struct sol_socket_linux *sock = (struct sol_socket_linux *)s;

    SOL_NULL_CHECK(sock->on_can_read, -EINVAL);

    if (!sock->watch) {
        sock->watch = sol_fd_add(sock->fd, SOL_FD_FLAGS_IN, on_socket_event, sock);
        SOL_NULL_CHECK(sock->watch, -EBADF);
        return 0;
    }

    if (on)
        ret = sol_fd_add_flags(sock->watch, SOL_FD_FLAGS_IN);
    else
        ret = sol_fd_remove_flags(sock->watch, SOL_FD_FLAGS_IN);

    return ret ? 0 : -EBADF;
}

static int
sol_socket_linux_set_write_monitor(struct sol_socket *s, bool on)
{
    bool ret;
    struct sol_socket_linux *sock = (struct sol_socket_linux *)s;

    SOL_NULL_CHECK(sock->on_can_write, -EINVAL);

    if (!sock->watch) {
        sock->watch = sol_fd_add(sock->fd, SOL_FD_FLAGS_OUT, on_socket_event, sock);
        SOL_NULL_CHECK(sock->watch, -EBADF);
        return 0;
    }

    if (on)
        ret = sol_fd_add_flags(sock->watch, SOL_FD_FLAGS_OUT);
    else
        ret = sol_fd_remove_flags(sock->watch, SOL_FD_FLAGS_OUT);

    return ret ? 0 : -EBADF;
}

struct sol_socket *
sol_socket_ip_default_new(const struct sol_socket_options *options)
{
    int ret = 0;
    struct sol_socket_linux *s;
    int fd, socktype = SOCK_DGRAM;
    struct sol_socket_ip_options *opts;
    static const struct sol_socket_type type = {
        SOL_SET_API_VERSION(.api_version = SOL_SOCKET_TYPE_API_VERSION, )
        .bind = sol_socket_linux_bind,
        .join_group = sol_socket_linux_join_group,
        .sendmsg = sol_socket_linux_sendmsg,
        .recvmsg = sol_socket_linux_recvmsg,
        .del = sol_socket_linux_del,
        .set_read_monitor = sol_socket_linux_set_read_monitor,
        .set_write_monitor = sol_socket_linux_set_write_monitor,
    };

    SOL_SOCKET_OPTIONS_CHECK_SUB_API_VERSION(options, SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION, NULL);

#ifdef SOCK_CLOEXEC
    socktype |= SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif

    opts = (struct sol_socket_ip_options *)options;
    fd = socket(sol_network_sol_to_af(opts->family), socktype, 0);
    SOL_INT_CHECK(fd, < 0, NULL);

#ifndef SOCK_CLOEXEC
    /* We need to set the socket to FD_CLOEXEC and non-blocking mode */
    if (!sol_fd_add_flags(fd, FD_CLOEXEC | O_NONBLOCK)) {
        SOL_WRN("Failed to set the socket to FD_CLOEXEC or O_NONBLOCK, %s",
            sol_util_strerrora(errno));
        ret = -EINVAL;
        goto calloc_error;
    }
#endif

    if (opts->reuse_port) {
        int val = 1;
        ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
        SOL_INT_CHECK_GOTO(ret, < 0, options_err);
    }

    if (opts->reuse_addr) {
        int val = 1;
        ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
        SOL_INT_CHECK_GOTO(ret, < 0, options_err);
    }

    s = calloc(1, sizeof(*s));
    if (!s) {
        ret = -ENOMEM;
        goto calloc_error;
    }

    s->base.type = &type;
    s->fd = fd;
    s->data = options->data;
    s->on_can_write = options->on_can_write;
    s->on_can_read = options->on_can_read;

    s->watch = sol_fd_add(fd, SOL_FD_FLAGS_NONE, on_socket_event, s);
    if (!s->watch) {
        ret = -errno;
        goto watch_error;
    }

    return &s->base;

watch_error:
    free(s);
options_err:
calloc_error:
    close(fd);
    errno = -ret;
    return NULL;
}
