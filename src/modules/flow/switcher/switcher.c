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

#include "switcher-gen.h"
#include "sol-flow-internal.h"
#include "sol-util.h"


#define PORT_MAX (256)

struct switcher_data {
    int port_index;
};

static void
set_port_index(struct switcher_data *mdata, int port_index)
{
    mdata->port_index = port_index;
    if (mdata->port_index < 0) {
        SOL_WRN("Output port index must be greater or equal to zero. Using 0.");
        mdata->port_index = 0;
    } else if (mdata->port_index >= PORT_MAX) {
        SOL_WRN("Output port index must be less than %d. Using %d.",
            PORT_MAX, PORT_MAX - 1);
        mdata->port_index = PORT_MAX - 1;
    }
}

static int
switcher_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_switcher_boolean_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_SWITCHER_BOOLEAN_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_switcher_boolean_options *)options;

    set_port_index(data, opts->out_port.val);

    return 0;
}

static int
switcher_set_index(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r, in_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    set_port_index(data, in_value);

    return 0;
}

static int
boolean_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_BOOLEAN__OUT__OUT_0 + mdata->port_index,
        in_value);
}

static int
byte_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    unsigned char in_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_BYTE__OUT__OUT_0 + mdata->port_index,
        in_value);
}

static int
blob_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    struct sol_blob *in_value;

    r = sol_flow_packet_get_blob(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_blob_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_BLOB__OUT__OUT_0 + mdata->port_index,
        in_value);
}

static int
rgb_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    struct sol_rgb in_value;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_RGB__OUT__OUT_0 + mdata->port_index,
        &in_value);
}

static int
direction_vector_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    struct sol_direction_vector in_value;

    r = sol_flow_packet_get_direction_vector(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_DIRECTION_VECTOR__OUT__OUT_0 +
        mdata->port_index, &in_value);
}

static int
empty_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;

    return sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_EMPTY__OUT__OUT_0 + mdata->port_index);
}

static int
error_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    int code_value;
    const char *msg;

    r = sol_flow_packet_get_error(packet, &code_value, &msg);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_error_packet(node, code_value, msg);
}

static int
float_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    struct sol_drange in_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_FLOAT__OUT__OUT_0 + mdata->port_index,
        &in_value);
}

static int
int_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_INT__OUT__OUT_0 + mdata->port_index,
        &in_value);
}

static int
string_forward(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct switcher_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_SWITCHER_STRING__OUT__OUT_0 + mdata->port_index,
        in_value);
}

#undef PORT_MAX

#include "switcher-gen.c"
