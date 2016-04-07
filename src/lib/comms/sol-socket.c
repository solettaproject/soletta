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

#include "sol-socket.h"

#include "sol-common-buildopts.h"
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-network.h"
#include "sol-socket-impl.h"

#ifdef DTLS
#include "sol-socket-dtls.h"
#endif

#include <errno.h>

#ifndef SOL_NO_API_VERSION

#define SOL_SOCKET_DEFAULT_OPTIONS_CHECK_API_VERSION(options_, ...) \
    if (SOL_UNLIKELY((options_)->api_version != \
        SOL_SOCKET_DEFAULT_OPTIONS_API_VERSION)) { \
        SOL_ERR("Unexpected API version (socket options is %u, expected %u)", \
            (options_)->api_version, SOL_SOCKET_DEFAULT_OPTIONS_API_VERSION); \
        return __VA_ARGS__; \
    }

#define SOL_SOCKET_TYPE_CHECK_API_VERSION(type_, ...) \
    if (SOL_UNLIKELY((type_)->api_version != \
        SOL_SOCKET_TYPE_API_VERSION)) { \
        SOL_ERR("Unexpected API version (socket type is %u, expected %u)", \
            (type_)->api_version, SOL_SOCKET_TYPE_API_VERSION); \
        return __VA_ARGS__; \
    }

#define SOL_SOCKET_CHECK_API_VERSION(sock_, ...) \
    if (SOL_UNLIKELY((sock_)->api_version != \
        SOL_SOCKET_API_VERSION)) { \
        SOL_ERR("Unexpected API version (socket is %u, expected %u)", \
            (sock_)->api_version, SOL_SOCKET_API_VERSION); \
        return __VA_ARGS__; \
    }

#else

#define SOL_SOCKET_TYPE_CHECK_API_VERSION(type_, ...)
#define SOL_SOCKET_CHECK_API_VERSION(sock_, ...)
#define SOL_SOCKET_DEFAULT_OPTIONS_CHECK_API_VERSION(options_, ...)

#endif

SOL_API struct sol_socket *
sol_socket_new(const struct sol_socket_default_options *options)
{
    struct sol_socket *s;

    SOL_SOCKET_DEFAULT_OPTIONS_CHECK_API_VERSION(options, NULL);

    if (options->secure) {
#ifdef DTLS
        s = sol_socket_default_dtls_new(options);
#else
        SOL_WRN("DTLS is not enabled, secure socket is not possible");
        return NULL;
#endif
    } else {
        s = sol_socket_default_new(options);
    }

    return s;
}

SOL_API void
sol_socket_del(struct sol_socket *s)
{
    SOL_NULL_CHECK(s);
    SOL_SOCKET_CHECK_API_VERSION(s);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s);
    SOL_NULL_CHECK(s->type->del);

    s->type->del(s);
}

SOL_API int
sol_socket_set_read_monitor(struct sol_socket *s, bool on)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_SOCKET_CHECK_API_VERSION(s, -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s, -EINVAL);
    SOL_NULL_CHECK(s->type->set_read_monitor, -ENOSYS);

    return s->type->set_read_monitor(s, on);
}

SOL_API int
sol_socket_set_write_monitor(struct sol_socket *s, bool on)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_SOCKET_CHECK_API_VERSION(s, -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s, -EINVAL);
    SOL_NULL_CHECK(s->type->set_write_monitor, -ENOSYS);

    return s->type->set_write_monitor(s, on);
}

SOL_API ssize_t
sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_SOCKET_CHECK_API_VERSION(s, -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s, -EINVAL);
    SOL_NULL_CHECK(s->type->recvmsg, -ENOSYS);

    return s->type->recvmsg(s, buf, len, cliaddr);
}

SOL_API int
sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_SOCKET_CHECK_API_VERSION(s, -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s, -EINVAL);
    SOL_NULL_CHECK(s->type->sendmsg, -ENOSYS);

    return s->type->sendmsg(s, buf, len, cliaddr);
}

SOL_API int
sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_SOCKET_CHECK_API_VERSION(s, -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s, -EINVAL);
    SOL_NULL_CHECK(s->type->join_group, -ENOSYS);

    return s->type->join_group(s, ifindex, group);
}

SOL_API int
sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_SOCKET_CHECK_API_VERSION(s, -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s, -EINVAL);
    SOL_NULL_CHECK(s->type->bind, -ENOSYS);

    return s->type->bind(s, addr);
}
