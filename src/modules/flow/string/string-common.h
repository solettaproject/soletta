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
