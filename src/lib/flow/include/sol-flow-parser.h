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
 * @struct sol_flow_parser
 *
 * @brief Flow Parser handle.
 */
struct sol_flow_parser;

/**
 * @brief Flow Parser's client structure.
 */
struct sol_flow_parser_client {
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
     * @param buf Where to store the file
     * @param size Buffer's size
     *
     * @return @c 0 on success, error code (always negative) otherwise
     */
    int (*read_file)(void *data, const char *name, const char **buf, size_t *size);
};

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
 * @param buf Flow description to be parsed
 * @param len Buffer's size
 * @param filename Name of the file which content is in @a buf
 *
 * @return Resulting node type of the parsed FBP on success, @c NULL otherwise.
 */
struct sol_flow_node_type *sol_flow_parse_buffer(
    struct sol_flow_parser *parser,
    const char *buf,
    size_t len,
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
 * @param len Buffer's size
 * @param filename Name of the file which content is in @a buf
 *
 * @return Resulting node type of the parsed FBP on success, @c NULL otherwise.
 */
struct sol_flow_node_type *sol_flow_parse_buffer_metatype(
    struct sol_flow_parser *parser,
    const char *metatype,
    const char *buf,
    size_t len,
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
