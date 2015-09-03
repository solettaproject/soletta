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

#include "sol-flow/random.h"
#include "sol-flow-internal.h"

#include <errno.h>
#include <float.h>
#include <sol-random.h>
#include <sol-util.h>
#include <stdlib.h>
#include <time.h>

struct random_node_data {
    struct sol_random *engine;
};

static int
random_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct random_node_data *mdata = data;
    const struct sol_flow_node_type_random_int_options *opts;

    /* TODO find some way to share the same options struct between
       multiple node types */
    opts = (const struct sol_flow_node_type_random_int_options *)options;

    mdata->engine = sol_random_new(SOL_RANDOM_DEFAULT, opts->seed.val);
    SOL_NULL_CHECK(mdata->engine, -EINVAL);

    return 0;
}

static void
random_close(struct sol_flow_node *node, void *data)
{
    struct random_node_data *mdata = data;

    sol_random_del(mdata->engine);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_int_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange value = { 0, 0, INT32_MAX, 1 };
    struct random_node_data *mdata = data;

    if (!sol_random_get_int32(mdata->engine, &value.val))
        return -EINVAL;

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_INT__OUT__OUT,
        &value);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_float_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    struct sol_drange out_value = { 0, 0, INT32_MAX, 1 };

    if (!sol_random_get_double(mdata->engine, &out_value.val))
        return -EINVAL;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_FLOAT__OUT__OUT,
        &out_value);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_byte_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    uint8_t value;

    if (!sol_random_get_byte(mdata->engine, &value))
        return -EINVAL;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_BYTE__OUT__OUT,
        value);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_boolean_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    bool value;

    if (!sol_random_get_bool(mdata->engine, &value))
        return -EINVAL;

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_BOOLEAN__OUT__OUT,
        value);
}

static int
random_seed_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    int r;
    int32_t in_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_random_del(mdata->engine);
    mdata->engine = sol_random_new(SOL_RANDOM_DEFAULT, in_value);
    SOL_NULL_CHECK(mdata->engine, -EINVAL);

    return 0;
}

#include "random-gen.c"
