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

/**
 * @file custom-node-types.c
 *
 * This file contains the implementation of 3 custom node types. It
 * does not contain the full implementation in C, rather it leverages
 * soletta's tooling to generate boilerplate from JSON. Even this file
 * was initially created using a tool:
 *
 * To create the initial version (stub) file from JSON:
 *
 *     $ sol-flow-node-type-stub-gen.py \
 *             custom-node-types.c \
 *             custom-node-types-spec.json
 *
 * To create the dependencies from JSON:
 *
 *     $ sol-flow-node-type-gen.py \
 *            ${top_srcdir}/data/schemas/node-type.schema \
 *            custom-node-types-spec.json \
 *            custom-node-types-gen.h \
 *            custom-node-types-gen.c
 *
 * This file contains 3 custom node types:
 *  @li reader: a node with only output ports.
 *  @li writer: a node with only input ports.
 *  @li logic: a node with both input and output ports.
 */

#include <errno.h>
#include <stdio.h>

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-util.h>

/* This file is generated using sol-flow-node-type-gen.py, see above */
#include "custom-node-types-gen.h"

/* macro to check if option's sub_api is the one we expect */
#define SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, expected, ...)      \
    do {                                                                \
        SOL_NULL_CHECK(options, __VA_ARGS__);                            \
        if (((const struct sol_flow_node_options *)options)->sub_api != (expected)) { \
            SOL_WRN("" # options "(%p)->sub_api(%hu) != "                \
                "" # expected "(%hu)",                               \
                (options),                                           \
                ((const struct sol_flow_node_options *)options)->sub_api, \
                (expected));                                         \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)


/**
 * Reader:
 *
 * The reader is a node type that send an integer packet every
 * second. The initial value is defined as an option 'intopt'.
 *
 * The private data 'struct reader_data' contains the information we
 * need to store per instance, in this case the timer and the last
 * value.
 */

struct reader_data {
    struct sol_timeout *timer;
    int val;
};

static bool
reader_on_timeout(void *data)
{
    /* we get the last parameter we used in sol_timeout_add() */
    struct sol_flow_node *node = data;
    struct reader_data *mdata = sol_flow_node_get_private_data(node);
    int r;

    mdata->val++;

    /* create and send a new int packet on OUT port.
     *
     * Note that an 'int' port is actually an integer range or a
     * 'struct sol_irange', as it carries not only the value, but how
     * to interpret that integer such as minimum and maximum values
     * (so we can limit to only positive or negative, and smaller
     * precisions) as well how to increment or decrement the value
     * (steps).
     *
     * In this example we are only interested in the value, thus we
     * use the simpler packet sender.
     *
     * The port number macro (_CUSTOM_NODE_TYPES_READER__OUT__OUT) is
     * defined in the generated file custom-node-types-gen.h and is
     * based on the JSON array declaration.
     *
     * For efficiency matters soletta will only deal with port
     * indexes, the name is only used by node type description and
     * high-level API to resolve names to indexes.
     */
    r = sol_flow_send_irange_value_packet(node,
        _CUSTOM_NODE_TYPES_READER__OUT__OUT,
        mdata->val);
    if (r < 0) {
        fprintf(stderr, "ERROR: could not send packet on port=%d, value=%d\n",
            _CUSTOM_NODE_TYPES_READER__OUT__OUT, mdata->val);
        /* we will stop running and mdata->timer will be deleted if we
         * return false, then invalidate the handler.
         */
        mdata->timer = NULL;
        return false;  /* stop running */
    }

    return true; /* keep running */
}

/**
 * this constructor method is called when the node is created.
 *
 * The options are checked to see if it conforms to our api by using
 * the 'sub_api' field.
 *
 * The private data is guaranteed to be of size 'struct reader_data'.
 *
 * Never send packets from this function as the node is still being
 * created and there are no connections.
 */
static int
reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct reader_data *mdata = data;
    const struct _custom_node_types_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        _CUSTOM_NODE_TYPES_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct _custom_node_types_reader_options *)options;

    /* create a 1 second timer where we produce packets. */
    mdata->timer = sol_timeout_add(1000, reader_on_timeout, node);

    /* the initial value comes from options. */
    mdata->val = opts->intopt.val;

    return 0;
}

/**
 * This destructor method is called when the node is finished.
 *
 * When this method returns the memory pointed by 'data' is released
 * and should stop being referenced.
 */
static void
reader_close(struct sol_flow_node *node, void *data)
{
    struct reader_data *mdata = data;

    /* stop the timer */
    sol_timeout_del(mdata->timer);
}

/**
 * This method is called when the port is connected. We use it to
 * deliver an initial packet.
 *
 * Never send packets from connect method as the connection is still
 * not established. Instead return it in @a packet.
 *
 * This method is usually not needed, but we provide it here to
 * clarify the initial packet delivery.
 */
static int
reader_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, struct sol_flow_packet **packet)
{
    struct reader_data *mdata = data;

    /* on connect, send the current value as the initial packet.
     *
     * Note that an 'int' port is actually an integer range or a
     * 'struct sol_irange', as it carries not only the value, but how
     * to interpret that integer such as minimum and maximum values
     * (so we can limit to only positive or negative, and smaller
     * precisions) as well how to increment or decrement the value
     * (steps).
     *
     * In this example we are only interested in the value, thus we
     * use the simpler packet constructor.
     */
    *packet = sol_flow_packet_new_irange_value(mdata->val);

    return 0;
}


/**
 * Writer:
 *
 * The writer is a node type that receives a boolean packet and prints
 * to stdout.
 *
 * The private data 'struct writer_data' contains the information we
 * need to store per instance, in this case it is the prefix we
 * received as an option.
 */

struct writer_data {
    char *prefix;
};

static int
writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct writer_data *mdata = data;
    const struct _custom_node_types_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        _CUSTOM_NODE_TYPES_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct _custom_node_types_writer_options *)options;

    if (opts->prefix)
        mdata->prefix = strdup(opts->prefix);

    return 0;
}

static void
writer_close(struct sol_flow_node *node, void *data)
{
    struct writer_data *mdata = data;

    free(mdata->prefix);
}

static int
writer_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct writer_data *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    printf("%s=%s\n",
        mdata->prefix ? mdata->prefix : "writer",
        in_value ? "true" : "false");

    return 0;
}

/**
 * Logic:
 *
 * The logic is a node type simulates business logic, it will receive
 * some information and deliver another. Here the logic is pretty
 * simple, we receive an integer and deliver a boolean packet as true
 * if the integer is even or false if it is odd.
 *
 * this node contains no data, it will recompute everything based on
 * the last received packet, thus there is no node private data, open
 * or close methods.
 */

static int
logic_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_boolean_packet(node,
        _CUSTOM_NODE_TYPES_LOGIC__IN__IN,
        in_value.val % 2 == 0);

    return 0;
}

/* This file is generated using sol-flow-node-type-gen.py, see above */
#include "custom-node-types-gen.c"
