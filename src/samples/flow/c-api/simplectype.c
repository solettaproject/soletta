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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "sol-flow-builder.h"
#include "sol-flow-simplectype.h"
#include "sol-log.h"
#include "sol-mainloop.h"

/**
 * @file simplectype.c
 *
 * Example how to create and use a simple C node type and the
 * high-level API. To understand how to use the high-level C API with
 * existing or custom C types using the generator from JSON
 * (recommended), take a look at @ref highlevel.c
 *
 * Note that this sample's 'mytype*' uses all features of simplectype,
 * usually some options will not be used in most applications, such as
 * port connections and disconnection events or context. One example
 * of the simplistic version is the 'isodd' that checks if if the
 * given number is odd or even.
 */

static struct sol_flow_node *flow;
static struct sol_flow_builder *builder;
static struct sol_flow_node_type *flow_node_type;
static struct sol_flow_node_type *isoddtype;
static struct sol_flow_node_type *mytype;

/*
 * isodd is a very simplistic type, it only handle a single event and
 * has no storage/context. All it does is get an integer and send a
 * boolean true if that number is odd, sending false if it's even.
 */
static int
isodd(struct sol_flow_node *node, const struct sol_flow_simplectype_event *ev, void *data)
{
    int32_t val;
    int r;

    /* we only handle events for port input. */
    if (ev->type != SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_PROCESS)
        return 0;

    /* get the integer value from irange and check if it worked */
    r = sol_flow_packet_get_irange_value(ev->packet, &val);
    if (r < 0)
        return r;

    /* we use port index '0' here, after all we have a single port */
    return sol_flow_send_boolean_packet(node, 0, (val % 2 != 0));
}

/*
 * mytype is an extensive example of simplectype capabilities.
 *
 * It will take options at node open, keep context and handle all
 * events.
 *
 * It stores an integer and a boolean, initially set throught options
 * and then modified via input ports, then from time to time (every
 * 500ms) it will create a string with both values and send on its
 * output port.
 *
 */
struct mytype_options {
#define MYTYPE_OPTIONS_SUB_API 0x1234
    struct sol_flow_node_options base;
    int someint;
    bool somebool;
};

struct mytype_context {
    struct sol_timeout *timer;
    int someint;
    bool somebool;
};

static bool
on_timeout(void *data)
{
    struct sol_flow_node *node = data;
    struct mytype_context *ctx = sol_flow_node_get_private_data(node);
    uint16_t port_idx;
    char buf[256];

    printf("mytype tick... send packet. ctx=%p someint=%d, somebool=%d\n",
        ctx, ctx->someint, ctx->somebool);

    snprintf(buf, sizeof(buf), "%d/%s",
        ctx->someint,
        ctx->somebool ? "true" : "false");

    /* this is to demo the discovery from name, but one could/should use the
     * port index for efficiency matters.
     */
    port_idx = sol_flow_simplectype_get_port_out_index(mytype, "STRING");
    sol_flow_send_string_packet(node, port_idx, buf);

    return true;
}

static int
mytype_func(struct sol_flow_node *node, const struct sol_flow_simplectype_event *ev, void *data)
{
    struct mytype_context *ctx = data;

    switch (ev->type) {
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_OPEN: {
        if (ev->options && ev->options->sub_api == MYTYPE_OPTIONS_SUB_API) {
            struct mytype_options *opt = (struct mytype_options *)ev->options;
            ctx->someint = opt->someint;
            ctx->somebool = opt->somebool;
        }
        /* every 500ms send out a string representing our someint + somebool */
        ctx->timer = sol_timeout_add(500, on_timeout, node);
        if (!ctx->timer)
            return -ENOMEM;
        printf("simplectype opened ctx=%p, someint=%d, somebool=%d\n",
            ctx, ctx->someint, ctx->somebool);
        return 0;
    }
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_CLOSE: {
        printf("simplectype closed ctx=%p\n", ctx);
        sol_timeout_del(ctx->timer);
        return 0;
    }
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_PROCESS: {
        /* this is to show the port names, ideally one would keep the
         * indexes and use them here, doing integer comparisons
         * instead of strcmp()
         */
        if (strcmp(ev->port_name, "IRANGE") == 0) {
            int32_t val;
            if (sol_flow_packet_get_irange_value(ev->packet, &val) == 0) {
                printf("simplectype updated integer from %d to %d\n",
                    ctx->someint, val);
                ctx->someint = val;
                return 0;
            }
        } else if (strcmp(ev->port_name, "BOOLEAN") == 0) {
            bool val;
            if (sol_flow_packet_get_boolean(ev->packet, &val) == 0) {
                printf("simplectype updated boolean from %d to %d\n",
                    ctx->somebool, val);
                ctx->somebool = val;
                return 0;
            }
        }
        printf("simplectype port '%s' got unexpected data!\n", ev->port_name);
        return -EINVAL;
    }
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_CONNECT:
        printf("simplectype port IN '%s' id=%d conn=%d connected ctx=%p\n",
            ev->port_name, ev->port, ev->conn_id, ctx);
        return 0;
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_DISCONNECT:
        printf("simplectype port IN '%s' id=%d conn=%d disconnected ctx=%p\n",
            ev->port_name, ev->port, ev->conn_id, ctx);
        return 0;
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_OUT_CONNECT:
        printf("simplectype port OUT '%s' id=%d conn=%d connected ctx=%p\n",
            ev->port_name, ev->port, ev->conn_id, ctx);
        return 0;
    case SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_OUT_DISCONNECT:
        printf("simplectype port OUT '%s' id=%d conn=%d disconnected ctx=%p\n",
            ev->port_name, ev->port, ev->conn_id, ctx);
        return 0;
    }

    return -EINVAL;
}

static void
startup(void)
{
    /* you can give your simplectype custom arguments, just give it
     * the struct and remember to fill its "base" with the API fields.
     * the 'api_version' is checked by sol-flow calls, while sub_api
     * is checked at mytype_func when handling the
     * SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_OPEN.
     */
    struct mytype_options mystuff_opts = {
        .base = {
            .api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION,
            .sub_api = MYTYPE_OPTIONS_SUB_API,
        },
        .someint = 12,
        .somebool = true,
    };

    builder = sol_flow_builder_new();

    /* declare 'isodd' without private data and with ports:
     * input: IN (index: 0)
     * output: OUT (index: 0)
     */
    isoddtype = sol_flow_simplectype_new_nocontext(
        isodd,
        SOL_FLOW_SIMPLECTYPE_PORT_IN("IN", SOL_FLOW_PACKET_TYPE_IRANGE),
        SOL_FLOW_SIMPLECTYPE_PORT_OUT("OUT", SOL_FLOW_PACKET_TYPE_BOOLEAN),
        NULL);

    /* declare mytype with 'struct mytype_context' private data and with ports:
     * input: IRANGE (index: 0), BOOLEAN (index: 1)
     * output: STRING (index: 0, as input and output have separate arrays)
     */
    mytype = sol_flow_simplectype_new_full(
        "mytype", sizeof(struct mytype_context), sizeof(struct mytype_options),
        mytype_func,
        SOL_FLOW_SIMPLECTYPE_PORT_IN("IRANGE", SOL_FLOW_PACKET_TYPE_IRANGE),
        SOL_FLOW_SIMPLECTYPE_PORT_IN("BOOLEAN", SOL_FLOW_PACKET_TYPE_BOOLEAN),
        SOL_FLOW_SIMPLECTYPE_PORT_OUT("STRING", SOL_FLOW_PACKET_TYPE_STRING),
        NULL);

    /* for types declared as builtin or external modules, add by type name */
    sol_flow_builder_add_node_by_type(builder, "timer",
        "timer", NULL);
    sol_flow_builder_add_node_by_type(builder, "booltoggle",
        "boolean/toggle", NULL);
    sol_flow_builder_add_node_by_type(builder, "intacc",
        "int/accumulator", NULL);
    sol_flow_builder_add_node_by_type(builder, "debug",
        "console", NULL);
    sol_flow_builder_add_node_by_type(builder, "console_mystuff",
        "console", NULL);
    sol_flow_builder_add_node_by_type(builder, "console_isodd",
        "console", NULL);

    /* use our types as we'd use any custom type: given its handle */
    sol_flow_builder_add_node(builder, "isodd", isoddtype, NULL);
    sol_flow_builder_add_node(builder, "mystuff", mytype, &mystuff_opts.base);

    /* setup connections */
    sol_flow_builder_connect(builder, "timer", "OUT", -1,
        "booltoggle", "IN", -1);
    sol_flow_builder_connect(builder, "timer", "OUT", -1,
        "intacc", "INC", -1);

    /* intacc OUT -> IN isodd OUT -> IN console_isodd */
    sol_flow_builder_connect(builder, "intacc", "OUT", -1,
        "isodd", "IN", -1);
    sol_flow_builder_connect(builder, "isodd", "OUT", -1,
        "console_isodd", "IN", -1);

    /* booltoggle OUT -> BOOLEAN mystuff
     * intacc OUT -> IRANGE mystuff
     * mystuff STRING -> IN console_mystuff
     */
    sol_flow_builder_connect(builder, "booltoggle", "OUT", -1,
        "mystuff", "BOOLEAN", -1);
    sol_flow_builder_connect(builder, "intacc", "OUT", -1,
        "mystuff", "IRANGE", -1);
    sol_flow_builder_connect(builder, "mystuff", "STRING", -1,
        "console_mystuff", "IN", -1);

    /* also print out values from boolean toggle and integer
     * accumulator so we can double check the results.
     */
    sol_flow_builder_connect(builder, "booltoggle", "OUT", -1,
        "debug", "IN", -1);
    sol_flow_builder_connect(builder, "intacc", "OUT", -1,
        "debug", "IN", -1);

    /* this creates a static flow using the low-level API that will
     * actually run the flow. Note that its memory is bound to
     * builder, then keep builder alive.
     */
    flow_node_type = sol_flow_builder_get_node_type(builder);

    /* create and run the flow */
    flow = sol_flow_node_new(NULL, "simplectype", flow_node_type, NULL);
}

static void
shutdown(void)
{
    /* stop the flow, disconnect ports and close children nodes */
    sol_flow_node_del(flow);

    /* delete types */
    sol_flow_node_type_del(isoddtype);
    sol_flow_node_type_del(mytype);
    sol_flow_node_type_del(flow_node_type);

    /* delete the builder */
    sol_flow_builder_del(builder);
}

SOL_MAIN_DEFAULT(startup, shutdown);
