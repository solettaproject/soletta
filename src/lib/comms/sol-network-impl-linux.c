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
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-network-util.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "network");

struct callback {
    void (*cb)(void *data, const struct sol_network_link *link,
        enum sol_network_event event);
    const void *data;
};

struct sol_network_hostname_handle {
    char *hostname;
    enum sol_network_family family;
    void (*cb)(void *data, const struct sol_str_slice hostname,
        const struct sol_vector *addrs_list);
    const void *data;
};

struct sol_network {
    unsigned int count;
    int nl_socket;
    struct sockaddr_nl nl_addr;
    struct sol_fd *fd;
    struct sol_timeout *hostname_worker;

    struct sol_vector links;
    struct sol_vector callbacks;
    struct sol_ptr_vector hostname_handles;

    int seq;
};

static struct sol_network *network = NULL;

SOL_API const char *
sol_network_link_addr_to_str(const struct sol_network_link_addr *addr,
    struct sol_buffer *buf)
{
    const char *r;

    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    while (1) {
        int err;

        r = inet_ntop(sol_network_sol_to_af(addr->family), &addr->addr,
            sol_buffer_at_end(buf), buf->capacity - buf->used);

        if (r || (!r && errno != ENOSPC))
            break;

        err = sol_buffer_expand(buf, SOL_INET_ADDR_STRLEN);
        SOL_INT_CHECK(err, < 0, NULL);
    }

    if (r)
        buf->used += strlen(r);
    return r;
}

SOL_API const struct sol_network_link_addr *
sol_network_link_addr_from_str(struct sol_network_link_addr *addr, const char *buf)
{
    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(buf, NULL);

    if (inet_pton(sol_network_sol_to_af(addr->family), buf, &addr->addr) != 1)
        return NULL;
    return addr;
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

    SOL_SET_API_VERSION(link->api_version = SOL_NETWORK_LINK_API_VERSION; )
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
            if (sol_network_sol_to_af(addr_itr->family) == ifa->ifa_family) {
                addr = addr_itr;
                break;
            }
        }

        if (!addr) {
            addr = sol_vector_append(&link->addrs);
            SOL_NULL_CHECK(addr);

            addr->family = sol_network_af_to_sol(ifa->ifa_family);
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
_on_event(void *data, int nl_socket, uint32_t cond)
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
        close(network->nl_socket);
        network->nl_socket = -1;
        network->fd = NULL;
        return false;
    }

    while ((status = recvmsg(nl_socket, &msg, MSG_WAITALL))) {
        if (status < 0) {
            if (errno == EAGAIN)
                return true;

            if (errno == EINTR)
                continue;

            SOL_WRN("Read netlink error");
            close(network->nl_socket);
            network->nl_socket = -1;
            network->fd = NULL;
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
    }

    return true;
}

static void
_netlink_request(int event)
{
    char buf[NLMSG_ALIGN(sizeof(struct nlmsghdr) + sizeof(struct rtgenmsg))] = { 0 };
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

    _on_event(network, network->nl_socket, SOL_FD_FLAGS_IN);
}

static int
sol_network_start_netlink(void)
{
    int err;

    SOL_NULL_CHECK(network, -ENOENT);

    if (network->fd)
        return 0;

    network->seq = 0;
    network->count = 1;
    network->nl_socket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (network->nl_socket < 0) {
        SOL_WRN("failed to create netlink socket, cannot listen to events or manage the links!");
        return -errno;
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
        if (!errno)
            errno = ENOMEM;
        goto err_bind;
    }

    _netlink_request(RTM_GETLINK);
    _netlink_request(RTM_GETADDR);
    return 0;

err_bind:
    err = -errno;
    close(network->nl_socket);
    network->nl_socket = -1;
    return err;
}

int sol_network_init(void);
void sol_network_shutdown(void);

int
sol_network_init(void)
{
    static struct sol_network _network = {};

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (network != NULL) {
        network->count++;
        return 0;
    }

    network = &_network;
    network->nl_socket = -1;

    sol_vector_init(&network->links, sizeof(struct sol_network_link));
    sol_vector_init(&network->callbacks, sizeof(struct callback));
    sol_ptr_vector_init(&network->hostname_handles);

    return 0;
}

static void
hostname_handle_free(struct sol_network_hostname_handle *ctx)
{
    free(ctx->hostname);
    free(ctx);
}

void
sol_network_shutdown(void)
{
    struct sol_network_link *link;
    uint16_t idx;
    struct sol_network_hostname_handle *ctx;

    if (!network)
        return;

    network->count--;
    if (network->count)
        return;

    if (network->fd)
        sol_fd_del(network->fd);

    if (network->hostname_worker)
        sol_timeout_del(network->hostname_worker);

    if (network->nl_socket > 0)
        close(network->nl_socket);

    SOL_VECTOR_FOREACH_IDX (&network->links, link, idx) {
        sol_vector_clear(&link->addrs);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&network->hostname_handles, ctx, idx)
        hostname_handle_free(ctx);

    sol_ptr_vector_clear(&network->hostname_handles);
    sol_vector_clear(&network->links);
    sol_vector_clear(&network->callbacks);
    network = NULL;
}

SOL_API bool
sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data)
{
    struct callback *callback;

    SOL_NULL_CHECK(cb, false);

    if (sol_network_start_netlink() < 0) return false;

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
    if (sol_network_start_netlink() < 0) return NULL;

    return &network->links;
}

SOL_API char *
sol_network_link_get_name(const struct sol_network_link *link)
{
    char name[IFNAMSIZ];

    SOL_NULL_CHECK(link, NULL);
    SOL_NETWORK_LINK_CHECK_VERSION(link, NULL);

    if (if_indextoname(link->index, name))
        return strdup(name);

    return NULL;
}

SOL_API bool
sol_network_link_up(uint16_t link_index)
{
    char buf[NLMSG_ALIGN(sizeof(struct nlmsghdr) + sizeof(struct ifinfomsg) + 512)] = { 0 };
    struct iovec iov = { buf, sizeof(buf) };
    struct msghdr msg = { 0 };
    struct nlmsghdr *h;
    struct ifinfomsg *ifi;
    struct sockaddr_nl snl = { .nl_family = AF_NETLINK };
    struct rtattr *inet_container, *af_spec_container, *addr_gen_container;
    uint8_t mode = IN6_ADDR_GEN_MODE_EUI64;
    size_t buf_size = sizeof(buf);;

    if (sol_network_start_netlink() < 0) return false;

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

#define ADD_RTATTR(_attr, _len, _type) \
    do { \
        SOL_INT_CHECK(NLMSG_ALIGN(h->nlmsg_len) + sizeof(struct rtattr), > buf_size, false); \
        _attr = (struct rtattr *)(((char *)buf) + NLMSG_ALIGN(h->nlmsg_len)); \
        _attr->rta_type = _type; \
        _attr->rta_len = _len; \
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

    if (a->family == SOL_NETWORK_FAMILY_INET) {
        addr_a = a->addr.in;
        addr_b = b->addr.in;
        bytes = sizeof(a->addr.in);
    } else if (a->family == SOL_NETWORK_FAMILY_INET6) {
        addr_a = a->addr.in6;
        addr_b = b->addr.in6;
        bytes = sizeof(a->addr.in6);
    } else {
        SOL_WRN("Unknown family type: %d", a->family);
        return false;
    }

    return !memcmp(addr_a, addr_b, bytes);
}

static bool
hostname_worker(void *data)
{
    uint16_t i;
    struct sol_network_hostname_handle *ctx;
    int r;
    struct addrinfo hints = { };
    uint8_t **base_data = network->hostname_handles.base.data;

    SOL_PTR_VECTOR_FOREACH_IDX (&network->hostname_handles, ctx, i) {
        struct addrinfo *addr_list;
        struct addrinfo *addr;
        struct sol_vector sol_addr_list;
        struct sol_network_link_addr *sol_addr;

        base_data[network->hostname_handles.base.elem_size * i] = NULL;

        sol_vector_init(&sol_addr_list, sizeof(struct sol_network_link_addr));
        switch (ctx->family) {
        case SOL_NETWORK_FAMILY_INET:
            hints.ai_family = AF_INET;
            break;
        case SOL_NETWORK_FAMILY_INET6:
            hints.ai_family = AF_INET6;
            break;
        default:
            hints.ai_family = AF_UNSPEC;
        }

        r = getaddrinfo(ctx->hostname, NULL, &hints, &addr_list);

        if (r != 0) {
            SOL_WRN("Could not fetch address info of %s."
                " Reason: %s", ctx->hostname, gai_strerror(r));
            goto err_getaddr;
        }


        for (addr = addr_list; addr != NULL; addr = addr->ai_next) {

            sol_addr = sol_vector_append(&sol_addr_list);
            SOL_NULL_CHECK_GOTO(sol_addr, err_alloc);

            sol_addr->family = sol_network_af_to_sol(addr->ai_family);

            if (addr->ai_family == AF_INET) {
                memcpy(&sol_addr->addr.in,
                    &(((struct sockaddr_in *)addr->ai_addr)->sin_addr),
                    sizeof(sol_addr->addr.in));
            } else if (addr->ai_family == AF_INET6) {
                memcpy(&sol_addr->addr.in6,
                    &(((struct sockaddr_in6 *)addr->ai_addr)->sin6_addr),
                    sizeof(sol_addr->addr.in6));
            }
        }

        freeaddrinfo(addr_list);
        ctx->cb((void *)ctx->data, sol_str_slice_from_str(ctx->hostname),
            &sol_addr_list);
        hostname_handle_free(ctx);
        sol_vector_clear(&sol_addr_list);
        continue;

err_alloc:
        freeaddrinfo(addr_list);
err_getaddr:
        ctx->cb((void *)ctx->data, sol_str_slice_from_str(ctx->hostname),
            NULL);
        hostname_handle_free(ctx);
        sol_vector_clear(&sol_addr_list);
    }

    network->hostname_worker = NULL;
    sol_ptr_vector_clear(&network->hostname_handles);
    return false;
}

SOL_API struct sol_network_hostname_handle *
sol_network_get_hostname_address_info(const struct sol_str_slice hostname,
    enum sol_network_family family, void (*host_info_cb)(void *data,
    const struct sol_str_slice host, const struct sol_vector *addrs_list),
    const void *data)
{
    struct sol_network_hostname_handle *ctx;
    int r;

    SOL_NULL_CHECK(host_info_cb, NULL);
    SOL_NULL_CHECK(network, NULL);

    ctx = calloc(1, sizeof(struct sol_network_hostname_handle));
    SOL_NULL_CHECK(ctx, NULL);

    r = sol_ptr_vector_append(&network->hostname_handles, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);

    ctx->hostname = sol_str_slice_to_string(hostname);
    SOL_NULL_CHECK_GOTO(ctx->hostname, err_hostname);

    ctx->cb = host_info_cb;
    ctx->data = data;
    ctx->family = family;

    if (!network->hostname_worker) {
        network->hostname_worker = sol_timeout_add(0, hostname_worker, NULL);
        SOL_NULL_CHECK_GOTO(network->hostname_worker, err_hostname);
    }

    return ctx;

err_hostname:
    (void)sol_ptr_vector_remove(&network->hostname_handles, ctx);
err_append:
    hostname_handle_free(ctx);
    return NULL;
}

SOL_API int
sol_network_cancel_get_hostname_address_info(
    struct sol_network_hostname_handle *handle)
{
    int r;

    SOL_NULL_CHECK(handle, -EINVAL);

    r = sol_ptr_vector_remove(&network->hostname_handles, handle);
    SOL_INT_CHECK(r, < 0, r);
    if (!sol_ptr_vector_get_len(&network->hostname_handles)) {
        sol_timeout_del(network->hostname_worker);
        network->hostname_worker = NULL;
    }

    hostname_handle_free(handle);
    return 0;
}
