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

#include "sol-buffer.h"
#include "sol-flow.h"
#include "sol-flow-resolver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta flow language (flow based programming) parsing.
 */

/**
 * @defgroup FlowParser Flow Parser
 * @ingroup Flow
 *
 * @brief The Flow Parser is used to transform a textual description
 * (either FBP or other metatypes) into node types that can be used in a flow.
 *
 * The node types created are owned by the parser object, so the
 * parser can only be deleted after all its types are not used
 * anymore.
 *
 * @{
 */

/**
 * @typedef sol_flow_parser
 *
 * @brief Flow Parser handle.
 */
struct sol_flow_parser;
typedef struct sol_flow_parser sol_flow_parser;

/**
 * @brief Flow Parser's client structure.
 */
typedef struct sol_flow_parser_client {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_PARSER_CLIENT_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif
    void *data; /**< @brief Client data */

    /**
     * @brief Parser's read callback.
     *
     * Called by the parser to load declared types,
     * @a buf should remain valid until the parser is finished.
     *
     * @param data Client's data
     * @param name File name to read
     * @param buf Where to store the file. The buffer will be initialized
     * inside this method.
     *
     * @return @c 0 on success, error code (always negative) otherwise
     */
    int (*read_file)(void *data, const char *name, struct sol_buffer *buf);
} sol_flow_parser_client;

/**
 * @brief Creates a new instance of @ref sol_flow_parser.
 *
 * @param client Parser's client
 * @param resolver @ref FlowResolver to be used
 *
 * @note Passing @c NULL to @a resolver will set the default resolver.
 *
 * @return A new Flow Parser instance, @c NULL on error.
 */
struct sol_flow_parser *sol_flow_parser_new(
    const struct sol_flow_parser_client *client,
    const struct sol_flow_resolver *resolver);

/**
 * @brief Destroy a @ref sol_flow_parser instance.
 *
 * @param parser Flow parser to be destroyed
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_parser_del(
    struct sol_flow_parser *parser);

/**
 * @brief Parses a flow description contained in @a buf and returns the resulting node type.
 *
 * @param parser The Flow Parser handle
 * @param buf Flow description to be parsed. The buffer will be
 * initialized in this function.
 * @param filename Name of the file which content is in @a buf
 *
 * @return Resulting node type of the parsed FBP on success, @c NULL otherwise.
 */
struct sol_flow_node_type *sol_flow_parse_buffer(
    struct sol_flow_parser *parser,
    const struct sol_buffer *buf,
    const char *filename);

/**
 * @brief Similar to @ref sol_flow_parse_buffer but accepts C string.
 *
 * It will calculate the size of @a str and call @ref sol_flow_parse_buffer
 * with the equivalent parameters.
 *
 * @param parser The Flow Parser handle
 * @param str Flow description to be parsed
 * @param filename Name of the file which content is in @a str
 *
 * @return Resulting node type of the parsed FBP on success, @c NULL otherwise.
 */
struct sol_flow_node_type *sol_flow_parse_string(
    struct sol_flow_parser *parser,
    const char *str,
    const char *filename);

/**
 * @brief Parsers a @a buf of a given @a metatype and returns the resulting node type.
 *
 * @param parser The Flow Parser handle
 * @param metatype Content's type (e.g. "fbp", or "js")
 * @param buf Content to be parsed
 * @param filename Name of the file which content is in @a buf
 *
 * @return Resulting node type of the parsed FBP on success, @c NULL otherwise.
 */
struct sol_flow_node_type *sol_flow_parse_buffer_metatype(
    struct sol_flow_parser *parser,
    const char *metatype,
    const struct sol_buffer *buf,
    const char *filename);

/**
 * @brief Similar to @ref sol_flow_parse_buffer_metatype but accepts C string.
 *
 * It will calculate the size of @a str and call @ref sol_flow_parse_buffer
 * with the equivalent parameters.
 *
 * @param parser The Flow Parser handle
 * @param metatype Content's type (e.g. "fbp", or "js")
 * @param str Content to be parsed
 * @param filename Name of the file which content is in @a str
 *
 * @return Resulting node type of the parsed FBP on success, @c NULL otherwise.
 */
struct sol_flow_node_type *sol_flow_parse_string_metatype(
    struct sol_flow_parser *parser,
    const char *metatype,
    const char *str,
    const char *filename);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
