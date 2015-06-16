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

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-network-up");

#include "sol-platform-linux-micro.h"
#include "sol-network.h"
#include "sol-util.h"

static void
_network_event_cb(void *data, const struct sol_network_link *link, enum sol_network_event event)
{
    switch (event) {
    case SOL_NETWORK_LINK_ADDED:
        sol_network_link_up(link->index);
        break;
    case SOL_NETWORK_LINK_CHANGED:
    case SOL_NETWORK_LINK_REMOVED:
    default:
        break;
    }
}

static int
network_up_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    uint16_t idx;
    struct sol_network_link *itr;
    const struct sol_vector *links;

    links = sol_network_get_available_links();
    SOL_VECTOR_FOREACH_IDX (links, itr, idx) {
        sol_network_link_up(itr->index);
    }

    return 0;
}

static int
network_up_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    sol_network_init();
    if (!sol_network_subscribe_events(_network_event_cb, NULL)) {
        sol_network_shutdown();
        return -1;
    }

    return 0;
}

static void
network_up_shutdown(const struct sol_platform_linux_micro_module *module, const char *service)
{
    sol_network_unsubscribe_events(_network_event_cb, NULL);
    sol_network_shutdown();
}

SOL_PLATFORM_LINUX_MICRO_MODULE(NETWORK_UP,
    .name = "network-up",
    .init = network_up_init,
    .shutdown = network_up_shutdown,
    .start = network_up_start,
    );
