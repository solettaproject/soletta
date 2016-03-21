/*
 * This file is part of the Soletta Project
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

int string_regexp_search_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);

void string_regexp_search_close(struct sol_flow_node *node, void *data);

int string_regexp_search(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

int set_string_regexp_pattern(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

int set_string_regexp_index(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

int set_string_regexp_max_match(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

struct string_regexp_search_data {
    struct sol_flow_node *node;
    struct sol_vector substrings;
    size_t max_regexp_search;
    char *string;
    char *regexp;
    int index;
};

int string_regexp_replace_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);

void string_regexp_replace_close(struct sol_flow_node *node, void *data);

int string_regexp_replace(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

int set_string_regexp_replace_pattern(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

int set_string_regexp_replace_to(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

int set_string_regexp_replace_max_match(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

struct string_regexp_replace_data {
    struct sol_flow_node *node;
    char *orig_string;
    char *regexp;
    char *to_regexp;
    int32_t max_regexp_replace;
    bool forward_on_no_match;
};
