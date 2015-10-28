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
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-util.h"

#include "sol-socket.h"

struct sol_socket {
    int fd;
    struct {
        bool (*cb)(void *data, struct sol_socket *s);
        const void *data;
        struct sol_fd *watch;
    } read, write;
};

static bool
on_socket_read(void *data, int fd, unsigned int flags)
{
    struct sol_socket *s = data;
    bool ret;

    ret = s->read.cb((void *)s->read.data, s);
    if (!ret) {
        s->read.cb = NULL;
        s->read.data = NULL;
        s->read.watch = NULL;
    }
    return ret;
}

static bool
on_socket_write(void *data, int fd, unsigned int flags)
{
    struct sol_socket *s = data;
    bool ret;

    ret = s->write.cb((void *)s->write.data, s);
    if (!ret) {
        s->write.cb = NULL;
        s->write.data = NULL;
        s->write.watch = NULL;
    }
    return ret;
}

static int
from_sockaddr(const struct sockaddr *sockaddr, socklen_t socklen,
    struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(sockaddr, -EINVAL);
    SOL_NULL_CHECK(addr, -EINVAL);

    if (sockaddr->sa_family != AF_INET && sockaddr->sa_family != AF_INET6)
        return -EINVAL;

    addr->family = sockaddr->sa_family;

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

    if (addr->family != AF_INET && addr->family != AF_INET6)
        return -EINVAL;

    if (addr->family == AF_INET ) {
        struct sockaddr_in *sock4 = (struct sockaddr_in *)sockaddr;
        if (*socklen < sizeof(struct sockaddr_in))
            return -EINVAL;

        memcpy(&sock4->sin_addr, addr->addr.in, sizeof(addr->addr.in));
        sock4->sin_port = htons(addr->port);
        sock4->sin_family = AF_INET;
        *socklen = sizeof(*sock4);
    } else {
        struct sockaddr_in6 *sock6 = (struct sockaddr_in6 *)sockaddr;
        if (*socklen < sizeof(struct sockaddr_in6))
            return -EINVAL;

        memcpy(&sock6->sin6_addr, addr->addr.in6, sizeof(addr->addr.in6));
        sock6->sin6_port = htons(addr->port);
        sock6->sin6_family = AF_INET6;
        *socklen = sizeof(*sock6);
    }

    return *socklen;
}

SOL_API struct sol_socket *
sol_socket_new(int domain, enum sol_socket_type type, int protocol)
{
    struct sol_socket *s;
    int fd, socktype = SOCK_CLOEXEC | SOCK_NONBLOCK;

    switch (type) {
    case SOL_SOCKET_UDP:
        socktype |= SOCK_DGRAM;
        break;
    default:
        SOL_WRN("Unsupported socket type: %d", type);
        errno = EPROTOTYPE;
        return NULL;
    }

    fd = socket(domain, socktype, protocol);
    SOL_INT_CHECK(fd, < 0, NULL);

    s = calloc(1, sizeof(*s));
    SOL_NULL_CHECK_GOTO(s, calloc_error);

    s->fd = fd;

    return s;
calloc_error:
    close(fd);
    return NULL;
}

SOL_API void
sol_socket_del(struct sol_socket *s)
{
    if (s->read.watch)
        sol_fd_del(s->read.watch);
    if (s->write.watch)
        sol_fd_del(s->write.watch);
    close(s->fd);
    free(s);
}

SOL_API int
sol_socket_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);

    if (cb && !s->read.watch) {
        s->read.watch = sol_fd_add(s->fd, SOL_FD_FLAGS_IN, on_socket_read, s);
        SOL_NULL_CHECK(s->read.watch, -ENOMEM);
    } else if (!cb && s->read.watch) {
        sol_fd_del(s->read.watch);
        s->read.watch = NULL;
    }

    s->read.cb = cb;
    s->read.data = data;

    return 0;
}

SOL_API int
sol_socket_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);

    if (cb && !s->write.watch) {
        s->write.watch = sol_fd_add(s->fd, SOL_FD_FLAGS_OUT, on_socket_write, s);
        SOL_NULL_CHECK(s->write.watch, -ENOMEM);
    } else if (!cb && s->write.watch) {
        sol_fd_del(s->write.watch);
        s->write.watch = NULL;
    }

    s->write.cb = cb;
    s->write.data = data;

    return 0;
}

SOL_API int
sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    uint8_t sockaddr[sizeof(struct sockaddr_in6)] = { };
    struct iovec iov = { .iov_base = buf,
                         .iov_len = len };
    struct msghdr msg = {
        .msg_name = &sockaddr,
        .msg_namelen = sizeof(sockaddr),
        .msg_iov = &iov,
        .msg_iovlen = 1
    };
    ssize_t r;

    SOL_NULL_CHECK(s, -EINVAL);

    r = recvmsg(s->fd, &msg, 0);

    if (r < 0)
        return -errno;

    if (from_sockaddr((struct sockaddr *)sockaddr, sizeof(sockaddr), cliaddr) < 0)
        return -EINVAL;

    if ((ssize_t)(int)r != r)
        return -EOVERFLOW;

    return (int)r;
}

static bool
sendmsg_multicast_addrs(int fd, struct sol_network_link *net_link,
    struct msghdr *msg)
{
    struct ip_mreqn ip4_mreq = { .imr_ifindex = net_link->index };
    struct ipv6_mreq ip6_mreq = { .ipv6mr_interface = net_link->index };
    struct ip_mreqn orig_ip4_mreq;
    struct ipv6_mreq orig_ip6_mreq;
    struct sol_network_link_addr *addr;
    uint16_t idx;
    bool success = false;

    SOL_VECTOR_FOREACH_IDX (&net_link->addrs, addr, idx) {
        void *p_orig, *p_new;
        int level, option;
        socklen_t l, l_orig;

        if (addr->family == AF_INET) {
            level = IPPROTO_IP;
            option = IP_MULTICAST_IF;
            p_orig = &orig_ip4_mreq;
            p_new = &ip4_mreq;
            l = sizeof(orig_ip4_mreq);
        } else if (addr->family == AF_INET6) {
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

        if (sendmsg(fd, msg, 0) < 0) {
            SOL_DBG("Error while sending multicast message: %s",
                sol_util_strerrora(errno));
            continue;
        }

        if (setsockopt(fd, level, option, p_orig, l_orig) < 0) {
            SOL_DBG("Error while restoring socket interface: %s",
                sol_util_strerrora(errno));
            continue;
        }

        success = true;
    }

    return success;
}

static int
sendmsg_multicast(int fd, struct msghdr *msg)
{
    const unsigned int running_multicast = SOL_NETWORK_LINK_RUNNING | SOL_NETWORK_LINK_MULTICAST;
    const struct sol_vector *net_links = sol_network_get_available_links();
    struct sol_network_link *net_link;
    uint16_t idx;
    bool had_success = false;

    if (!net_links || !net_links->len)
        return -ENOTCONN;

    SOL_VECTOR_FOREACH_IDX (net_links, net_link, idx) {
        if ((net_link->flags & running_multicast) == running_multicast) {
            if (sendmsg_multicast_addrs(fd, net_link, msg))
                had_success = true;
        }
    }

    return had_success ? 0 : -EIO;
}

static bool
is_multicast(int family, const struct sockaddr *sockaddr)
{
    if (family == AF_INET6) {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)sockaddr;

        return IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr);
    }

    if (family == AF_INET) {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)sockaddr;

        return IN_MULTICAST(htonl(addr4->sin_addr.s_addr));
    }

    SOL_WRN("Unknown address family (%d)", family);
    return false;
}

SOL_API int
sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr)
{
    uint8_t sockaddr[sizeof(struct sockaddr_in6)] = { };
    struct iovec iov = { .iov_base = (void *)buf,
                         .iov_len = len };
    struct msghdr msg = { .msg_iov = &iov,
                          .msg_iovlen = 1 };
    socklen_t l;

    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(cliaddr, -EINVAL);

    l = sizeof(struct sockaddr_in6);
    if (to_sockaddr(cliaddr, (struct sockaddr *)sockaddr, &l) < 0)
        return -EINVAL;

    msg.msg_name = &sockaddr;
    msg.msg_namelen = l;

    if (is_multicast(cliaddr->family, (struct sockaddr *)sockaddr))
        return sendmsg_multicast(s->fd, &msg);

    if (sendmsg(s->fd, &msg, 0) < 0)
        return -errno;

    return 0;
}

SOL_API int
sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    struct ip_mreqn ip_join = { };
    struct ipv6_mreq ip6_join = { };
    const void *p;
    int level, option;
    socklen_t l;

    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(group, -EINVAL);

    if (group->family != AF_INET && group->family != AF_INET6)
        return -EINVAL;

    if (group->family == AF_INET) {
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
        option = IPV6_ADD_MEMBERSHIP;
    }

    if (setsockopt(s->fd, level, option, p, l) < 0)
        return -errno;

    return 0;
}

SOL_API int
sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{

    uint8_t sockaddr[sizeof(struct sockaddr_in6)] = { };
    socklen_t l;

    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(addr, -EINVAL);

    l = sizeof(struct sockaddr_in6);
    if (to_sockaddr(addr, (void *)sockaddr, &l) < 0)
        return -EINVAL;

    if (bind(s->fd, (struct sockaddr *)sockaddr, l) < 0) {
        return -errno;
    }

    return 0;
}
