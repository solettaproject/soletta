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

#include <stdio.h>

#include "sol-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta flow language's .conf file entries resolving.
 */

/**
 * @defgroup FlowResolver Resolver
 * @ingroup Flow
 *
 * @brief A Resolver matches type names to an actual node_type and possibly a
 * companion options.
 *
 * Typically, it would match type names directly, but other
 * uses exist, e.g. a resolver could match IDs from a configuration
 * file to concrete types.
 *
 * It's possible to have different Resolvers by using different heuristics
 * to implement the @c revolve callback.
 *
 * @{
 */

/**
 * @brief Resolver's structure.
 */
typedef struct sol_flow_resolver {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_RESOLVER_API_VERSION (1) /**< @brief Current API version number */
    uint16_t api_version; /**< @brief API version number */
#endif
    const char *name; /**< @brief Resolver's name (useful for logging) */
    void *data; /**< @brief Resolver's context data (will be provided to @c resolve) */

    /**
     * @brief Resolution routine
     *
     * That is the routine used to resolve @a id into a node type.
     *
     * @param data Contextual data of this Resolver
     * @param id Id that needs to be resolved
     * @param type Where to return the node type of id
     * @param named_opts Options of the found node type if available
     *
     * @return @c 0 in case of success, error code (always negative) otherwise
     */
    int (*resolve)(void *data, const char *id, struct sol_flow_node_type const **type, struct sol_flow_node_named_options *named_opts);
} sol_flow_resolver;

/**
 * @brief The default resolver set at compile time.
 */
const struct sol_flow_resolver *sol_flow_get_default_resolver(void);

/**
 * @brief Resolver for built-in node types.
 *
 * A Resolver implementation that interprets IDs as node type names
 * and return the appropriate built-in node type.
 */
const struct sol_flow_resolver *sol_flow_get_builtins_resolver(void);

/**
 * @brief Function used to resolve @a id into a node type.
 *
 * It uses @a resolver to translate @a id into a node type (returned in @a type)
 * and its options if available after resolution.
 *
 * When resolver is @c NULL, the default resolver is used.
 *
 * @ref sol_flow_node_named_options_fini() must be used
 * to delete members in case of success.
 *
 * @param resolver The resolver to be used to solve this id.
 * @param id Id to be resolved
 * @param type The node type for @a id if success, @c NULL otherwise
 * @param named_opts Options for the resolved node type if available, @c NULL otherwise
 *
 * @return @c 0 in case of success, error code (always negative) otherwise
 */
int sol_flow_resolve(
    const struct sol_flow_resolver *resolver,
    const char *id,
    const struct sol_flow_node_type **type,
    struct sol_flow_node_named_options *named_opts);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
