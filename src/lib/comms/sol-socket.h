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

#pragma once

#include "sol-network.h"

struct sol_socket_impl;

struct sol_socket {
    const struct sol_socket_impl *impl;
};

enum sol_socket_type {
    SOL_SOCKET_UDP,
#ifdef DTLS
    SOL_SOCKET_DTLS,
#endif
};

enum sol_socket_option {
    SOL_SOCKET_OPTION_REUSEADDR,
    SOL_SOCKET_OPTION_REUSEPORT
};

enum sol_socket_level {
    SOL_SOCKET_LEVEL_SOCKET,
    SOL_SOCKET_LEVEL_IP,
    SOL_SOCKET_LEVEL_IPV6,
};

struct sol_socket *sol_socket_new(int domain, enum sol_socket_type type, int protocol);
void sol_socket_del(struct sol_socket *s);

int sol_socket_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data);
int sol_socket_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data);

/* If the socket's type is SOL_SOCKET_UDP, @a buf may be @c NULL, and
 * in this case the function will only peek the incoming packet queue
 * (not removing data from it), returning the number of bytes needed
 * to store the next datagram and ignoring the cliaddr argument. This
 * way, the user may allocate the exact number of bytes to hold the
 * message contents. */
ssize_t sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr);

int sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr);

int sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group);

int sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr);

int sol_socket_setsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname,
    const void *optval, size_t optlen);

int sol_socket_getsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname,
    void *optval, size_t *optlen);
