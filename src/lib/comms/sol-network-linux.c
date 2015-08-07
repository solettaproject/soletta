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

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-missing.h"
#include "sol-network.h"
#include "sol-util.h"
#include "sol-util-linux.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "network");

struct callback {
    void (*cb)(void *data, const struct sol_network_link *link,
        enum sol_network_event event);
    const void *data;
};

struct sol_network {
    unsigned int count;
    int nl_socket;
    struct sockaddr_nl nl_addr;
    struct sol_fd *fd;

    struct sol_vector links;
    struct sol_vector callbacks;

    int seq;
};

static struct sol_network *network = NULL;


SOL_API const char *
sol_network_addr_to_str(const struct sol_network_link_addr *addr,
    char *buf, socklen_t len)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    return inet_ntop(addr->family, &addr->addr, buf, len);
}

static struct sol_network_link *
_get_link(int index)
{
    struct sol_network_link *link = NULL;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (&network->links, link, idx)
        if (link->index == index)
            return link;

    link = sol_vector_append(&network->links);
    SOL_NULL_CHECK(link, NULL);

    link->api_version = SOL_NETWORK_LINK_API_VERSION;
    link->flags = 0;
    sol_vector_init(&link->addrs, sizeof(struct sol_network_link_addr));
    link->index = index;

    return link;
}

static void
_on_link_event(struct nlmsghdr *header)
{
    struct ifinfomsg *ifi;
    struct rtattr *rth;
    struct sol_network_link *link;
    struct callback *cb;
    enum sol_network_event event;
    unsigned int rtl, flags = 0;
    uint16_t idx;

    ifi = NLMSG_DATA(header);
    link = _get_link(ifi->ifi_index);
    SOL_NULL_CHECK(link);

    event = (header->nlmsg_type == RTM_NEWLINK) ? SOL_NETWORK_LINK_ADDED :
        SOL_NETWORK_LINK_REMOVED;

    for (rth = IFLA_RTA(ifi), rtl = IFLA_PAYLOAD(header); rtl && RTA_OK(rth, rtl);
        rth = RTA_NEXT(rth, rtl)) {
        if (rth->rta_type != IFLA_STATS)
            continue;

        if (ifi->ifi_flags & IFF_UP)
            flags |= SOL_NETWORK_LINK_UP;
        if (ifi->ifi_flags & IFF_RUNNING)
            flags |= SOL_NETWORK_LINK_RUNNING;
        if (ifi->ifi_flags & IFF_BROADCAST)
            flags |= SOL_NETWORK_LINK_BROADCAST;
        if (ifi->ifi_flags & IFF_LOOPBACK)
            flags |= SOL_NETWORK_LINK_LOOPBACK;
        if (ifi->ifi_flags & IFF_MULTICAST)
            flags |= SOL_NETWORK_LINK_MULTICAST;

        /*
         * if the link already exists (flags == 0) and the event is not DELLINK, we should
         * notify as a change event
         */
        if ((link->flags == flags) && (event == SOL_NETWORK_LINK_ADDED))
            continue;

        if ((link->flags != flags) && (event != SOL_NETWORK_LINK_REMOVED))
            event = SOL_NETWORK_LINK_CHANGED;

        link->flags = flags;
        SOL_VECTOR_FOREACH_IDX (&network->callbacks, cb, idx)
            cb->cb((void *)cb->data, link, event);
    }
}

static void
_on_addr_event(struct nlmsghdr *header)
{
    struct ifaddrmsg *ifa;
    struct rtattr *rth;
    struct sol_network_link *link;
    struct callback *cb;
    int rtl;
    uint16_t idx;

    ifa = NLMSG_DATA(header);
    link = _get_link(ifa->ifa_index);
    SOL_NULL_CHECK(link);

    for (rth = IFA_RTA(ifa), rtl = IFA_PAYLOAD(header); rtl && RTA_OK(rth, rtl);
        rth = RTA_NEXT(rth, rtl)) {
        struct sol_network_link_addr *addr = NULL, *addr_itr;

        if (rth->rta_type != IFA_LOCAL && rth->rta_type != IFA_ADDRESS)
            continue;

        SOL_VECTOR_FOREACH_IDX (&link->addrs, addr_itr, idx) {
            if (addr_itr->family == ifa->ifa_family) {
                addr = addr_itr;
                break;
            }
        }

        if (!addr) {
            addr = sol_vector_append(&link->addrs);
            SOL_NULL_CHECK(addr);

            addr->family = ifa->ifa_family;
        }

        if (ifa->ifa_family == AF_INET)
            memcpy(addr->addr.in, RTA_DATA(rth), sizeof(addr->addr.in));
        else
            memcpy(addr->addr.in6, RTA_DATA(rth), sizeof(addr->addr.in6));

        SOL_VECTOR_FOREACH_IDX (&network->callbacks, cb, idx)
            cb->cb((void *)cb->data, link, SOL_NETWORK_LINK_CHANGED);
    }
}

static bool
_on_event(void *data, int nl_socket, unsigned int cond)
{
    int status;
    char buf[4096];
    struct iovec iov = { buf, sizeof(buf) };
    struct sockaddr_nl snl;
    struct msghdr msg = {
        .msg_name = (void *)&snl,
        .msg_namelen = sizeof(snl),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };
    struct nlmsghdr *h;

    if (cond & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        SOL_WRN("There is something wrong with the socket");
        return false;
    }

    status = recvmsg(nl_socket, &msg, MSG_WAITALL);
    if (status <= 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return true;

        SOL_WRN("Read netlink error");
        return false;
    }

    for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, (unsigned int)status);
        h = NLMSG_NEXT(h, status)) {

        switch (h->nlmsg_type) {
        case NLMSG_ERROR:
            SOL_WRN("read_netlink: Message is an error");
        case NLMSG_DONE:
            return true;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            _on_addr_event(h);
            break;
        case RTM_NEWLINK:
        case RTM_SETLINK:
        case RTM_DELLINK:
            _on_link_event(h);
            break;
        default:
            SOL_WRN("Unexpected message");
            break;
        }
    }

    return true;
}

static void
_netlink_request(int event)
{
    ssize_t n;
    char buf[sizeof(struct nlmsghdr) + sizeof(struct rtgenmsg)] = { 0 };
    char buffer_receive[4096];
    struct iovec iov = { buf, sizeof(buf) };
    struct sockaddr_nl snl = { .nl_family = AF_NETLINK };
    struct msghdr msg = {
        .msg_name = (void *)&snl,
        .msg_namelen = sizeof(snl),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };
    struct nlmsghdr *h;
    struct rtgenmsg *rt;

    h = (struct nlmsghdr *)buf;

    h->nlmsg_type = event;
    h->nlmsg_len = sizeof(buf);
    h->nlmsg_pid = getpid();
    h->nlmsg_seq = network->seq++;
    h->nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT;

    rt = (struct rtgenmsg *)(buf + sizeof(struct nlmsghdr) - 1);
    rt->rtgen_family = AF_NETLINK;

    if (sendmsg(network->nl_socket, (struct msghdr *)&msg, 0) <= 0)
        SOL_WRN("Failed on send message to get the links");

    while ((n = sol_util_fill_buffer(network->nl_socket, buffer_receive, sizeof(buffer_receive), NULL)) > 0) {
        for (h = (struct nlmsghdr *)buffer_receive; NLMSG_OK(h, n); h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE)
                return;
            if (h->nlmsg_type == NLMSG_ERROR) {
                SOL_WRN("netlink error");
                return;
            }
            if ((h->nlmsg_type == RTM_NEWLINK) || (h->nlmsg_type == RTM_DELLINK))
                _on_link_event(h);
            if ((h->nlmsg_type == RTM_NEWADDR) || (h->nlmsg_type == RTM_DELADDR))
                _on_addr_event(h);
        }
    }
}

SOL_API bool
sol_network_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    if (network != NULL) {
        network->count++;
        return true;
    }

    network = calloc(1, sizeof(*network));
    SOL_NULL_CHECK(network, false);

    network->seq = 0;
    network->count = 1;
    network->nl_socket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (network->nl_socket < 0) {
        SOL_WRN("Socket create failed!");
        goto err;
    }

    network->nl_addr.nl_family = AF_NETLINK;
    network->nl_addr.nl_pid = getpid();
    network->nl_addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

    if (bind(network->nl_socket, (struct sockaddr *)&network->nl_addr,
        sizeof(network->nl_addr)) < 0) {
        SOL_WRN("Socket bind failed!");
        goto err_bind;
    }

    network->fd = sol_fd_add(network->nl_socket,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP,
        _on_event, network);
    if (!network->fd) {
        SOL_WRN("failed to monitor the file descriptor");
        goto err_bind;
    }

    sol_vector_init(&network->links, sizeof(struct sol_network_link));
    sol_vector_init(&network->callbacks, sizeof(struct callback));

    _netlink_request(RTM_GETLINK);
    _netlink_request(RTM_GETADDR);

    return true;

err_bind:
    close(network->nl_socket);
err:
    free(network);
    network = NULL;
    return false;
}

SOL_API void
sol_network_shutdown(void)
{
    struct callback *callback;
    struct sol_network_link *link;
    struct sol_network_link_addr *addr;
    uint16_t idx;

    if (!network)
        return;

    network->count--;
    if (network->count)
        return;

    if (network->fd)
        sol_fd_del(network->fd);

    close(network->nl_socket);

    SOL_VECTOR_FOREACH_IDX (&network->links, link, idx) {
        sol_vector_clear(&link->addrs);
    }

    sol_vector_clear(&network->links);
    sol_vector_clear(&network->callbacks);
    free(network);
    network = NULL;

    (void)addr;
    (void)callback;
}

SOL_API bool
sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    struct callback *callback;

    SOL_NULL_CHECK(network, false);
    SOL_NULL_CHECK(cb, false);

    callback = sol_vector_append(&network->callbacks);
    SOL_NULL_CHECK(callback, false);

    callback->cb = cb;
    callback->data = data;

    return true;
}

SOL_API bool
sol_network_unsubscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    struct callback *callback;
    bool ret = false;
    uint16_t idx;

    SOL_NULL_CHECK(network, false);
    SOL_NULL_CHECK(cb, false);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&network->callbacks, callback, idx) {
        if ((callback->cb == cb) && (callback->data == data)) {
            sol_vector_del(&network->callbacks, idx);
            ret = true;
        }
    }

    return ret;
}

SOL_API const struct sol_vector *
sol_network_get_available_links(void)
{
    SOL_NULL_CHECK(network, NULL);

    return &network->links;
}

SOL_API char *
sol_network_link_get_name(const struct sol_network_link *link)
{
    char name[IFNAMSIZ];

    SOL_NULL_CHECK(link, NULL);

    if (unlikely(link->api_version != SOL_NETWORK_LINK_API_VERSION)) {
        SOL_WRN("Couldn't link that has unsupported version '%u', "
            "expected version is '%u'",
            link->api_version, SOL_NETWORK_LINK_API_VERSION);
        return NULL;
    }

    if (if_indextoname(link->index, name))
        return strdup(name);

    return NULL;
}

SOL_API bool
sol_network_link_up(unsigned int link_index)
{
    char buf[sizeof(struct nlmsghdr) + sizeof(struct ifinfomsg) + 512] = { 0 };
    struct iovec iov = { buf, sizeof(buf) };
    struct msghdr msg = { 0 };
    struct nlmsghdr *h;
    struct ifinfomsg *ifi;
    struct sockaddr_nl snl = { .nl_family = AF_NETLINK };
    struct rtattr *inet_container, *af_spec_container, *addr_gen_container;
    uint8_t mode = IN6_ADDR_GEN_MODE_EUI64;
    size_t buf_size = sizeof(buf);;

    SOL_NULL_CHECK(network, false);

    msg.msg_name = (void *)&snl;
    msg.msg_namelen = sizeof(snl);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;

    h = (struct nlmsghdr *)buf;

    h->nlmsg_type = RTM_SETLINK;
    h->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    h->nlmsg_pid = getpid();
    h->nlmsg_seq = network->seq++;
    h->nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT;

    ifi = (struct ifinfomsg *)(buf + sizeof(struct nlmsghdr));
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index = link_index;
    ifi->ifi_change = IFF_UP;
    ifi->ifi_flags = IFF_UP;

#define ADD_RTATTR(_attr, _len, _type)                                        \
    do {                                                                      \
        SOL_INT_CHECK(NLMSG_ALIGN(h->nlmsg_len) + sizeof(struct rtattr), > buf_size, false); \
        _attr = (struct rtattr *)(((char *)buf) + NLMSG_ALIGN(h->nlmsg_len)); \
        _attr->rta_type = _type;                                              \
        _attr->rta_len = _len;                                                \
        h->nlmsg_len = NLMSG_ALIGN(h->nlmsg_len) + RTA_ALIGN(_attr->rta_len); \
    } while (0)

    ADD_RTATTR(af_spec_container, RTA_LENGTH(0), IFLA_AF_SPEC);
    ADD_RTATTR(inet_container, RTA_LENGTH(0), AF_INET6);
    ADD_RTATTR(addr_gen_container, RTA_LENGTH(sizeof(uint8_t)), IFLA_INET6_ADDR_GEN_MODE);

    inet_container->rta_len +=  RTA_ALIGN(addr_gen_container->rta_len);
    af_spec_container->rta_len += RTA_ALIGN(inet_container->rta_len);
    memcpy(RTA_DATA(addr_gen_container), &mode, sizeof(mode));
#undef ADD_RTATTR

    if (sendmsg(network->nl_socket, (struct msghdr *)&msg, 0) <= 0) {
        SOL_WRN("Failed on send message to set link up");
        return false;
    }

    return true;
}

SOL_API bool
sol_network_link_addr_eq(const struct sol_network_link_addr *a, const struct sol_network_link_addr *b)
{
    const uint8_t *addr_a, *addr_b;
    size_t bytes;

    if (a->family != b->family)
        return false;

    if (a->family == AF_INET) {
        addr_a = a->addr.in;
        addr_b = b->addr.in;
        bytes = sizeof(a->addr.in);
    } else if (a->family == AF_INET6) {
        addr_a = a->addr.in6;
        addr_b = b->addr.in6;
        bytes = sizeof(a->addr.in6);
    } else {
        SOL_WRN("Unknown family type: %d", a->family);
        return false;
    }

    return !memcmp(addr_a, addr_b, bytes);
}
