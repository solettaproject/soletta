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

#include "app-gen.h"
#include "sol-flow-internal.h"
#include "sol-mainloop.h"
#include "sol-util.h"

static int
check_index(struct sol_flow_node *node, int index)
{
    int count = sol_argc();

    if (index < 0) {
        sol_flow_send_error_packet(node, EINVAL,
            "Argument position (%d) must be non negative value.",
            index);
        return -EINVAL;
    }

    if (index >= count) {
        sol_flow_send_error_packet(node, EINVAL,
            "Argument position (%d) is greater than arguments length (%d)",
            index, count);
        return -EINVAL;
    }

    return 0;
}

static int
argv_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_app_argv_options *opts;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_APP_ARGV_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_app_argv_options *)options;

    r = check_index(node, opts->index.val);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_APP_ARGV__OUT__OUT, sol_argv()[opts->index.val]);
}

static int
argc_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_APP_ARGC_ARGV__OUT__ARGC, sol_argc());
}

static int
argv_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = check_index(node, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_APP_ARGC_ARGV__OUT__OUT, sol_argv()[in_value]);
}

static int
quit_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    sol_quit();
    return 0;
}

static int
quit_with_code_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_quit_with_code(in_value);
    return 0;
}

static int
quit_with_error_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_error(packet, &in_value, NULL);
    SOL_INT_CHECK(r, < 0, r);

    sol_quit_with_code(in_value);
    return 0;
}

#include "app-gen.c"
