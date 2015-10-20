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
 * @ingroup Flow
 *
 * @{
 */

/**
 * @struct sol_flow_resolver
 *
 * Resolver matches type names to an actual node_type and possibly and
 * companion options.
 *
 * A resolver typically would match type names directly, but other
 * uses exist, e.g. a resolver could match IDs from a configuration
 * file to concrete types.
 */

struct sol_flow_resolver {
#define SOL_FLOW_RESOLVER_API_VERSION (1UL)
    unsigned long api_version;
    const char *name;
    void *data;

    int (*resolve)(void *data, const char *id, struct sol_flow_node_type const **type, struct sol_flow_node_named_options *named_opts);
};

/* Default resolver set at compile time. */
const struct sol_flow_resolver *sol_flow_get_default_resolver(void);

/* When resolver is NULL, use the default resolver.
 *
 * sol_flow_node_named_options_fini() must be used to delete members
 * in case of success. */
int sol_flow_resolve(
    const struct sol_flow_resolver *resolver,
    const char *id,
    const struct sol_flow_node_type **type,
    struct sol_flow_node_named_options *named_opts);

/* Resolver that interprets IDs as node type names and return the
 * appropriate builtin node type. */
const struct sol_flow_resolver *sol_flow_get_builtins_resolver(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
