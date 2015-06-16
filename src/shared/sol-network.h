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

#include <stdbool.h>
#include <sys/socket.h>

#include "sol-vector.h"

#define SOL_INET_ADDR_STRLEN 48

enum sol_network_event {
    SOL_NETWORK_LINK_ADDED,
    SOL_NETWORK_LINK_REMOVED,
    SOL_NETWORK_LINK_CHANGED,
};

enum sol_network_link_flags {
    SOL_NETWORK_LINK_UP            = 1 << 0,
        SOL_NETWORK_LINK_BROADCAST     = 1 << 1,
        SOL_NETWORK_LINK_LOOPBACK      = 1 << 2,
        SOL_NETWORK_LINK_MULTICAST     = 1 << 3,
        SOL_NETWORK_LINK_RUNNING       = 1 << 4,
};

struct sol_network_link_addr {
    unsigned short int family;
    union {
        uint8_t in[4];
        uint8_t in6[16];
    } addr;
    uint16_t port;
};

struct sol_network_link {
    int index;
    enum sol_network_link_flags flags;
    struct sol_vector addrs;       /* struct sol_network_link_addr */
};

const char *sol_network_addr_to_str(const struct sol_network_link_addr *addr,
    char *buf, socklen_t len);
bool sol_network_link_addr_eq(const struct sol_network_link_addr *a,
    const struct sol_network_link_addr *b);

bool sol_network_init(void);
void sol_network_shutdown(void);
bool sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
        enum sol_network_event event),
    const void *data);
bool sol_network_unsubscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
        enum sol_network_event event),
    const void *data);

const struct sol_vector *sol_network_get_available_links(void);
char *sol_network_link_get_name(const struct sol_network_link *link);

bool sol_network_link_up(unsigned int link_index);
