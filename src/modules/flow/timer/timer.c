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

    r = sol_flow_packet_get_boolean(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

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
    return start_timer(mdata);
}

static void
timer_close(struct sol_flow_node *node, void *data)
{
    struct timer_data *mdata = data;

    stop_timer(mdata);
}

#include "timer-gen.c"
