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

#include "constant-gen.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

struct sol_constant_byte {
    unsigned char byte;
};

struct sol_constant_boolean {
    bool boolean;
};

struct sol_constant_string {
    char *string;
};

static int
constant_irange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_irange *mdata = data;
    const struct sol_flow_node_type_constant_int_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_INT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_int_options *)options;

    *mdata = opts->value;

    return 0;
}

static int
irange_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct sol_irange *mdata = data;

    *packet = sol_flow_packet_new_irange(mdata);
    SOL_NULL_CHECK(*packet, -ENOMEM);

    return 0;
}

static int
constant_drange_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_drange *mdata = data;
    const struct sol_flow_node_type_constant_float_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_FLOAT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_float_options *)options;

    *mdata = opts->value;

    return 0;
}

static int
drange_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct sol_drange *mdata = data;

    *packet = sol_flow_packet_new_drange(mdata);
    SOL_NULL_CHECK(*packet, -ENOMEM);

    return 0;
}

static int
constant_byte_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_constant_byte *mdata = data;
    const struct sol_flow_node_type_constant_byte_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_BYTE_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_byte_options *)options;

    mdata->byte = opts->value;

    return 0;
}

static int
byte_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct sol_constant_byte *mdata = data;

    *packet = sol_flow_packet_new_byte(mdata->byte);
    SOL_NULL_CHECK(*packet, -ENOMEM);

    return 0;
}

static int
constant_boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_constant_boolean *mdata = data;
    const struct sol_flow_node_type_constant_boolean_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_BOOLEAN_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_boolean_options *)options;

    mdata->boolean = opts->value;

    return 0;
}

static int
boolean_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct sol_constant_boolean *mdata = data;

    *packet = sol_flow_packet_new_boolean(mdata->boolean);
    SOL_NULL_CHECK(*packet, -ENOMEM);

    return 0;
}

static int
constant_string_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_constant_string *mdata = data;
    const struct sol_flow_node_type_constant_string_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_STRING_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_string_options *)options;

    mdata->string = strdup(opts->value);
    if (!mdata->string)
        return -errno;

    return 0;
}

static void
constant_string_close(struct sol_flow_node *node, void *data)
{
    struct sol_constant_string *mdata = data;

    free(mdata->string);
}

static int
string_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct sol_constant_string *mdata = data;

    *packet = sol_flow_packet_new_string(mdata->string);
    SOL_NULL_CHECK(*packet, -ENOMEM);

    return 0;
}

static int
empty_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    *packet = sol_flow_packet_new_empty();
    SOL_NULL_CHECK(*packet, -ENOMEM);
    return 0;
}

static int
constant_rgb_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_rgb *mdata = data;
    const struct sol_flow_node_type_constant_rgb_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_CONSTANT_RGB_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_constant_rgb_options *)options;

    *mdata = opts->value;

    return 0;
}

static int
rgb_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct sol_rgb *mdata = data;

    *packet = sol_flow_packet_new_rgb(mdata);
    SOL_NULL_CHECK(*packet, -ENOMEM);

    return 0;
}

#include "constant-gen.c"
