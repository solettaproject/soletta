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
sol_socket_ip_new(const struct sol_socket_options *options)
{
    struct sol_socket *s;
    struct sol_socket_ip_options *opts;

    SOL_SOCKET_OPTIONS_CHECK_API_VERSION(options, NULL);

    /*
     * This SUB_API check should be done by constructors. This is being done
     * here only because the strange behaviour/implementation of socket_dtls.
     * TODO: Fix sol_socket_dtls
     */
    SOL_SOCKET_OPTIONS_CHECK_SUB_API_VERSION(options, SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION, NULL);

    opts = (struct sol_socket_ip_options *)options;

    if (opts->secure) {
#ifdef DTLS
        s = sol_socket_default_dtls_new(options);
#else
        SOL_WRN("DTLS is not enabled, secure socket is not possible");
        errno = ENOSYS;
        return NULL;
#endif
    } else {
        s = sol_socket_ip_default_new(options);
    }

    return s;
}

SOL_API void
sol_socket_del(struct sol_socket *s)
{
    SOL_EXP_CHECK(!(s && s->type));
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type);
    SOL_NULL_CHECK(s->type->del);

    s->type->del(s);
}

SOL_API int
sol_socket_set_read_monitor(struct sol_socket *s, bool on)
{
    SOL_EXP_CHECK(!(s && s->type), -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type, -EINVAL);
    SOL_NULL_CHECK(s->type->set_read_monitor, -ENOSYS);

    return s->type->set_read_monitor(s, on);
}

SOL_API int
sol_socket_set_write_monitor(struct sol_socket *s, bool on)
{
    SOL_EXP_CHECK(!(s && s->type), -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type, -EINVAL);
    SOL_NULL_CHECK(s->type->set_write_monitor, -ENOSYS);

    return s->type->set_write_monitor(s, on);
}

SOL_API ssize_t
sol_socket_recvmsg(struct sol_socket *s, struct sol_buffer *buffer,
    struct sol_network_link_addr *cliaddr)
{
    SOL_EXP_CHECK(!(s && s->type), -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type, -EINVAL);
    SOL_NULL_CHECK(s->type->recvmsg, -ENOSYS);
    SOL_NULL_CHECK(buffer, -EINVAL);

    return s->type->recvmsg(s, buffer, cliaddr);
}

SOL_API ssize_t
sol_socket_sendmsg(struct sol_socket *s, const struct sol_buffer *buffer,
    const struct sol_network_link_addr *cliaddr)
{
    SOL_EXP_CHECK(!(s && s->type), -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type, -EINVAL);
    SOL_NULL_CHECK(s->type->sendmsg, -ENOSYS);
    SOL_NULL_CHECK(buffer, -EINVAL);

    return s->type->sendmsg(s, buffer, cliaddr);
}

SOL_API int
sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    SOL_EXP_CHECK(!(s && s->type), -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type, -EINVAL);
    SOL_NULL_CHECK(s->type->join_group, -ENOSYS);

    return s->type->join_group(s, ifindex, group);
}

SOL_API int
sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    SOL_EXP_CHECK(!(s && s->type), -EINVAL);
    SOL_SOCKET_TYPE_CHECK_API_VERSION(s->type, -EINVAL);
    SOL_NULL_CHECK(s->type->bind, -ENOSYS);

    return s->type->bind(s, addr);
}
