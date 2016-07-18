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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup SingleFlow Single Flow
 * @ingroup Flow
 *
 * @brief Single flow is a parent node that creates a single children
 * that works stand alone.
 *
 * Sometimes it is desired to use a single node, manually feeding
 * packets and processing those being sent on the node's output port.
 *
 * The single node type does exactly that by wrapping a base type
 * (also called "child type"), it will create an internal node and
 * take care to deliver incoming packets to that and also take its
 * outgoing packets and calling the provided @c process() method, if
 * any.
 *
 * It also forwards connections and disconnections requests, as some
 * nodes will only work if there is a connection established for a
 * given port.
 *
 * See sol_flow_single_new() and sol_flow_single_new_type().
 *
 * @{
 */

/**
 * @def SOL_FLOW_SINGLE_CONNECTIONS()
 *
 * Convenience macro to declare an array of @c uint16_t terminated by
 * @c UINT16_MAX.
 *
 * This can be used in sol_flow_single_new() or
 * struct sol_flow_single_options.
 */
#define SOL_FLOW_SINGLE_CONNECTIONS(...) \
    (const uint16_t[]){ __VA_ARGS__, UINT16_MAX }

/**
 * @brief Structure for the specification of a single node.
 *
 * This option is used to instatiate a single-node that wrappes an
 * inner node of a given type specified at sol_flow_single_new_type().
 *
 * It is used internally by sol_flow_single_new() or explicitly by
 * those calling sol_flow_new() manually.
 *
 * It contains a struct sol_flow_node_options header (@c base) so it
 * conforms to the options protocol. Be sure to fill its @c
 * api_version with #SOL_FLOW_NODE_OPTIONS_API_VERSION and @c sub_api
 * with #SOL_FLOW_SINGLE_OPTIONS_API_VERSION, or use
 * SOL_FLOW_SINGLE_OPTIONS_DEFAULTS() to help you.
 */
typedef struct sol_flow_single_options {
    /**
     * @brief base guarantees sol_flow_node_options compatibility.
     *
     * its sub_api must be set to #SOL_FLOW_SINGLE_OPTIONS_API_VERSION.
     */
    struct sol_flow_node_options base;

#ifndef SOL_NO_API_VERSION
    /**
     * @def SOL_FLOW_SINGLE_OPTIONS_API_VERSION
     *
     * This versions the rest of the structure and should be used in
     * @c base.sub_api.
     *
     * @see SOL_FLOW_SINGLE_OPTIONS_DEFAULTS()
     */
#define SOL_FLOW_SINGLE_OPTIONS_API_VERSION (1)
#endif

    /**
     * @brief options to give to the actual base type.
     *
     * If thie single node type wraps SOL_FLOW_NODE_TYPE_CONSOLE, then
     * the options member should be of that sub_api and will be passed
     * to @c SOL_FLOW_NODE_TYPE_CONSOLE->open().
     */
    const struct sol_flow_node_options *options;

    /**
     * @brief callback to deliver outgoing packets.
     *
     * If non-NULL, this callback is used to deliver packets produced
     * by the base node, with the first parameter @c user_data being
     * the sibling member of the same name, @c node being the wrapper
     * single node (not the actual instance to avoid
     * miscommunications), @c port is the index of producing output
     * port and @c packet is the produced packet to be delivered, it
     * will be automatically deleted after this function returns, so
     * don't keep a reference to it.
     *
     * If NULL, the packet is dropped.
     *
     * @note some node types only produce packets to ports that are
     *       connected, then make sure the port index is specified in
     *       the @c connected_ports_out.
     */
    void (*process)(void *user_data, struct sol_flow_node *node, uint16_t port, const struct sol_flow_packet *packet);

    /**
     * @brief user data to give to callback @c process().
     */
    const void *user_data;

    /**
     * @brief indexes of input ports that should be connected.
     *
     * If non-NULL, must be an array terminated with @c
     * UINT16_MAX. See SOL_FLOW_SINGLE_CONNECTIONS().
     *
     * @note some nodes will only process data from connected input
     *       ports, such as boolean/and will wait all connected ports
     *       before processing and sending output packets.
     */
    const uint16_t *connected_ports_in;

    /**
     * @brief indexes of output ports that should be connected.
     *
     * If non-NULL, must be an array terminated with @c UINT16_MAX.
     * See SOL_FLOW_SINGLE_CONNECTIONS().
     *
     * @note some node types only produce packets to ports that are
     *       connected, then make sure the port index is specified
     *       so you get the packets in @c process(). Then it is
     *       @b mandatory to connect to ports one wants to receive
     *       packets, but this is not checked due performance reasons.
     */
    const uint16_t *connected_ports_out;
} sol_flow_single_options;

/**
 * @def SOL_FLOW_SINGLE_OPTIONS_DEFAULTS()
 *
 * This macro sets struct sol_flow_single_options base member to
 * contain the proper @c api_version
 * (#SOL_FLOW_NODE_OPTIONS_API_VERSION) and @c sub_api
 * (#SOL_FLOW_SINGLE_OPTIONS_API_VERSION).
 *
 * The remaining variable arguments are passed as member initializers,
 * please use ".name = value" to avoid problems and make code easy to
 * read.
 */
#define SOL_FLOW_SINGLE_OPTIONS_DEFAULTS(...) { \
        .base = { \
            SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION, ) \
            SOL_SET_API_VERSION(.sub_api = SOL_FLOW_SINGLE_OPTIONS_API_VERSION) \
        }, \
        __VA_ARGS__ \
}


/**
 * @brief create a single-node instance for the given @a base_type.
 *
 * Sometimes it is desired to use a single node, manually feeding
 * packets and processing those being sent on the node's output port.
 *
 * This function is a helper around sol_flow_single_new_type() that
 * creates the type and instance using the provided parameters.
 *
 * If many instances of the same type are desired, it is recommended
 * to create a single type with sol_flow_single_new_type() and then
 * call sol_flow_node_new() on it, using
 * struct sol_flow_single_options to inform options of the inner
 * node, connected ports and process callback.
 *
 * It is worth to mention that node must be informed of the ports they
 * will receive packets and those that they should send by specifying
 * the @c connected_ports_in and @c connected_ports_out
 * arrays.
 *
 * @param id A string to identify the node, may be @c NULL.
 * @param base_type The type of the node to be wrapped, it must be
 *        valid and a reference is stored during the returned node
 *        lifetime.
 * @param options the options to be forwarded to the wrapped node of
 *        type @a base_type.
 * @param connected_ports_in if non-NULL, an array of input port
 *        indexes to connect. The array must be @c UINT16_MAX
 *        terminated. See SOL_FLOW_SINGLE_CONNECTIONS().
 * @param connected_ports_out if non-NULL, an array of output port
 *        indexes to connect. The array must be @c UINT16_MAX
 *        terminated. See SOL_FLOW_SINGLE_CONNECTIONS().
 * @param process if non-NULL, the callback to process outgoing
 *        packets produced by node. The first argument,
 *        @c user_data will be the value of the same-name
 *        sibling parameter.
 * @param user_data the context to provide to @a process when it is
 *        called.
 *
 * @return NULL on error or the newly created node. It should be
 *         deleted with sol_flow_node_del(). One can feed the node
 *         with packets by calling sol_flow_send_packet().
 *
 * @see sol_flow_single_new_type()
 */
struct sol_flow_node *sol_flow_single_new(const char *id, const struct sol_flow_node_type *base_type, const struct sol_flow_node_options *options, const uint16_t *connected_ports_in, const uint16_t *connected_ports_out, void (*process)(void *user_data, struct sol_flow_node *node, uint16_t port, const struct sol_flow_packet *packet), const void *user_data);

/**
 * Connect the input port @a port_idx of the inner node.
 *
 * Ports connections are counted, so a matching number of disconnects
 * must happen to actually disconnect.
 *
 * @param node a valid node created from sol_flow_single_new() or
 *        sol_flow_single_new_type().
 * @param port_idx the port index to connect.
 *
 * @return number of connections (>1) on success, -errno on error. 0
 *         is never returned.
 */
int32_t sol_flow_single_connect_port_in(struct sol_flow_node *node, uint16_t port_idx);

/**
 * Disconnect the input port @a port_idx of the inner node.
 *
 * Ports connections are counted, so a matching number of disconnects
 * must happen to actually disconnect.
 *
 * @param node a valid node created from sol_flow_single_new() or
 *        sol_flow_single_new_type().
 * @param port_idx the port index to disconnect.
 *
 * @return number of connections (>1) on success, -errno on error. 0
 *         if the last connection is gone.
 */
int32_t sol_flow_single_disconnect_port_in(struct sol_flow_node *node, uint16_t port_idx);

/**
 * Connect the output port @a port_idx of the inner node.
 *
 * Ports connections are counted, so a matching number of disconnects
 * must happen to actually disconnect.
 *
 * @note prefer a static list of connections specified at node
 *       creation time. Some inner nodes will deliver packets when
 *       they are opened/created, then you will miss the initial
 *       packets since they will be dropped due lack of connections.
 *
 * @param node a valid node created from sol_flow_single_new() or
 *        sol_flow_single_new_type().
 * @param port_idx the port index to connect.
 *
 * @return number of connections (>1) on success, -errno on error. 0
 *         is never returned.
 */
int32_t sol_flow_single_connect_port_out(struct sol_flow_node *node, uint16_t port_idx);

/**
 * Disconnect the output port @a port_idx of the inner node.
 *
 * Ports connections are counted, so a matching number of disconnects
 * must happen to actually disconnect.
 *
 * @param node a valid node created from sol_flow_single_new() or
 *        sol_flow_single_new_type().
 * @param port_idx the port index to disconnect.
 *
 * @return number of connections (>1) on success, -errno on error. 0
 *         if the last connection is gone.
 */
int32_t sol_flow_single_disconnect_port_out(struct sol_flow_node *node, uint16_t port_idx);

/**
 * Return the reference to the inner node.
 *
 * @param node a valid node created from sol_flow_single_new() or
 *        sol_flow_single_new_type().
 *
 * @return the inner node wrapped by the given @a node.
 */
struct sol_flow_node *sol_flow_single_get_child(const struct sol_flow_node *node);

/**
 * @brief create a wrapper type to use @a base_type nodes without a flow.
 *
 * Sometimes it is desired to use a single node, manually feeding
 * packets and processing those being sent on the node's output port.
 *
 * To make it easy this function returns a wrapper node type that can
 * be instantiated with sol_flow_node_new() and when
 * sol_flow_send_packet() (or variants such as
 * sol_flow_send_bool_packet()) will feed the base type's instance
 * with such packets, as well as outgoing packets produced by that
 * instance can be processed by the external user.
 *
 * To process the outgoing packets provide the @c process member of
 * the struct sol_flow_single_options or the same-name parameter
 * for the sol_flow_single_new() helper. They will receive the context
 * data provided in @c user_data.
 *
 * It is worth to mention that node must be informed of the ports they
 * will receive packets and those that they should send by specifying
 * the @c connected_ports_in and @c connected_ports_out
 * arrays. Sending packets to disconnected ports is not verified may
 * result in malfunction.
 *
 * However, some nodes may produce packets even in disconnected ports
 * and these are not filtered-out automatically. If you do not want to
 * receive packets in disconnected ports, filter them by checking the
 * @c port parameter.
 *
 * @param base_type the type to be wrapped. It must be a valid type
 *        and a reference is stored while the returned type is alive.
 *
 * @return @c NULL on error or newly allocated node type wrapping @a
 *         base_type, this should be deleted with
 *         sol_flow_node_type_del().
 *
 * @see sol_flow_single_new()
 */
struct sol_flow_node_type *sol_flow_single_new_type(const struct sol_flow_node_type *base_type);

/**
 * Given a single-node type wrapper, return the internal (child) type.
 *
 * This is useful to create options since the wrapper type options
 * differ from the internal.
 *
 * @param single_type a type previously created with
 * sol_flow_single_new_type().
 *
 * @return the internal (child or base_type) used with
 * sol_flow_single_new_type(), or NULL on failures.
 */
const struct sol_flow_node_type *sol_flow_single_type_get_child_type(const struct sol_flow_node_type *single_type);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif
