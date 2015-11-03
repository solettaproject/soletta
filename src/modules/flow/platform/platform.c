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

#include "sol-flow/platform.h"
#include "sol-platform.h"
#include "sol-flow-internal.h"

// =============================================================================
// PLATFORM
// =============================================================================

struct platform_data {
    struct sol_flow_node *node;
    enum sol_platform_state state;
};

static int
state_dispatch_ready(struct platform_data *mdata)
{
    return sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_PLATFORM__OUT__READY,
        mdata->state == SOL_PLATFORM_STATE_RUNNING);
}

static int
state_dispatch(struct platform_data *mdata)
{
    return state_dispatch_ready(mdata);
    /* TODO dispatch irange packet */
}

static void
on_state_changed(void *data, enum sol_platform_state state)
{
    struct platform_data *mdata = data;

    SOL_DBG("state changed %d -> %d", mdata->state, state);
    mdata->state = state;
    state_dispatch(mdata);
}

static int
platform_trigger_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_data *mdata = data;

    return state_dispatch(mdata);
}

static int
platform_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct platform_data *mdata = data;

    mdata->node = node;
    sol_platform_add_state_monitor(on_state_changed, mdata);
    mdata->state = sol_platform_get_state();

    return state_dispatch_ready(mdata);
}

static void
platform_close(struct sol_flow_node *node, void *data)
{
    struct platform_data *mdata = data;

    sol_platform_del_state_monitor(on_state_changed, mdata);
}

// =============================================================================
// PLATFORM SERVICE
// =============================================================================

struct platform_service_data {
    struct sol_flow_node *node;
    char *service_name;
    enum sol_platform_service_state state;
};

static void
service_state_dispatch_active(struct platform_service_data *mdata)
{
    sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_PLATFORM_SERVICE__OUT__ACTIVE,
        mdata->state == SOL_PLATFORM_SERVICE_STATE_ACTIVE);
}

static void
service_state_dispatch(struct platform_service_data *mdata)
{
    service_state_dispatch_active(mdata);
    /* TODO dispatch irange packet */
}

static void
on_service_state_changed(void *data, const char *service, enum sol_platform_service_state state)
{
    struct platform_service_data *mdata = data;

    SOL_DBG("service %s state changed %d -> %d", service, mdata->state, state);
    mdata->state = state;
    service_state_dispatch(mdata);
}

static int
platform_service_trigger_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_service_data *mdata = data;

    service_state_dispatch(mdata);
    return 0;
}

static int
platform_service_start_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_service_data *mdata = data;

    return sol_platform_start_service(mdata->service_name);
}

static int
platform_service_stop_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_service_data *mdata = data;

    return sol_platform_stop_service(mdata->service_name);
}

static int
platform_service_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct platform_service_data *mdata = data;
    const struct sol_flow_node_type_platform_service_options *opts;

    SOL_NULL_CHECK_MSG(options, -1, "Platform Service Node: Options not found.");
    opts = (const struct sol_flow_node_type_platform_service_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_PLATFORM_SERVICE_OPTIONS_API_VERSION, -EINVAL);

    mdata->service_name = strdup(opts->service_name);
    SOL_NULL_CHECK(mdata->service_name, -ENOMEM);

    mdata->node = node;

    sol_platform_add_service_monitor(on_service_state_changed, mdata->service_name, mdata);
    mdata->state = sol_platform_get_service_state(mdata->service_name);

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_PLATFORM_SERVICE__OUT__ACTIVE,
        (mdata->state == SOL_PLATFORM_SERVICE_STATE_ACTIVE));
}

static void
platform_service_close(struct sol_flow_node *node, void *data)
{
    struct platform_service_data *mdata = data;

    sol_platform_del_service_monitor(on_service_state_changed, mdata->service_name, mdata);

    free(mdata->service_name);
}

static int
platform_machine_id_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    const char *id;

    id = sol_platform_get_machine_id();
    if (!id) {
        sol_flow_send_error_packet(node, ENOSYS,
            "Fail on retrieving machine id -- not available");
        return 0; /* do not fail to create node */
    }

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_PLATFORM_MACHINE_ID__OUT__OUT, id);
}

#include "platform-gen.c"
