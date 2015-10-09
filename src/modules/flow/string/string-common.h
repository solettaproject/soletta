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

int string_is_empty(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

struct string_b64_data {
    const char *base64_map;
};

/* NOTE: string_b64decode_data is handled to functions such as open/close that will expect a
 * string_b64_data, thus keep the same header (base64_map).
 */
struct string_b64decode_data {
    const char *base64_map;
    uint32_t string_conns;
    uint32_t blob_conns;
};

int string_b64encode_string(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int string_b64encode_blob(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int string_b64decode(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int string_b64decode_port_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int string_b64decode_port_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int string_b64_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);
void string_b64_close(struct sol_flow_node *node, void *data);

struct string_b16_data {
    bool uppercase;
};

/* NOTE: string_b16decode_data is handled to functions such as open/close that will expect a
 * string_b16_data, thus keep the same header (uppercase).
 */
struct string_b16decode_data {
    bool uppercase;
    uint32_t string_conns;
    uint32_t blob_conns;
};

int string_b16encode_string(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int string_b16encode_blob(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int string_b16decode(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int string_b16decode_port_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int string_b16decode_port_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int string_b16_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);
