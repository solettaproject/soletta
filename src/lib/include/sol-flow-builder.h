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

#include <stdbool.h>

#include "sol-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* flow-builder is a helper component that can be used to
 * create nodes and connections specs with a friendly API.
 * So instead of declaring a node spec structure and an ordered
 * connection spec structure directly, using node and port indexes,
 * builder can be used to add nodes and connections in a programmatic way,
 * using nodes and ports names.
 * This approach should led to a code easier to read and maintain, but
 * it would consume a bit more memory and processing.
 */
struct sol_flow_builder;

/* Returns NULL on error. Should be deleted with
 * sol_flow_builder_del() only after flow stops running. */
struct sol_flow_builder *sol_flow_builder_new(void);
int sol_flow_builder_del(struct sol_flow_builder *builder);

/* Set resolver to use instead of the default resolver. Setting to NULL will
 * fallback to using the default resolver. */
void sol_flow_builder_set_resolver(struct sol_flow_builder *builder, const struct sol_flow_resolver *resolver);

/* Set type description to use. Input/output ports
 * and options descriptions are automatically set.
 * The strings passed as arguments will be copied. */
int sol_flow_builder_set_type_description(struct sol_flow_builder *builder, const char *name, const char *category, const char *description, const char *author, const char *url, const char *license, const char *version);

/* Add nodes to nodes spec.
 * Node names can't be NULL and must to be unique */
int sol_flow_builder_add_node(struct sol_flow_builder *builder, const char *name, const struct sol_flow_node_type *type, const struct sol_flow_node_options *option);

/* Add nodes to nodes spec by its type name. It'll use a resolver to get
 * node type and options.
 * Node names can't be NULL and must to be unique */
int sol_flow_builder_add_node_by_type(struct sol_flow_builder *builder, const char *name, const char *type_name, const char *const *options_strv);

/* Add connections to conn spec.
 * Nodes refered by names on src_name and dst_name need to be previoulsy added
 * with sol_flow_builder_add_node. It uses port names as declared on
 * description struct. If no description was declared,
 * sol_flow_builder_connect_by_index() should be used instead.
 */
int sol_flow_builder_connect(struct sol_flow_builder *builder, const char *src_name, const char *src_port_name, int src_port_idx, const char *dst_name, const char *dst_port_name, int dst_port_idx);

int sol_flow_builder_connect_by_index(struct sol_flow_builder *builder, const char *src_name, uint16_t src_port_index, const char *dst_name, uint16_t dst_port_index);

int sol_flow_builder_export_in_port(struct sol_flow_builder *builder, const char *node_name, const char *port_name, int port_idx, const char *exported_name);
int sol_flow_builder_export_out_port(struct sol_flow_builder *builder, const char *node_name, const char *port_name, int port_idx, const char *exported_name);

/* Returns the node type generated with the builder. It should be used
 * to create nodes with sol_flow_node_new(). After the type is
 * created, no more nodes or connections can be added.
 * This node type will be freed by the builder when the builder
 * itself is deleted.
 */
struct sol_flow_node_type *sol_flow_builder_get_node_type(struct sol_flow_builder *builder);

#ifdef __cplusplus
}
#endif
