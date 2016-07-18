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

#include <stdint.h>

#include "sol-flow-packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta flow's runtime inspector.
 */

/**
 * @defgroup FlowInspector Flow Inspector
 * @ingroup Flow
 *
 * @brief Soletta flow's runtime inspector.
 *
 * @{
 */

/**
 * @brief Structure containing a set of inspecting routines.
 *
 * This structure is used to setup the inspector with a set of routines that should
 * be called in specific point and actions that happens during the execution of a flow.
 */
typedef struct sol_flow_inspector {
#ifndef SOL_NO_API_VERSION
    uint16_t api_version; /**< @brief API version */
#define SOL_FLOW_INSPECTOR_API_VERSION (1)
#endif
    /**
     * @brief Callback to trigger when a node is open.
     *
     * @param inspector This inspector
     * @param node Node that was open
     * @param options Node options
     */
    void (*did_open_node)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node, const struct sol_flow_node_options *options);

    /**
     * @brief Callback to trigger when a node is about to be closed.
     *
     * @param inspector This inspector
     * @param node Node that will be closed
     */
    void (*will_close_node)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node);

    /**
     * @brief Callback to trigger when a connection between ports is made.
     *
     * Connections are unidirectional. So it's only possible to send packets
     * from the source to the destination node.
     *
     * @param inspector This inspector
     * @param src_node Source node
     * @param src_port Connected port on the source node
     * @param src_conn_id Connection ID on the source node
     * @param dst_node Destination node
     * @param dst_port Connected port on the destination node
     * @param dst_conn_id Connection ID on the destination node
     */
    void (*did_connect_port)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id);

    /**
     * @brief Callback to trigger when a connection is terminated.
     *
     * @param inspector This inspector
     * @param src_node Source node
     * @param src_port Disconnected port on the source node
     * @param src_conn_id Connection ID on the source node
     * @param dst_node Destination node
     * @param dst_port Disconnected port on the destination node
     * @param dst_conn_id Connection ID on the destination node
     */
    void (*will_disconnect_port)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id);

    /**
     * @brief Callback to trigger when a packet is about to be sent.
     *
     * @param inspector This inspector
     * @param src_node Packet's source node
     * @param src_port Packet's source port
     * @param packet The packet
     */
    void (*will_send_packet)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, const struct sol_flow_packet *packet);

    /**
     * @brief Callback to trigger when a packet is about to be delivered.
     *
     * @param inspector This inspector
     * @param dst_node Packet's destination node
     * @param dst_port Packet's destination port
     * @param dst_conn_id Connection ID on the destination node
     * @param packet The packet
     */
    void (*will_deliver_packet)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id, const struct sol_flow_packet *packet);
} sol_flow_inspector;

/**
 * @brief Provide a set of inspecting routines to flow's runtime inspector.
 *
 * @param inspector A set of Inspector's callbacks
 *
 * @return @c true on success, @c false otherwise
 */
bool sol_flow_set_inspector(const struct sol_flow_inspector *inspector);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
