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

#include "sol-flow/timer.h"
#include "sol-flow-internal.h"
#include "sol-mainloop.h"

#include <stdio.h>
#include <stdlib.h>

struct timer_data {
    struct sol_flow_node *node;
    struct sol_timeout *timer;
    int32_t interval;
};

static bool
timer_tick(void *data)
{
    struct timer_data *mdata = data;

    sol_flow_send_empty_packet(mdata->node, SOL_FLOW_NODE_TYPE_TIMER__OUT__OUT);
    return true;
}

static int
start_timer(struct timer_data *mdata)
{
    if (mdata->timer) {
        sol_timeout_del(mdata->timer);
        mdata->timer = NULL;
    }

    if (mdata->interval < 1)
        return 0;

    mdata->timer = sol_timeout_add(mdata->interval, timer_tick, mdata);
    SOL_NULL_CHECK(mdata->timer, -errno);
    return 0;
}

static int
stop_timer(struct timer_data *mdata)
{
    if (!mdata->timer)
        return 0;

    sol_timeout_del(mdata->timer);
    mdata->timer = NULL;
    return 0;
}

static int
timer_interval_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timer_data *mdata = data;
    struct sol_irange val;
    int r;

    r = sol_flow_packet_get_irange(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->interval == val.val)
        return 0;

    mdata->interval = val.val;
    return start_timer(mdata);
}

static int
timer_reset_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timer_data *mdata = data;

    return start_timer(mdata);
}

static int
timer_enabled_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timer_data *mdata = data;
    int r;
    bool val;

    r = sol_flow_packet_get_bool(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    if (val == !!mdata->timer)
        return 0;

    if (!val)
        return stop_timer(mdata);
    else if (!mdata->timer)
        return start_timer(mdata);

    return 0;
}

static int
timer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct timer_data *mdata = data;
    const struct sol_flow_node_type_timer_options *opts = (const struct sol_flow_node_type_timer_options *)options;

    mdata->node = node;

    if (!options)
        return 0;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_TIMER_OPTIONS_API_VERSION, -EINVAL);
    if (opts->interval < 1)
        return 0;

    mdata->interval = opts->interval;

    if (!opts->enabled)
        return 0;

    return start_timer(mdata);
}

static void
timer_close(struct sol_flow_node *node, void *data)
{
    struct timer_data *mdata = data;

    stop_timer(mdata);
}

#include "timer-gen.c"
