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

#include "sol-flow/random.h"
#include "sol-flow-internal.h"

#include <errno.h>
#include <float.h>
#include <sol-random.h>
#include <sol-util-internal.h>
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

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_RANDOM_INT_OPTIONS_API_VERSION, -EINVAL);

    /* TODO find some way to share the same options struct between
       multiple node types */
    opts = (const struct sol_flow_node_type_random_int_options *)options;

    mdata->engine = sol_random_new(SOL_RANDOM_DEFAULT, opts->seed);
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
    struct sol_irange value = { 0, INT32_MIN, INT32_MAX, 1 };
    struct random_node_data *mdata = data;
    int r;

    r = sol_random_get_int32(mdata->engine, &value.val);
    if (r < 0)
        return r;

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
    int r;

    r = sol_random_get_double(mdata->engine, &out_value.val);
    if (r < 0)
        return r;

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
    int r;

    r = sol_random_get_byte(mdata->engine, &value);
    if (r < 0)
        return r;

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
    int r;

    r = sol_random_get_bool(mdata->engine, &value);
    if (r < 0)
        return r;

    return sol_flow_send_bool_packet(node,
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
