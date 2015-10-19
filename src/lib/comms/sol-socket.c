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

#include "sol-socket.h"

#include "sol-common-buildopts.h"
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-network.h"
#include "sol-socket-impl.h"

#include <errno.h>

SOL_API struct sol_socket *
sol_socket_new(int domain, enum sol_socket_type type, int protocol)
{
    const struct sol_socket_impl *impl = NULL;
    struct sol_socket *socket;

#ifdef SOL_PLATFORM_LINUX
    impl = sol_socket_linux_get_impl();
#elif SOL_PLATFORM_RIOT
    impl = sol_socket_riot_get_impl();
#endif

    SOL_NULL_CHECK(impl, NULL);

    socket = impl->new(domain, type, protocol);
    if (socket)
        socket->impl = impl;

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
sol_socket_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->set_on_read, -ENOSYS);

    return s->impl->set_on_read(s, cb, data);
}

SOL_API int
sol_socket_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    SOL_NULL_CHECK(s, -EINVAL);
    SOL_NULL_CHECK(s->impl->set_on_write, -ENOSYS);

    return s->impl->set_on_write(s, cb, data);
}

SOL_API int
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
