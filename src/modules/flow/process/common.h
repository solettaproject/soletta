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

#define SOL_LOG_DOMAIN &_process_log_domain
#include "sol-log-internal.h"
extern struct sol_log_domain _process_log_domain;

#include "sol-flow/process.h"
#include "sol-mainloop.h"
#include "sol-flow-internal.h"

struct subprocess_data {
    pid_t pid;
    struct {
        int in[2];
        int out[2];
        int err[2];
    } pipes;
    struct {
        struct sol_fd *in;
        struct sol_fd *out;
        struct sol_fd *err;
    } watches;
    struct sol_vector write_data;
    struct sol_flow_node *node;
    struct sol_platform_linux_fork_run *fork_run;
    char *command;
};

void process_log_init(void);

int process_stdin_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int process_stdin_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int process_stdin_closed_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int process_stdin_closed_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);

int process_stdout_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int process_stdout_closed_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int process_stdout_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);
void process_stdout_close(struct sol_flow_node *node, void *data);

int process_stderr_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int process_stderr_closed_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
int process_stderr_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);
void process_stderr_close(struct sol_flow_node *node, void *data);

int process_subprocess_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int process_subprocess_start_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int process_subprocess_stop_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int process_subprocess_signal_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
int process_subprocess_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);
void process_subprocess_close(struct sol_flow_node *node, void *data);
