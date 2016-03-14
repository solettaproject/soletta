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
#include "sol-socket.h"

struct sol_socket_impl {
    struct sol_socket *(*new)(int domain, enum sol_socket_type type, int protocol);
    void (*del)(struct sol_socket *s);

    int (*set_on_read)(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data);
    int (*set_on_write)(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data);

    ssize_t (*recvmsg)(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr);

    int (*sendmsg)(struct sol_socket *s, const void *buf, size_t len,
        const struct sol_network_link_addr *cliaddr);

    int (*join_group)(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group);

    int (*bind)(struct sol_socket *s, const struct sol_network_link_addr *addr);

    int (*setsockopt)(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname, const void *optval, size_t optlen);

    int (*getsockopt)(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname, void *optval, size_t *optlen);
};

#ifdef SOL_PLATFORM_LINUX
const struct sol_socket_impl *sol_socket_linux_get_impl(void);
#elif defined(SOL_PLATFORM_RIOT)
const struct sol_socket_impl *sol_socket_riot_get_impl(void);
#endif
