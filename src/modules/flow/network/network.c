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

#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sol-flow/network.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-flow-internal.h"

struct network_data {
    struct sol_flow_node *node;
    bool connected;
    bool regex_initialized;

    /*
     * Options to match the link
     */
    regex_t regex;
    struct sol_ptr_vector links;
};

static bool
_compile_regex(struct network_data *mdata, const char *text)
{
    char error_message[256];
    int status;

    if (mdata->regex_initialized)
        regfree(&mdata->regex);

    status = regcomp(&mdata->regex, text, REG_EXTENDED | REG_NEWLINE);

    if (!status) {
        mdata->regex_initialized = true;
        return true;
    }

    regerror(status, &mdata->regex, error_message, sizeof(error_message));
    SOL_WRN("Regex error compiling '%s': %s", text, error_message);
    mdata->regex_initialized = false;

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

    if (mdata->regex_initialized && !regexec(&mdata->regex, p, 1, &m, 0)) {
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
    int r;

    SOL_NETWORK_LINK_CHECK_VERSION(link);

    if (!_match_link(mdata, link))
        return;

    switch (event) {
    case SOL_NETWORK_LINK_CHANGED:
    case SOL_NETWORK_LINK_ADDED:
        r = sol_ptr_vector_append(&mdata->links, (struct sol_network_link *)link);
        SOL_INT_CHECK(r, < 0);

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
        sol_flow_send_bool_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_NETWORK_BOOLEAN__OUT__OUT,
            mdata->connected);
    }
}

static int
_setup_links(struct network_data *mdata, const char *addr)
{
    const struct sol_vector *links;
    struct sol_network_link *itr;
    uint16_t idx;

    if (!_compile_regex(mdata, addr))
        return -EINVAL;

    links = sol_network_get_available_links();

    sol_ptr_vector_clear(&mdata->links);

    if (links) {
        SOL_VECTOR_FOREACH_IDX (links, itr, idx) {
            SOL_NETWORK_LINK_CHECK_VERSION(itr, -EINVAL);
            if (_match_link(mdata, itr)) {
                int r;

                r = sol_ptr_vector_append(&mdata->links, itr);
                SOL_INT_CHECK_GOTO(r, < 0, err_net);

                mdata->connected |= (itr->flags & SOL_NETWORK_LINK_RUNNING) &&
                    !(itr->flags & SOL_NETWORK_LINK_LOOPBACK);
            }
        }
    }

    return 0;

err_net:
    SOL_WRN("Failed to subscribe to network events");
    sol_ptr_vector_clear(&mdata->links);
    regfree(&mdata->regex);
    return -EINVAL;
}

static int
network_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct network_data *mdata = data;
    int r;
    const struct sol_flow_node_type_network_boolean_options *opts =
        (const struct sol_flow_node_type_network_boolean_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_NETWORK_BOOLEAN_OPTIONS_API_VERSION, -EINVAL);

    r = sol_network_subscribe_events(_on_network_event, mdata);
    SOL_INT_CHECK(r, < 0, r);

    sol_ptr_vector_init(&mdata->links);
    mdata->node = node;

    if (!opts->address)
        return 0;

    r = _setup_links(mdata, opts->address);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_NETWORK_BOOLEAN__OUT__OUT,
        _check_connected(&mdata->links));

err_exit:
    sol_network_unsubscribe_events(_on_network_event, mdata);
    return r;
}

static void
network_close(struct sol_flow_node *node, void *data)
{
    struct network_data *mdata = data;

    if (mdata->regex_initialized)
        regfree(&mdata->regex);
    sol_ptr_vector_clear(&mdata->links);
    sol_network_unsubscribe_events(_on_network_event, mdata);
}

static int
network_address_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct network_data *mdata = data;
    const char *reg;
    int r;

    r = sol_flow_packet_get_string(packet, &reg);
    SOL_INT_CHECK(r, < 0, r);

    r = _setup_links(mdata, reg);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_NETWORK_BOOLEAN__OUT__OUT,
        _check_connected(&mdata->links));
}


#include "network-gen.c"
