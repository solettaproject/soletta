/*
 * This file is part of the Soletta™ Project
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

#include <stdlib.h>

/* we include custom-node-types-gen.h but are not building a module,
 * disregard log_init */
#include "sol-macros.h"

#include "soletta.h"
#include "sol-flow-static.h"
#include "custom-node-types-gen.h"
#include "sol-flow/console.h"

/**
 * @file lowlevel.c
 *
 * Example of soletta's flow in C using low-level API. It will
 * manipulate the nodes and connections by indexes in space-efficient
 * lookup matrices.
 *
 * It's not as nice to use, matrices are looked up in place and thus
 * must be in correct ascending order.
 *
 * This is the most efficient way to use soletta, but it's not user
 * friendly. To solve this we recommend one to write FBP files and use
 * da-fbp-generator to create the efficient lowlevel flow of it.
 *
 * See highlevel.c on how to use the lowlevel API that does not rely on
 * node type descriptions.
 */

static const struct sol_flow_node_type_custom_node_types_reader_options reader_opts =
    SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_READER_OPTIONS_DEFAULTS(
    .intopt = 1
    );
static const struct sol_flow_node_type_custom_node_types_writer_options writer_opts =
    SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_WRITER_OPTIONS_DEFAULTS(
    .prefix = "writer prefix from options"
    );

/* This array defines the nodes we will use in our flow. It is space
 * efficient and will not be duplicated, a reference of it is kept by
 * the static flow.
 *
 * However, we can't initialize the types in the global declaration as
 * they are defined in other libraries and will be relocated during
 * runtime (they are not compile time constants).
 *
 * The console block is ifdef'ed as it may not be compiled in
 * libsoletta, in that case we cannot access the
 * SOL_FLOW_NODE_TYPE_CONSOLE symbol. It is available as an external
 * module, then one have to manually dlopen() and dlsym() the symbol
 * in the startup() function below.
 */
static struct sol_flow_static_node_spec nodes[] = {
    [0] = { NULL /* placeholder SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_READER */,
            "reader", &reader_opts.base },
    [1] = { NULL /* placeholder SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_LOGIC */,
            "logic", NULL },
    [2] = { NULL /* placeholder SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_WRITER */,
            "writer", &writer_opts.base },
    [3] = { NULL /* placeholder SOL_FLOW_NODE_TYPE_CONSOLE */,
            "console", NULL },
    SOL_FLOW_STATIC_NODE_SPEC_GUARD
};

/* This array defines the connections between nodes in our flow. It is
 * space efficient and will not be duplicated, a reference of it is
 * kept by the static flow.
 *
 * However this array must be sorted by node index and port indexes as
 * it is used in searches. While it's verified during runtime, it is
 * cumbersome to maintain as seen below, we can't isolate the console
 * connections in a single ifdef as it would break the order.
 */
static const struct sol_flow_static_conn_spec conns[] = {
    { 0 /* reader */, SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_READER__OUT__OUT,
      1 /* logic */, SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_LOGIC__IN__IN },
    { 0 /* reader */, SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_READER__OUT__OUT,
      3 /* console */, SOL_FLOW_NODE_TYPE_CONSOLE__IN__IN },
    { 1 /* logic */, SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_LOGIC__OUT__OUT,
      2 /* writer */, SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_WRITER__IN__IN },
    { 1 /* logic */, SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_LOGIC__OUT__OUT,
      3 /* console */, SOL_FLOW_NODE_TYPE_CONSOLE__IN__IN },
    SOL_FLOW_STATIC_CONN_SPEC_GUARD
};

static struct sol_flow_node *flow;

static void
startup(void)
{
    const struct sol_flow_node_type **console_type;

    if (sol_flow_get_node_type("console", SOL_FLOW_NODE_TYPE_CONSOLE, &console_type) < 0)
        return;

    /*
     * Since these symbols will be relocated in runtime, we can't
     * initialize them in the vector initialization, we must assign
     * them in runtime.
     */
    nodes[0 /* reader */].type = SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_READER;
    nodes[1 /* logic */].type = SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_LOGIC;
    nodes[2 /* writer */].type = SOL_FLOW_NODE_TYPE_CUSTOM_NODE_TYPES_WRITER;
    nodes[3 /* console */].type = *console_type;

    flow = sol_flow_static_new(NULL, nodes, conns);
}

static void
shutdown(void)
{
    /* stop the flow, disconnect ports and close children nodes */
    sol_flow_node_del(flow);
}

SOL_MAIN_DEFAULT(startup, shutdown);
