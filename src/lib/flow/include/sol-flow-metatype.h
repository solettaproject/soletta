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

/**
 * @file
 * @brief These routines are used to manipulate the flow meta types.
 */


/**
 * @defgroup Meta Meta types
 *
 * @brief These routines are used to manipulate the flow meta types.
 *
 * Meta type nodes are nodes that are created on-the-fly. This means that there is no json file describing what are
 * the node ports, open/close functions, private data and etc.
 *
 * One usually needs to create a meta type node for two reasons:
 * <ul>
 * <li> The port types are not known before the node creation.
 * <li> The user should choose if that node will be avaible for distribuiton or not. Certain meta types
 * can be disabled and they will not be built.
 * </ul>
 *
 * To use a meta type using flow, one must declare it using the DECLARE keyword, take a look
 * in the example.
 *
 * @code
 * DECLARE=The-Name-Of-The-Node-Type:The-Name-Of-My-Meta-Type:Arguments-to-my-meta-type
 * @endcode
 *
 * The first part of the declare statement is responsible for identifying what will be
 * the node type in the flow code, the second part is the name of the meta type that
 * is going to be created and the last part are the arguments for the meta type.
 *
 * As a concrete example, the code below creates a composed meta type.
 *
 * @code
 * DECLARE=MyComposedNode:composed-new:KEY(string)|VALUE(int)
 *
 * _(constant/string:value="My Key") OUT -> KEY Composed(MyComposedNode) OUT -> _(console)
 * _(constant/int:value=20) OUT -> VALUE Composed
 * @endcode
 *
 * The example above, uses the Soletta's composed-new meta type. This meta type
 * is responsible for grouping various packet types and output them as one single packet with
 * all the input values.
 *
 * @{
 */

/**
 * @brief Meta type context
 *
 * This is used when the meta type is being created or its code is being generated.
 * It contains usefull information like the the node name, the parameters for the meta type and
 * some helper functions.
 *
 */
struct sol_flow_metatype_context {
    struct sol_str_slice name; /**< The node name that is being created. */
    struct sol_str_slice contents; /**< Parameters for the metatype that is being created. */

    /**
     * @brief Opens a file for the meta type.
     *
     *
     * @param ctx The metatype context.
     * @param name The file path.
     * @param buf Where the file contents should be stored.
     * @param size The size of the buffer.
     * @return 0 on success, negative value on failure.
     */
    int (*read_file)(
        const struct sol_flow_metatype_context *ctx,
        const char *name, const char **buf, size_t *size);

    /**
     * @brief Stores the meta type in the Soletta infrastructure.
     *
     * Any node types produced by the creator function should be
     * stored using this function, it takes ownership of the type.
     * This means that one does not need to worry about freeing the node,
     * because Soletta will do that for one.
     *
     *
     * @param ctx The metatype context.
     * @param type The type to be stored.
     * @return 0 on success, negative value on failure.
     */
    int (*store_type)(
        const struct sol_flow_metatype_context *ctx,
        struct sol_flow_node_type *type);
};

/**
 * @brief Struct that describes the meta type ports.
 *
 * This struct is used by the sol_fbp_generator in order to check if the node connections are valid.
 * The sol-fbp-generator will call sol_flow_metatype_ports_description_func() callback in order to
 * obtain an array of input and output ports description.
 *
 * @see sol_flow_metatype_ports_description_func()
 * @see sol_flow_metatype_get_ports_description_func()
 */
struct sol_flow_metatype_port_description {
    char *name; /**< The port name. */
    char *type; /**< The port type (int, float, blob and etc). */
    int array_size; /**< If the port is an array this field should be > 0. */
    int idx; /**< The port index. */
};

/**
 * @brief A callback used to create the meta type itself.
 *
 * This function is used to create a meta type, this means that this function must setup the node properties, ports and etc.
 *
 * @param ctx The meta type context.
 * @param type The meta type node that shall be used by Soletta.
 * @return 0 on success, negative value on failure.
 */
typedef int (*sol_flow_metatype_create_type_func)(const struct sol_flow_metatype_context *ctx, struct sol_flow_node_type **type);

/**
 * @brief A callback used by sol-fbp-generator to generate the meta type C code.
 *
 * This function is called by sol-fbp-generator in order generate the meta type code to be compiled.
 * The code generation is divded in three steps:
 * <ul>
 *     <li> Start type generation <br>
 *     Where the common code for the meta type is generated this include - common open/close/port process functions
 *     <li> Type generation  <br>
 *     Port definitions, node type definition
 *     <li> End type generation <br>
 *     Clean up code and etc.
 * </ul>
 *
 * The code generation functions are set using the struct sol_flow_metatype during the meta type definition.
 *
 * @note The meta type must provide all callbacks in order generate its code, if the meta type does not
 * provide the callbacks it will be impossible to use the meta type node in sol-fbp-generator.
 *
 * @param ctx The meta type context.
 * @param out A buffer where the meta type should append its code.
 *
 * @return 0 on success, negative value on failure.
 * @see struct sol_flow_metatype
 * @see struct sol_flow_metatype_context
 */
typedef int (*sol_flow_metatype_generate_code_func)(const struct sol_flow_metatype_context *ctx, struct sol_buffer *out);

/**
 * @brief A callback used create the port description of a meta type.
 *
 * @param ctx The meta type context.
 * @param in Where the input port descriptions should be inserted.
 * @param out Where the output port descriptions should be inserted.
 *
 * @return 0 on success, negative value on failure.
 *
 * @note sol-fbp-generator will free the @a in and @a out elements when it is not necessary anymore, this means
 * that it will call free() on the name and type variables.
 *
 * @see struct sol_flow_metatype_context
 * @see struct sol_flow_metatype
 * @see struct sol_flow_metatype_port_description
 */
typedef int (*sol_flow_metatype_ports_description_func)(const struct sol_flow_metatype_context *ctx, struct sol_vector *in, struct sol_vector *out);

/**
 * @brief Searchs for the callback that generates the start code of a given meta type.
 *
 * @param name The meta type name.
 *
 * @return A valid pointer to a sol_flow_metatype_generate_code_func() or @c NULL if not found.
 * @see sol_flow_metatype_generate_code_func
 */
sol_flow_metatype_generate_code_func sol_flow_metatype_get_generate_code_start_func(const struct sol_str_slice name);

/**
 * @brief Searchs for the callback that generates the body code of a given meta type.
 *
 * @param name The meta type name.
 *
 * @return A valid pointer to a sol_flow_metatype_generate_code_func() or @c NULL if not found.
 * @see sol_flow_metatype_generate_code_func
 */
sol_flow_metatype_generate_code_func sol_flow_metatype_get_generate_code_type_func(const struct sol_str_slice name);

/**
 * @brief Searchs for the callback that generates the end code of a given meta type.
 *
 * @param name The meta type name
 * @return A valid pointer to a sol_flow_metatype_generate_code_func() or @c NULL if not found.
 * @see sol_flow_metatype_generate_code_func
 */
sol_flow_metatype_generate_code_func sol_flow_metatype_get_generate_code_end_func(const struct sol_str_slice name);

/**
 * @brief Searchs for callback that describes the ports of a given meta type
 *
 * @param name The meta type name.
 *
 * @return A valid pointer to a sol_flow_metatype_ports_description_func() or @c NULL if not found.
 * @see sol_flow_metatype_ports_description_func
 */
sol_flow_metatype_ports_description_func sol_flow_metatype_get_ports_description_func(const struct sol_str_slice name);

/**
 * @brief Defines the current version of the meta type API.
 */
#define SOL_FLOW_METATYPE_API_VERSION (1)

/**
 * @brief Struct that describes a meta type.
 *
 * This structs is used to declare a meta type, it contains the create type,
 * code generation, port description functions and the name of the meta type.
 *
 * @see sol_flow_metatype_get_ports_description_func()
 * @see sol_flow_metatype_get_generate_code_end_func()
 * @see sol_flow_metatype_get_generate_code_type_func()
 * @see sol_flow_metatype_get_generate_code_start_func()
 * @see sol_flow_metatype_create_type_func()
 * @see sol_flow_metatype_ports_description_func()
 * @see sol_flow_metatype_generate_code_func()
 */
struct sol_flow_metatype {
#ifndef SOL_NO_API_VERSION
    uint16_t api_version; /**< The API version. It is autocamically set by @ref SOL_FLOW_METATYPE macro. */
#endif

    const char *name; /**< The name of the meta type. */

    sol_flow_metatype_create_type_func create_type; /**< A callback used the create the meta type. */
    sol_flow_metatype_generate_code_func generate_type_start; /**< A callback used to generate the meta type start code. */
    sol_flow_metatype_generate_code_func generate_type_body; /**< A callback used to generate the meta type code. */
    sol_flow_metatype_generate_code_func generate_type_end; /**< A callback used to generate the meta type end code. */
    sol_flow_metatype_ports_description_func ports_description; /**< A callback used to fetch the meta type port description. */
};

#ifdef SOL_FLOW_METATYPE_MODULE_EXTERNAL
/**
 * @brief Exports a meta type.
 *
 * This macro should be used to declare a meta type, making it visible to Soletta.
 * @param _NAME The meta type name.
 * @param decl The meta declarations.
 *
 * Example:
 * @code
 * SOL_FLOW_METATYPE(MY_META,
 *   .name = "My Meta"
 *   .create_type = create_function,
 *   .generate_type_start = type_start_function,
 *   .generate_type_body = body_function,
 *   .generate_type_end = type_end_function,
 *   .ports_description = ports_description_function);
 * @endcode
 */
#define SOL_FLOW_METATYPE(_NAME, decl ...) \
    SOL_API const struct sol_flow_metatype *SOL_FLOW_METATYPE = \
        &((const struct sol_flow_metatype) { \
            SOL_SET_API_VERSION(.api_version = SOL_FLOW_METATYPE_API_VERSION, ) \
            decl \
        })
#else
/**
 * @brief Exports a meta type.
 *
 * This macro should be used to declare a meta type, making it visible to Soletta.
 * @param _NAME The meta type name. This name will be appended to the sol_flow_metatype variable name.
 * @param decl The meta type declarations.
 *
 * Example:
 * @code
 * SOL_FLOW_METATYPE(MY_META,
 *   .name = "My Meta"
 *   .create_type = create_function,
 *   .generate_type_start = type_start_function,
 *   .generate_type_body = body_function,
 *   .generate_type_end = type_end_function,
 *   .ports_description = ports_description_function);
 * @endcode
 */
#define SOL_FLOW_METATYPE(_NAME, decl ...) \
    const struct sol_flow_metatype SOL_FLOW_METATYPE_ ## _NAME = { \
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_METATYPE_API_VERSION, ) \
        decl \
    }
#endif

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
