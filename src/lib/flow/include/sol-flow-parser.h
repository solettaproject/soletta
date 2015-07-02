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

/* Parser is used to transform a flow description files into node
 * types that can be used in a flow.
 *
 * The node types created are owned by the parser object, so the
 * parser can only be deleted after all its types are not used
 * anymore. */

struct sol_flow_parser;

struct sol_flow_parser_client {
#define SOL_FLOW_PARSER_CLIENT_API_VERSION (1UL)
    unsigned long api_version;
    void *data;

    /* Called by parser to load declared types, buf should be valid
     * until the API call to parser returns. */
    int (*read_file)(void *data, const char *name, const char **buf, size_t *size);
};

/* Resolver is used to find node types used by the nodes in the
 * flow. If not set, will use the default resolver. */
struct sol_flow_parser *sol_flow_parser_new(
    const struct sol_flow_parser_client *client,
    const struct sol_flow_resolver *resolver);

int sol_flow_parser_del(
    struct sol_flow_parser *parser);

struct sol_flow_node_type *sol_flow_parse_buffer(
    struct sol_flow_parser *parser,
    const char *buf,
    size_t len,
    const char *filename);

/* Same as above but accepts C string. */
struct sol_flow_node_type *sol_flow_parse_string(
    struct sol_flow_parser *parser,
    const char *str,
    const char *filename);

#ifdef __cplusplus
}
#endif
