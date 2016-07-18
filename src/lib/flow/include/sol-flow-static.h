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

#include "sol-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup StaticFlow Static Flow
 * @ingroup Flow
 *
 * @brief Static flow is a parent node that creates its children nodes and
 * routes packets between them.
 *
 * It follows an specification received upon creation. The @c configuration
 * of the flow (connections made) will not change at runtime -- thus the name static.
 *
 * The specification consists in two arrays: one indicating each node
 * type, and the other indicating each connection as a sequence of
 * (source node, source port, destination node, destination port)
 * tuples.
 *
 * The connection specification is used to dispatch connections when
 * messages are sent. This approach provides a very low memory
 * overhead but still perfomant way to route the packets.
 *
 * @{
 */

/**
 * @brief Structure for the specification of a node.
 */
typedef struct sol_flow_static_node_spec {
    const struct sol_flow_node_type *type; /**< @brief Node type */
    const char *name; /**< @brief Name for the specific type's instance */
    const struct sol_flow_node_options *opts; /**< @brief Options for the specific type's instance */
} sol_flow_static_node_spec;

/**
 * @brief Structure for the specification of a connection.
 */
typedef struct sol_flow_static_conn_spec {
    uint16_t src; /**< @brief Source node index */
    uint16_t src_port; /**< @brief Source node port index */
    uint16_t dst; /**< @brief Destination node index */
    uint16_t dst_port; /**< @brief Destination node port index */
} sol_flow_static_conn_spec;

/**
 * @brief Structure for the specification of node ports
 */
typedef struct sol_flow_static_port_spec {
    uint16_t node; /**< @brief Node index */
    uint16_t port; /**< @brief Port index */
} sol_flow_static_port_spec;

/* Use these guards as the last element of the spec arrays. */

/**
 * @brief Guard element of the nodes spec array.
 */
#define SOL_FLOW_STATIC_NODE_SPEC_GUARD { }

/**
 * @brief Guard element of the connections spec array.
 */
#define SOL_FLOW_STATIC_CONN_SPEC_GUARD { .src = UINT16_MAX }

/**
 * @brief Guard element of the ports spec array.
 */
#define SOL_FLOW_STATIC_PORT_SPEC_GUARD { .node = UINT16_MAX }


/**
 * @brief Specification of how a static flow should work.
 *
 * @note Note that the arrays and functions provided are assumed to be available
 * and valid while the static flow type created from it is being used.
 */
typedef struct sol_flow_static_spec {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_STATIC_API_VERSION (1) /**< @brief Current API version number */
    uint16_t api_version; /**< @brief API version number */
#endif
    uint16_t flags;

    /**
     * @brief Array specifying the node types that are used by the static flow.
     *
     * It should terminate with a #SOL_FLOW_STATIC_NODE_SPEC_GUARD.
     */
    const struct sol_flow_static_node_spec *nodes;

    /**
     * @brief Array specifying the connections between nodes in the static flow.
     *
     * This array @b must be sorted by node index and port indexes. It should
     * terminate with a #SOL_FLOW_STATIC_CONN_SPEC_GUARD.
     */
    const struct sol_flow_static_conn_spec *conns;

    /**
     * @brief Array specifying which input ports from the flow are going to
     * be exported by the static flow.
     *
     * When static flow is used as a node in another flow, these will be its
     * input ports. It should terminate with a #SOL_FLOW_STATIC_PORT_SPEC_GUARD.
     */
    const struct sol_flow_static_port_spec *exported_in;

    /**
     * @brief Array specifying which output ports from the flow are going to
     * be exported by the static flow.
     *
     * When static flow is used as a node in another flow, these will be its
     * output ports. It should terminate with a #SOL_FLOW_STATIC_PORT_SPEC_GUARD.
     */
    const struct sol_flow_static_port_spec *exported_out;

    /**
     * @brief Function to allow the static flow control the options used to
     * create its nodes.
     *
     * It is called for each node every time a new flow is created from this type.
     */
    int (*child_opts_set)(const struct sol_flow_node_type *type,
        uint16_t child_index,
        const struct sol_flow_node_options *opts,
        struct sol_flow_node_options *child_opts);

    /**
     * @brief Function called after the static type is disposed.
     *
     * The user is passed the type_data pointer, that can be used to store
     * reference to extra resources to be disposed.
     */
    void (*dispose)(const void *type_data);
} sol_flow_static_spec;

/**
 * @brief Creates a new "static flow" node.
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
 * @brief Get a container node's children node by index
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
 * @brief Creates a new "static flow" (container) type.
 *
 * This allows one to create a static flow type "by hand" and fine-
 * tune it. Exported input/output ports may be declared, as well as
 * options forwarding.
 *
 * @param spec A specification of the type to be created. The data
 * inside the spec is assumed to still be valid until the type is
 * deleted.
 *
 * @return A new container node type on success, otherwise @c NULL.
 */
struct sol_flow_node_type *sol_flow_static_new_type(
    const struct sol_flow_static_spec *spec);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
