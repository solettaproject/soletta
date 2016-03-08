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

ssize_t sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr);

int sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr);

int sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group);

int sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr);

int sol_socket_setsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname,
    const void *optval, size_t optlen);

int sol_socket_getsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname,
    void *optval, size_t *optlen);
