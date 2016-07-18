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
#include "sol-macros.h"

/**
 * @file
 * @brief These routines are used for Soletta flow's @c Simple @c C type
 */

/**
 * @defgroup SimpleCType Flow's Simple C Type
 * @ingroup Flow
 *
 * @brief Helper to create a flow node type from a simple C function.
 *
 * @b Simple @b C @b Type is a helper to ease development of custom nodes where the full
 * power of a node type is not needed. Instead, a single function is used
 * and given to the node, the node private data and the event.
 *
 * Each node will have a context (private) data of size declared to
 * sol_flow_simple_c_type_new_full(). This is given as the last
 * argument to the callback @a func as well as can be retrieved with
 * sol_flow_node_get_private_data(). An example:
 *
 * @code
 * static int
 * mytype_func(struct sol_flow_node *node,
 *             const struct sol_flow_simple_c_type_event *ev,
 *             void *data)
 * {
 *    struct my_context *ctx = data;
 *    if (ev->type == SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_OPEN) {
 *       ctx->my_value = initial_value;
 *       ctx->my_string = strdup("inital_string");
 *    } else if (ev->type == SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CLOSE) {
 *       free(ctx->my_string);
 *       // do not free(ctx), it's automatically deleted
 *    } else {
 *       printf("value=%d, string=%s\n", ctx->my_value, ctx->my_string);
 *    }
 * }
 * @endcode
 *
 * @{
 */

/**
 * @brief @c Simple @c C event structure.
 */
typedef struct sol_flow_simple_c_type_event {
    /**
     * @brief Event type.
     *
     * @warning Use it before accessing the other members of this structure.
     */
    enum sol_flow_simple_c_type_event_type {
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_OPEN, /**< @brief Node is being open (instantiated) */
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CLOSE, /**< @brief Node is being closed (deleted) */
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CONNECT_PORT_IN, /**< @brief The input port defined by @c port index and @c port_name name is being connected */
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_DISCONNECT_PORT_IN, /**< @brief The input port defined by @c port index and @c port_name name is being disconnected */
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_PROCESS_PORT_IN, /**< @brief The input port defined by @c port index and @c port_name name received an incoming @c packet */
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CONNECT_PORT_OUT,  /**< @brief The output port defined by @c port index and @c port_name name is being connected */
        SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_DISCONNECT_PORT_OUT,  /**< @brief The output port defined by @c port index and @c port_name name is being disconnected */
    } type;
    uint16_t port; /**< @brief If type is one of SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_PORT_* events, the reference port index */
    uint16_t conn_id; /**< @brief If type is one of SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_PORT_* events, the reference connection identifier */
    const char *port_name; /* @brief If type is one of SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_PORT_* events, the port name (copy of the string given to sol_flow_simple_c_type_new_full() */
    const struct sol_flow_node_options *options; /* @brief If type is SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_OPEN, the given options */
    const struct sol_flow_packet *packet; /* @brief If type is SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_PORT_IN_PROCESS, the incoming packet */
} sol_flow_simple_c_type_event;

/**
 * @brief Input port identifier
 */
#define SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_IN 1

/**
 * @brief Output port identifier
 */
#define SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_OUT 2

/**
 * @brief Helper macro to declare an input port
 *
 * @param name Port's name
 * @param type Data type of the packets that this port should receive
 */
#define SOL_FLOW_SIMPLE_C_TYPE_PORT_IN(name, type) \
    SOL_TYPE_CHECK(const char *, name), \
    SOL_TYPE_CHECK(const struct sol_flow_packet_type *, type), \
    SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_IN

/**
 * @brief Helper macro to declare an output port
 *
 * @param name Port's name
 * @param type Data type of the packets that this port will send
 */
#define SOL_FLOW_SIMPLE_C_TYPE_PORT_OUT(name, type) \
    SOL_TYPE_CHECK(const char *, name), \
    SOL_TYPE_CHECK(const struct sol_flow_packet_type *, type), \
    SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_OUT

/**
 * @brief Creates a flow node type using a simple C function.
 *
 * The newly returned type should be freed calling
 * sol_flow_node_type_del().
 *
 * The ports of the given type should be specified using the
 * NULL-terminated variable arguments, each port takes a triple name,
 * packet_type and direction. Name is the string and direction is
 * either #SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_IN or
 * #SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_OUT. Consider using the macros
 * #SOL_FLOW_SIMPLE_C_TYPE_PORT_IN() and
 * #SOL_FLOW_SIMPLE_C_TYPE_PORT_OUT() to make it clear and future-proof.
 *
 * @param name the type name, will be used in debug. Often this is the
 *        name of the function you're using.
 * @param context_data_size the amount of bytes to store for each node
 *        instance. This memory can be retrieved from a node with
 *        sol_flow_node_get_private_data() and is given to @a func as
 *        the last argument. If zero is given, then this node
 *        shouldn't store per-instance information and data shouldn't
 *        be used.
 * @param options_size the amount of bytes to store options used with
 *        this type. The options struct must contain a struct
 *        sol_flow_node_options as its first member.
 * @param func the function to call for all events of this node such
 *        as opening (creating), closing (destroying), ports being
 *        connected or disconnected as well as incoming packets on
 *        input ports.
 *
 * @return newly created node type, free using its
 *         sol_flow_node_type_del().
 *
 * @see sol_flow_simple_c_type_new()
 * @see sol_flow_simple_c_type_new_nocontext()
 * @see sol_flow_node_type_del()
 */
struct sol_flow_node_type *sol_flow_simple_c_type_new_full(const char *name, size_t context_data_size, uint16_t options_size, int (*func)(struct sol_flow_node *node, const struct sol_flow_simple_c_type_event *ev, void *data), ...) SOL_ATTR_SENTINEL;

/**
 * @def sol_flow_simple_c_type_new(context_data_type, cb, ...)
 *
 * This macro will simplify usage of sol_flow_simple_c_type_new_full()
 * by taking only a context data type and the callback, as well as the
 * port information. It will transform the callback (func) into the
 * name of the simple_c_type as well as doing the
 * sizeof(context_data_type) to specify the data size.
 *
 * @see sol_flow_simple_c_type_new_full()
 * @see sol_flow_simple_c_type_new_nocontext()
 */
#define sol_flow_simple_c_type_new(context_data_type, cb, ...) sol_flow_simple_c_type_new_full(#cb, sizeof(context_data_type), sizeof(struct sol_flow_node_type), cb, ## __VA_ARGS__, NULL)

/**
 * @def sol_flow_simple_c_type_new_nocontext(cb, ...)
 *
 * This macro will simplify usage of sol_flow_simple_c_type_new_full()
 * by taking only the callback as well as the port information. It
 * will transform the callback (func) into the name of the simple_c_type
 * and use context_data_size as 0.
 *
 * @see sol_flow_simple_c_type_new_full()
 * @see sol_flow_simple_c_type_new()
 */
#define sol_flow_simple_c_type_new_nocontext(cb, ...) sol_flow_simple_c_type_new_full(#cb, 0, sizeof(struct sol_flow_node_type), cb, ## __VA_ARGS__, NULL)

/**
 * @brief Helper to retrieve the output port index from its name.
 *
 * While the port index is defined by the declaration order given to
 * sol_flow_simple_c_type_new_full(), sometimes it is desirable to find
 * out the index given the string.
 *
 * @note Note that this needs a lookup, so avoid doing it in hot paths.
 *
 * @param type the type to search the port index given its name.
 * @param port_out_name the output port name to retrieve the index.
 * @return UINT16_MAX if not found, the index if found.
 */
uint16_t sol_flow_simple_c_type_get_port_out_index(const struct sol_flow_node_type *type, const char *port_out_name);

/**
 * @brief Helper to retrieve the input port index from its name.
 *
 * While the port index is defined by the declaration order given to
 * sol_flow_simple_c_type_new_full(), sometimes it is desirable to find
 * in the index given the string.
 *
 * @note Note that this needs a lookup, so avoid doing it in hot paths.
 *
 * @param type the type to search the port index given its name.
 * @param port_in_name the input port name to retrieve the index.
 * @return UINT16_MAX if not found, the index if found.
 */
uint16_t sol_flow_simple_c_type_get_port_in_index(const struct sol_flow_node_type *type, const char *port_in_name);

/**
 * @}
 */
