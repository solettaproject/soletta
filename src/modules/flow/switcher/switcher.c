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

#include "sol-flow/switcher.h"
#include "sol-flow-internal.h"
#include "sol-util.h"


#define PORT_MAX (SOL_FLOW_NODE_TYPE_SWITCHER_BOOLEAN__IN__IN_LAST + 1)

struct switcher_data {
    int in_port_index;
    int out_port_index;
    struct sol_ptr_vector last;
    bool keep_state : 1;
};

static void
set_port_index(int *port_index, int index_value)
{
    *port_index = index_value;
    if (*port_index < 0) {
        SOL_WRN("Output port index must be greater or equal to zero. Using 0.");
        *port_index = 0;
    } else if (*port_index >= PORT_MAX) {
        SOL_WRN("Output port index must be less than %d. Using %d.",
            PORT_MAX, PORT_MAX - 1);
        *port_index = PORT_MAX - 1;
    }
}

static int
switcher_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_switcher_boolean_options *opts;
    struct switcher_data *mdata = data;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_SWITCHER_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_switcher_boolean_options *)options;

    set_port_index(&mdata->out_port_index, opts->out_port.val);
    set_port_index(&mdata->in_port_index, opts->in_port.val);

    if (opts->keep_state) {
        r = sol_ptr_vector_init_n(&mdata->last, PORT_MAX);
        SOL_INT_CHECK(r, < 0, r);
        mdata->keep_state = true;
    }

    return 0;
}

static void
switcher_close(struct sol_flow_node *node, void *data)
{
    struct switcher_data *mdata = data;
    void *last_packet;
    int i;

    if (!mdata->keep_state)
        return;

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->last, last_packet, i) {
        if (last_packet)
            sol_flow_packet_del(last_packet);
    }
    sol_ptr_vector_clear(&mdata->last);
}

static int
send_packet(struct switcher_data *mdata, struct sol_flow_node *node, const struct sol_flow_packet *packet)
{
    struct sol_flow_packet *new_packet;

    new_packet = sol_flow_packet_dup(packet);
    SOL_NULL_CHECK(new_packet, -EINVAL);

    return sol_flow_send_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_BOOLEAN__OUT__OUT_0 + mdata->out_port_index,
        new_packet);
}

static int
send_last(struct switcher_data *mdata, struct sol_flow_node *node)
{
    const struct sol_flow_packet *last_packet;

    if (!mdata->keep_state)
        return 0;

    last_packet = sol_ptr_vector_get(&mdata->last, mdata->in_port_index);
    if (!last_packet)
        return 0;

    return send_packet(mdata, node, last_packet);
}

static int
switcher_set_output_index(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    set_port_index(&mdata->out_port_index, in_value);

    return send_last(mdata, node);
}

static int
switcher_set_input_index(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    set_port_index(&mdata->in_port_index, in_value);

    return send_last(mdata, node);
}

static int
forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;

    if (mdata->keep_state) {
        struct sol_flow_packet *new_packet, *last_packet;

        new_packet = sol_flow_packet_dup(packet);
        SOL_NULL_CHECK(new_packet, -EINVAL);

        last_packet = sol_ptr_vector_get(&mdata->last, port);
        if (last_packet)
            sol_flow_packet_del(last_packet);

        sol_ptr_vector_set(&mdata->last, port, new_packet);
    }

    if (port != mdata->in_port_index)
        return 0;

    return send_packet(mdata, node, packet);
}

static int
empty_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;

    if (mdata->keep_state)
        sol_ptr_vector_set(&mdata->last, port, (void *)1);

    if (port != mdata->in_port_index)
        return 0;

    return sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_EMPTY__OUT__OUT_0 + mdata->out_port_index);
}

static void
empty_close(struct sol_flow_node *node, void *data)
{
    struct switcher_data *mdata = data;

    if (!mdata->keep_state)
        return;
    sol_ptr_vector_clear(&mdata->last);
}

static int
send_last_empty(struct switcher_data *mdata, struct sol_flow_node *node)
{
    const struct sol_flow_packet *last_packet;

    if (!mdata->keep_state)
        return 0;

    last_packet = sol_ptr_vector_get(&mdata->last, mdata->in_port_index);
    if (!last_packet)
        return 0;

    return sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_EMPTY__OUT__OUT_0 + mdata->out_port_index);
}

static int
empty_set_output_index(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    set_port_index(&mdata->out_port_index, in_value);

    return send_last_empty(mdata, node);
}

static int
empty_set_input_index(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    set_port_index(&mdata->in_port_index, in_value);

    return send_last_empty(mdata, node);
}

static int
error_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    const char *msg;
    int code_value, r;

    if (port != mdata->in_port_index)
        return 0;

    r = sol_flow_packet_get_error(packet, &code_value, &msg);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_error_packet_str(node, code_value, msg);
}

#undef PORT_MAX

#include "switcher-gen.c"
