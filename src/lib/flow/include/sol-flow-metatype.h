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
#include "sol-str-slice.h"
#include "sol-buffer.h"
#include "sol-vector.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sol_flow_metatype_context {
    struct sol_str_slice name;
    struct sol_str_slice contents;

    /* Buffers are guaranteed to be valid only until the creator
     * function returns. */
    int (*read_file)(
        const struct sol_flow_metatype_context *ctx,
        const char *name, const char **buf, size_t *size);

    /* Any node types produced by the creator function should be
     * stored using this function, it takes ownership of the type. */
    int (*store_type)(
        const struct sol_flow_metatype_context *ctx,
        struct sol_flow_node_type *type);
};

struct sol_flow_metatype_port_description {
    char *name;
    char *type;
    int array_size;
    int idx;
};

typedef int (*sol_flow_metatype_create_type_func)(
    const struct sol_flow_metatype_context *,
    struct sol_flow_node_type **);

typedef int (*sol_flow_metatype_generate_code_func)(struct sol_buffer *,
    const struct sol_str_slice, const struct sol_str_slice);

typedef int (*sol_flow_metatype_ports_description_func)(const struct sol_str_slice, struct sol_vector *, struct sol_vector *);

sol_flow_metatype_generate_code_func sol_flow_metatype_get_generate_code_start_func(const struct sol_str_slice);
sol_flow_metatype_generate_code_func sol_flow_metatype_get_generate_code_type_func(const struct sol_str_slice);
sol_flow_metatype_generate_code_func sol_flow_metatype_get_generate_code_end_func(const struct sol_str_slice);
sol_flow_metatype_ports_description_func sol_flow_metatype_get_ports_description_func(const struct sol_str_slice);

#define SOL_FLOW_METATYPE_API_VERSION (1)

struct sol_flow_metatype {
    uint16_t api_version;

    const char *name;

    sol_flow_metatype_create_type_func create_type;
    sol_flow_metatype_generate_code_func generate_type_start;
    sol_flow_metatype_generate_code_func generate_type_body;
    sol_flow_metatype_generate_code_func generate_type_end;
    sol_flow_metatype_ports_description_func ports_description;
};

#ifdef SOL_FLOW_METATYPE_MODULE_EXTERNAL
#define SOL_FLOW_METATYPE(_NAME, decl ...) \
    SOL_API const struct sol_flow_metatype *SOL_FLOW_METATYPE = \
        &((const struct sol_flow_metatype) { \
            .api_version = SOL_FLOW_METATYPE_API_VERSION, \
            decl \
        })
#else
#define SOL_FLOW_METATYPE(_NAME, decl ...) \
    const struct sol_flow_metatype SOL_FLOW_METATYPE_ ## _NAME = { \
        .api_version = SOL_FLOW_METATYPE_API_VERSION, \
        decl \
    }
#endif

#ifdef __cplusplus
}
#endif
