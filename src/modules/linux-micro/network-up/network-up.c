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

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-network-up");

#include "sol-platform-linux-micro.h"
#include "sol-network.h"
#include "sol-util-internal.h"

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
    if (!links)
        return 0;

    SOL_VECTOR_FOREACH_IDX (links, itr, idx)
        sol_network_link_up(itr->index);

    return 0;
}

static int
network_up_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    r = sol_network_subscribe_events(_network_event_cb, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static void
network_up_shutdown(const struct sol_platform_linux_micro_module *module, const char *service)
{
    sol_network_unsubscribe_events(_network_event_cb, NULL);
}

SOL_PLATFORM_LINUX_MICRO_MODULE(NETWORK_UP,
    .name = "network-up",
    .init = network_up_init,
    .shutdown = network_up_shutdown,
    .start = network_up_start,
    );
