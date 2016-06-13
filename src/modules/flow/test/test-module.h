/*
 * This file is part of the Soletta™ Project
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
#include "sol-log.h"

extern struct sol_log_domain _test_log_domain;
void test_init_log_domain(void);

#undef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_test_log_domain

#define DECLARE_PROCESS_FUNCTION(_name)             \
    int _name(                                      \
    struct sol_flow_node *node,              \
    void *data,                             \
    uint16_t port,                          \
    uint16_t conn_id,                       \
    const struct sol_flow_packet *packet)

#define DECLARE_OPEN_FUNCTION(_name)                    \
    int _name(                                          \
    struct sol_flow_node *node,                  \
    void *data,                                 \
    const struct sol_flow_node_options *options)

#define DECLARE_CLOSE_FUNCTION(_name)                  \
    void _name(                                        \
    struct sol_flow_node *node,                \
    void *data)
