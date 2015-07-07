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

#define SOL_LOG_DOMAIN &_process_log_domain
#include "sol-log-internal.h"
extern struct sol_log_domain _process_log_domain;

#include "process-gen.h"
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
