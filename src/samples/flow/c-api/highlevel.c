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

#include "sol-flow-builder.h"
#include "custom-node-types-gen.h"
#include "sol-mainloop.h"

/**
 * @file highlevel.c
 *
 * Example of soletta's flow in C using high-level API. It will
 * manipulate the nodes and connections using easy to use and human
 * readable strings.
 *
 * There is a runtime penalty as this requires node type descriptions
 * to be available, consuming disk and memory space, thus it is not
 * recommended for very constrained systems.
 *
 * See lowlevel.c on how to use the lowlevel API that does not rely on
 * node type descriptions.
 */

static struct sol_flow_node *flow;
static struct sol_flow_builder *builder;

static void
startup(void)
{
    struct sol_flow_node_type *flow_node_type;
    struct _custom_node_types_reader_options reader_opts =
        _CUSTOM_NODE_TYPES_READER_OPTIONS_DEFAULTS(
            .intopt.val = 1
            );
    struct _custom_node_types_writer_options writer_opts =
        _CUSTOM_NODE_TYPES_WRITER_OPTIONS_DEFAULTS(
            .prefix = "writer prefix from options"
            );

    builder = sol_flow_builder_new();

    /* use our custom node types */
    sol_flow_builder_add_node(builder, "reader",
        _CUSTOM_NODE_TYPES_READER,
        &reader_opts.base);
    sol_flow_builder_add_node(builder, "logic",
        _CUSTOM_NODE_TYPES_LOGIC,
        NULL);
    sol_flow_builder_add_node(builder, "writer",
        _CUSTOM_NODE_TYPES_WRITER,
        &writer_opts.base);
    sol_flow_builder_connect(builder, "reader", "OUT", "logic", "IN");
    sol_flow_builder_connect(builder, "logic", "OUT", "writer", "IN");

    /* Also output to console using soletta's console node type.  If
     * console is builtin libsoletta, it is used, otherwise a module
     * console.so is looked up and if exists will be added. If nothing
     * can be found (ie: module is disabled) it will keep going as we
     * are not checking the return value of
     * sol_flow_builder_add_node().
     */
    sol_flow_builder_add_node_by_type(builder, "console", "console", NULL);
    sol_flow_builder_connect(builder, "reader", "OUT", "console", "IN");
    sol_flow_builder_connect(builder, "logic", "OUT", "console", "IN");

    /* this creates a static flow using the low-level API that will
     * actually run the flow. Note that its memory is bound to
     * builder, then keep builder alive.
     */
    flow_node_type = sol_flow_builder_get_node_type(builder);

    /* create and run the flow */
    flow = sol_flow_node_new(NULL, "highlevel", flow_node_type, NULL);
}

static void
shutdown(void)
{
    /* stop the flow, disconnect ports and close children nodes */
    sol_flow_node_del(flow);
    /* free the builder and its internal node type */
    sol_flow_builder_del(builder);
}

SOL_MAIN_DEFAULT(startup, shutdown);
