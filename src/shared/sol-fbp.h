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

#pragma once

#include "sol-arena.h"
#include "sol-macros.h"
#include "sol-str-slice.h"
#include "sol-vector.h"

struct sol_fbp_position {
    unsigned int line, column;
};

struct sol_fbp_port {
    struct sol_fbp_position position;

    struct sol_str_slice name;
};

struct sol_fbp_meta {
    struct sol_fbp_position position;

    struct sol_str_slice key;
    struct sol_str_slice value;
};

struct sol_fbp_node {
    struct sol_fbp_position position;

    struct sol_str_slice name;
    struct sol_str_slice component;

    struct sol_vector meta;
    struct sol_vector in_ports;
    struct sol_vector out_ports;

    /* To be used by client code. Allows associating client-specific
     * data with a given node. */
    void *user_data;
};

struct sol_fbp_conn {
    struct sol_fbp_position position;

    int src, dst;
    struct sol_str_slice src_port, dst_port;
    int src_port_idx, dst_port_idx;
};

struct sol_fbp_exported_port {
    struct sol_fbp_position position;

    int node;
    int port_idx;
    struct sol_str_slice port, exported_name;
};

struct sol_fbp_declaration {
    struct sol_str_slice name;
    struct sol_str_slice metatype;
    struct sol_str_slice contents;

    struct sol_fbp_position position;
};

struct sol_fbp_option {
    struct sol_str_slice name;
    struct sol_str_slice node_option;
    int node;

    struct sol_fbp_position position;
};

struct sol_fbp_graph {
    struct sol_vector nodes;
    struct sol_vector conns;

    struct sol_vector exported_in_ports;
    struct sol_vector exported_out_ports;
    struct sol_vector declarations;
    struct sol_vector options;

    struct sol_arena *arena;
};

struct sol_fbp_error {
    struct sol_fbp_position position;
    char *msg;
};

int sol_fbp_graph_init(struct sol_fbp_graph *g);
int sol_fbp_graph_fini(struct sol_fbp_graph *g);

/* Add a node to the graph if needed. Component parameter should be
 * added only once, so the function will fail with -EEXIST return. */
int sol_fbp_graph_add_node(struct sol_fbp_graph *g, struct sol_str_slice name,
    struct sol_str_slice component, struct sol_fbp_position position,
    struct sol_fbp_node **out_node);

/* Add will succeed even if the port already exists, the graph just
 * keep track of all ports each node must have. */
int sol_fbp_graph_add_in_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice name, struct sol_fbp_position position);
int sol_fbp_graph_add_out_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice name, struct sol_fbp_position position);

int sol_fbp_graph_add_meta(struct sol_fbp_graph *g,
    int node, struct sol_str_slice key, struct sol_str_slice value, struct sol_fbp_position position);

/* May return -EEXIST to indicate duplicate entries. */

int sol_fbp_graph_add_conn(struct sol_fbp_graph *g,
    int src, struct sol_str_slice src_port, int src_port_idx,
    int dst, struct sol_str_slice dst_port, int dst_port_idx,
    struct sol_fbp_position position);

/* Return -EEXIST if exported port with same name exists, and
 * -EADDRINUSE if the same node/port is already exported. */
int sol_fbp_graph_add_exported_in_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice port, int port_idx, struct sol_str_slice exported_name,
    struct sol_fbp_position position, struct sol_fbp_exported_port **out_ep);

/* Return -EEXIST if exported port with same name exists, and
 * -EADDRINUSE if the same node/port is already exported. */
int sol_fbp_graph_add_exported_out_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice port, int port_idx, struct sol_str_slice exported_name,
    struct sol_fbp_position position, struct sol_fbp_exported_port **out_ep);

int sol_fbp_graph_declare(struct sol_fbp_graph *g,
    struct sol_str_slice name, struct sol_str_slice metatype, struct sol_str_slice contents, struct sol_fbp_position);

int sol_fbp_graph_option(struct sol_fbp_graph *g,
    int node, struct sol_str_slice name, struct sol_str_slice node_opt, struct sol_fbp_position position);

/* Given an input string written using the "FBP file format" described
 * in https://github.com/noflo/fbp/blob/master/README.md, returns a
 * graph of it. See also README.fbp. */
struct sol_fbp_error *sol_fbp_parse(struct sol_str_slice input, struct sol_fbp_graph *g);

/* Print out a message of a given FBP file. */
void sol_fbp_log_print(const char *file, unsigned int line, unsigned int column, const char *format, ...) SOL_ATTR_PRINTF(4, 5);
void sol_fbp_error_free(struct sol_fbp_error *e);
