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

#pragma once

#include "sol-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Static flow is a parent node that creates its children nodes and
 * routes packets between them according to an specification received
 * upon creation. The 'configuration' of the flow (connections made)
 * will not change at runtime -- thus the name static).
 *
 * The specification consists in two arrays: one indicating each node
 * type, and the other indicating each connection as a sequence of
 * (source node, source port, destination node, destionation port)
 * tuples.
 *
 * The connection specification is used to dispatch connections when
 * messages are sent. This approach provides a very low memory
 * overhead but still perfomant way to route the packets.
 */

struct sol_flow_static_node_spec {
    const struct sol_flow_node_type *type; /**< node type */
    const char *name; /**< name for the specitic type's instance */
    const struct sol_flow_node_options *opts; /**< options for the specitic type's instance */
};

struct sol_flow_static_conn_spec {
    uint16_t src; /**< source node index */
    uint16_t src_port; /**< source node port index */
    uint16_t dst; /**< destination node index */
    uint16_t dst_port; /**< destination node port index */
};

struct sol_flow_static_port_spec {
    uint16_t node; /**< node index */
    uint16_t port; /**< port index */
};

/* Use the guards as the last element of the spec arrays. */
#define SOL_FLOW_STATIC_NODE_SPEC_GUARD { }
#define SOL_FLOW_STATIC_CONN_SPEC_GUARD { .src = UINT16_MAX }
#define SOL_FLOW_STATIC_PORT_SPEC_GUARD { .node = UINT16_MAX }

/**
 * Creates a new "static flow" node.
 *
 * Nodes should be created in the sol_main_callbacks::startup
 * function, and at least the root one must be a "static flow" node.
 * This function will create one of those "by hand", if all the child
 * nodes and connections are provided.
 *
 * @param parent The parent node. Pass @c NULL if you're creating the
 *               root node of the flow.
 * @param nodes A #SOL_FLOW_STATIC_PORT_SPEC_GUARD terminated array of
 *              node specification structs
 * @param conns A #SOL_FLOW_STATIC_CONN_SPEC_GUARD terminated array of
 *              port connections of the nodes declared in @a nodes.
 *              This array @b must be sorted by node index and port
 *              indexes.
 *
 * For a higher level construct to get your root static flow node,
 * consider using sol_flow_builder_new() family of functions.
 *
 * @see sol_flow_new(), sol_flow_builder_get_node_type() and
 * sol_flow_static_new_type().
 *
 * @return A new node instance on success, otherwise @c NULL.
 */
struct sol_flow_node *sol_flow_static_new(struct sol_flow_node *parent, const struct sol_flow_static_node_spec nodes[], const struct sol_flow_static_conn_spec conns[]);

/**
 * Get a container node's children node by index
 *
 * @param node The node to get the child node from
 * @param index The index of the child node in @a node (position in
 *              the #sol_flow_static_node_spec array)
 *
 * @return The child node of @a node with the given index or @c NULL,
 *         on errors.
 */
struct sol_flow_node *sol_flow_static_get_node(struct sol_flow_node *node, uint16_t index);

/**
 * Creates a new "static flow" (container) type.
 *
 * This allows one to create a static flow type "by hand" and fine-
 * tune it. Exported input/output ports may be declared, as well as
 * options forwarding.
 *
 * @param nodes A #SOL_FLOW_STATIC_PORT_SPEC_GUARD terminated array of
 *              node specification structs
 * @param conns A #SOL_FLOW_STATIC_CONN_SPEC_GUARD terminated array of
 *              port connections of the nodes declared in @a nodes.
 *              This array @b must be sorted by node index and port
 *              indexes.
 * @param exported_in A #SOL_FLOW_STATIC_PORT_SPEC_GUARD terminated
 *                    array of port specification structs that will
 *                    map child node input ports to the container's
 *                    own input ports
 * @param exported_out A #SOL_FLOW_STATIC_PORT_SPEC_GUARD terminated
 *                     array of port specification structs that will
 *                     map child node output ports to the container's
 *                     own output ports
 * @param child_opts_set member function to forward option members of
 *                       the container node to child nodes
 *
 * @return A new container node type on success, otherwise @c NULL.
 */
struct sol_flow_node_type *sol_flow_static_new_type(
    const struct sol_flow_static_node_spec nodes[],
    const struct sol_flow_static_conn_spec conns[],
    const struct sol_flow_static_port_spec exported_in[],
    const struct sol_flow_static_port_spec exported_out[],
    int (*child_opts_set)(const struct sol_flow_node_type *type,
    uint16_t child_index,
    const struct sol_flow_node_options *opts,
    struct sol_flow_node_options *child_opts));

void sol_flow_static_del_type(struct sol_flow_node_type *type);

#ifdef __cplusplus
}
#endif
