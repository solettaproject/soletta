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
 * @ingroup Flow
 *
 * @{
 */

struct sol_flow_inspector {
    unsigned long api_version;
#define SOL_FLOW_INSPECTOR_API_VERSION (1UL)
    void (*did_open_node)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node, const struct sol_flow_node_options *options);
    void (*will_close_node)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node);
    void (*did_connect_port)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id);
    void (*will_disconnect_port)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id);
    void (*will_send_packet)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, const struct sol_flow_packet *packet);
    void (*will_deliver_packet)(const struct sol_flow_inspector *inspector, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id, const struct sol_flow_packet *packet);
};

bool sol_flow_set_inspector(const struct sol_flow_inspector *inspector);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
