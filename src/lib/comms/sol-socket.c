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

SOL_API struct sol_socket *
sol_socket_new(int domain, enum sol_socket_type type, int protocol)
{
    const struct sol_socket_impl *impl = NULL;
    struct sol_socket *socket;

#ifdef DTLS
    bool dtls = false;

    if (type == SOL_SOCKET_DTLS) {
        dtls = true;
        type = SOL_SOCKET_UDP;
    }
#endif

    impl = sol_socket_get_impl();

    SOL_NULL_CHECK(impl, NULL);

    socket = impl->new(domain, type, protocol);
    if (socket)
        socket->impl = impl;

#ifdef DTLS
    if (dtls) {
        struct sol_socket *dtls_wrapped = sol_socket_dtls_wrap_socket(socket);

        if (dtls_wrapped)
            return dtls_wrapped;

        sol_socket_del(socket);
        socket = NULL;
    }
#endif

    return socket;
}

SOL_API void
sol_socket_del(struct sol_socket *s)
{
    SOL_NULL_CHECK(s);
    SOL_NULL_CHECK(s->impl->del);

    s->impl->del(s);
}

SOL_API int
sol_socket_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->set_on_read, -ENOSYS);

    return s->impl->set_on_read(s, cb, data);
}

SOL_API int
sol_socket_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->set_on_write, -ENOSYS);

    return s->impl->set_on_write(s, cb, data);
}

SOL_API ssize_t
sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->recvmsg, -ENOSYS);

    return s->impl->recvmsg(s, buf, len, cliaddr);
}

SOL_API int
sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->sendmsg, -ENOSYS);

    return s->impl->sendmsg(s, buf, len, cliaddr);
}

SOL_API int
sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->join_group, -ENOSYS);

    return s->impl->join_group(s, ifindex, group);
}

SOL_API int
sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->bind, -ENOSYS);

    return s->impl->bind(s, addr);
}

SOL_API int
sol_socket_setsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname, const void *optval, size_t optlen)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->setsockopt, -ENOSYS);

    return s->impl->setsockopt(s, level, optname, optval, optlen);
}

SOL_API int
sol_socket_getsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname, void *optval, size_t *optlen)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->getsockopt, -ENOSYS);

    return s->impl->getsockopt(s, level, optname, optval, optlen);
}
