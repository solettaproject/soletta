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

#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-network");

#include "network-gen.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-util.h"
#include "sol-vector.h"

struct network_data {
    struct sol_flow_node *node;
    bool connected;

    /*
     * Options to match the link
     */
    regex_t regex;
    struct sol_ptr_vector links;
};

static bool
_compile_regex(regex_t *r, const char *text)
{
    char error_message[PATH_MAX];
    int status = regcomp(r, text, REG_EXTENDED | REG_NEWLINE);

    if (!status)
        return true;

    regerror(status, r, error_message, sizeof(error_message));
    SOL_WRN("Regex error compiling '%s': %s", text, error_message);

    return false;
}

static bool
_match_link(const struct network_data *mdata, const struct sol_network_link *link)
{
    char *name = sol_network_link_get_name(link);
    const char *p = name;
    regmatch_t m;

    if (!name)
        return false;
    if (!regexec(&mdata->regex, p, 1, &m, 0)) {
        free(name);
        return true;
    }
    free(name);

    return false;
}

static bool
_check_connected(struct sol_ptr_vector *links)
{
    struct sol_network_link *itr;
    uint16_t idx;

    SOL_PTR_VECTOR_FOREACH_IDX (links, itr, idx)
        if ((itr->flags & SOL_NETWORK_LINK_RUNNING) && !(itr->flags & SOL_NETWORK_LINK_LOOPBACK))
            return true;

    return false;
}

static void
_on_network_event(void *data, const struct sol_network_link *link, enum sol_network_event event)
{
    struct network_data *mdata = data;
    struct sol_network_link *itr;
    bool connected;
    uint16_t idx;

    if (!_match_link(mdata, link))
        return;

    switch (event) {
    case SOL_NETWORK_LINK_CHANGED:
    case SOL_NETWORK_LINK_ADDED:
        sol_ptr_vector_append(&mdata->links, (struct sol_network_link *)link);
        break;
    case SOL_NETWORK_LINK_REMOVED:
        SOL_PTR_VECTOR_FOREACH_IDX (&mdata->links, itr, idx) {
            if (itr == link) {
                sol_ptr_vector_del(&mdata->links, idx);
                break;
            }
        }
        break;
    default:
        break;
    }

    connected = _check_connected(&mdata->links);
    if (connected != mdata->connected) {
        mdata->connected = connected;
        sol_flow_send_boolean_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_NETWORK_BOOLEAN__OUT__OUT,
            mdata->connected);
    }
}

static int
network_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct network_data *mdata = data;
    const struct sol_vector *links;
    struct sol_network_link *itr;
    uint16_t idx;
    const struct sol_flow_node_type_network_boolean_options *opts =
        (const struct sol_flow_node_type_network_boolean_options *)options;

    SOL_NULL_CHECK(options, -EINVAL);

    if (!_compile_regex(&mdata->regex, opts->address))
        return -EINVAL;

    if (sol_network_init() == false) {
        SOL_WRN("Could not initialize the network");
        goto err;
    }

    if (!sol_network_subscribe_events(_on_network_event, mdata))
        goto err_net;

    sol_ptr_vector_init(&mdata->links);

    links = sol_network_get_available_links();

    if (links) {
        SOL_VECTOR_FOREACH_IDX (links, itr, idx) {
            if (_match_link(mdata, itr)) {
                sol_ptr_vector_append(&mdata->links, itr);
                mdata->connected |= (itr->flags & SOL_NETWORK_LINK_RUNNING) &&
                                    !(itr->flags & SOL_NETWORK_LINK_LOOPBACK);
            }
        }
    }

    mdata->node = node;

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_NETWORK_BOOLEAN__OUT__OUT,
        _check_connected(&mdata->links));

err_net:
    SOL_WRN("Failed to init the network");
    sol_ptr_vector_clear(&mdata->links);
    sol_network_shutdown();
err:
    regfree(&mdata->regex);
    return -EINVAL;
}

static void
network_close(struct sol_flow_node *node, void *data)
{
    struct network_data *mdata = data;
    struct sol_network_link *itr;
    uint16_t idx;

    regfree(&mdata->regex);
    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->links, itr, idx)
        sol_ptr_vector_del(&mdata->links, idx);
    sol_ptr_vector_clear(&mdata->links);
    sol_network_unsubscribe_events(_on_network_event, mdata);
    sol_network_shutdown();

    (void)itr;
}


#include "network-gen.c"
