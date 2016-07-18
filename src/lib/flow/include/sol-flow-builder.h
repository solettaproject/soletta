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

#include <stdbool.h>

#include "sol-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used by Soletta Flow Builder.
 */

/**
 * @defgroup FlowBuilder Flow Builder
 * @ingroup Flow
 *
 * @brief Flow-builder is a helper component that creates nodes
 * and connections specs with a friendly API.
 *
 * Instead of declaring a node spec structure and an ordered
 * connection spec structure directly, using node and port indexes,
 * builder can be used to add nodes and connections in a programmatic way,
 * using nodes and ports names.
 *
 * This approach should led to a code easier to read and maintain, but
 * it would consume a bit more memory and processing when compared with
 * the static version.
 *
 * @{
 */

/**
 * @typedef sol_flow_builder
 *
 * @brief Builder's handle.
 */
struct sol_flow_builder;
typedef struct sol_flow_builder sol_flow_builder;

/**
 * @brief Creates a new instance of a @ref sol_flow_builder.
 *
 * It should be deleted with @ref sol_flow_builder_del() only
 * after the flow stops running.
 *
 * @return A new Builder's instance, @c NULL on error.
 */
struct sol_flow_builder *sol_flow_builder_new(void);

/**
 * @brief Destroy a @ref sol_flow_builder instance.
 *
 * @param builder Builder to be destroyed
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_builder_del(struct sol_flow_builder *builder);

/**
 * @brief Set the @ref FlowResolver to be used by the builder's resulting flow.
 *
 * @param builder The Builder
 * @param resolver Resolver to be used
 *
 * @note Passing @c NULL to @a resolver will set the default resolver.
 */
void sol_flow_builder_set_resolver(struct sol_flow_builder *builder, const struct sol_flow_resolver *resolver);

/**
 * @brief Set the type description to be used by the @a builder.
 *
 * Input/output ports and options descriptions are automatically set.
 * The strings passed as arguments will be copied.
 *
 * @param builder The Builder
 * @param name Node type name
 * @param category Node type category
 * @param description Node type description
 * @param author Author's name
 * @param url Node type url
 * @param license License text
 * @param version Node type version
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_builder_set_type_description(struct sol_flow_builder *builder, const char *name, const char *category, const char *description, const char *author, const char *url, const char *license, const char *version);

/**
 * @brief Add a node to the nodes spec of the resulting flow.
 *
 * @warning Node names can't be @c NULL and @b must be unique within a flow.
 *
 * @param builder The Builder
 * @param name Node name
 * @param type Node type
 * @param option Node options
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @see sol_flow_builder_add_node_by_type
 */
int sol_flow_builder_add_node(struct sol_flow_builder *builder, const char *name, const struct sol_flow_node_type *type, const struct sol_flow_node_options *option);

/**
 * @brief Add a node (via its type's name) to the nodes spec of the resulting flow.
 *
 * @ref FlowResolver will be used to get the node type and options
 * base on the type's name provided.
 *
 * @warning Node names can't be @c NULL and @b must be unique within a flow.
 *
 * @param builder The Builder
 * @param name Node name
 * @param type_name Name of the node type
 * @param options_strv Node options in a string "list" format
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @see sol_flow_builder_add_node
 */
int sol_flow_builder_add_node_by_type(struct sol_flow_builder *builder, const char *name, const char *type_name, const char *const *options_strv);

/**
 * @brief Add a connection (via port names) to the conn spec of the resulting flow.
 *
 * @warning The nodes used in the connection must have been previously
 * added by @ref sol_flow_builder_add_node. So the names can be found
 * by this function.
 *
 * Port names must be as declared in the description structure of the node types.
 * If no description was declared, @ref sol_flow_builder_connect_by_index() should be used instead
 * of this function.
 *
 * @param builder The Builder
 * @param src_name Name of the source node
 * @param src_port_name Port's name in the source node
 * @param src_port_idx Port's index in the source node
 * @param dst_name Name of the destination node
 * @param dst_port_name Port's name in the destination node
 * @param dst_port_idx Port's index in the destination node
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @see sol_flow_builder_connect_by_index
 */
int sol_flow_builder_connect(struct sol_flow_builder *builder, const char *src_name, const char *src_port_name, int src_port_idx, const char *dst_name, const char *dst_port_name, int dst_port_idx);

/**
 * @brief Add a connection to the conn spec of the resulting flow.
 *
 * @warning The nodes used in the connection must have been previously
 * added by @ref sol_flow_builder_add_node. So the names can be found
 * by this function.
 *
 * @param builder The Builder
 * @param src_name Name of the source node
 * @param src_port_index Port's index in the source node
 * @param dst_name Name of the destination node
 * @param dst_port_index Port's index in the destination node
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @see sol_flow_builder_add_node_by_type
 */
int sol_flow_builder_connect_by_index(struct sol_flow_builder *builder, const char *src_name, uint16_t src_port_index, const char *dst_name, uint16_t dst_port_index);

/**
 * @brief Exports an input port of a node using @a exported_name as identifier.
 *
 * @warning The node must have been previously added by
 * @ref sol_flow_builder_add_node before this function is used.
 *
 * @param builder The Builder
 * @param node_name Node's name
 * @param port_name Port's name
 * @param port_idx Port's index
 * @param exported_name Name used to identify the exported port
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @see sol_flow_builder_export_port_out
 */
int sol_flow_builder_export_port_in(struct sol_flow_builder *builder, const char *node_name, const char *port_name, int port_idx, const char *exported_name);

/**
 * @brief Exports an output port of a node using @a exported_name as identifier.
 *
 * @warning The node must have been previously added by
 * @ref sol_flow_builder_add_node before this function is used.
 *
 * @param builder The Builder
 * @param node_name Node's name
 * @param port_name Port's name
 * @param port_idx Port's index
 * @param exported_name Name used to identify the exported port
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @see sol_flow_builder_export_port_in
 */
int sol_flow_builder_export_port_out(struct sol_flow_builder *builder, const char *node_name, const char *port_name, int port_idx, const char *exported_name);

/**
 * @brief Exports an given option of a node using @c exported_name as identifier.
 *
 * @warning The node must have been previously added by
 * @ref sol_flow_builder_add_node before this function is used.
 *
 * @param builder The Builder
 * @param node_name Node's name
 * @param option_name Option's name
 * @param exported_name Name used to identify the exported option
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_builder_export_option(struct sol_flow_builder *builder, const char *node_name, const char *option_name, const char *exported_name);

/**
 * @brief Returns the node type generated by the builder.
 *
 * It should be used to create nodes with @ref sol_flow_node_new().
 * After the type is created, no more nodes or connections can be added.
 *
 * This node type must be deleted using @ref sol_flow_node_type_del().
 *
 * @param builder The Builder
 *
 * @return The generated node type on success, @c NULL otherwise
 */
struct sol_flow_node_type *sol_flow_builder_get_node_type(struct sol_flow_builder *builder);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
