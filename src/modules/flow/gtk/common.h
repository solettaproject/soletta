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

#include <errno.h>
#include <gtk/gtk.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_gtk_log_domain
extern struct sol_log_domain _gtk_log_domain;
#include "sol-log-internal.h"
#endif

#include "sol-flow.h"
#include "sol-util.h"

struct gtk_common_data {
    struct sol_flow_node *node;
    GtkWidget *widget;
};

int gtk_open(struct sol_flow_node *node,
    void *data,
    int (*setup_cb)(struct gtk_common_data *mdata,
        const struct sol_flow_node_options *options),
    const struct sol_flow_node_options *options);

void gtk_close(struct sol_flow_node *node, void *data);

#define DEFINE_DEFAULT_OPEN(NAME)                                   \
    int                                                      \
    gtk_ ## NAME ## _open(struct sol_flow_node *node, void *data,    \
    const struct sol_flow_node_options *options)             \
    {                                                               \
        return gtk_open(node, data, NAME ## _setup, options);       \
    }

#define DEFINE_DEFAULT_CLOSE(NAME)                                \
    void                                                   \
    gtk_ ## NAME ## _close(struct sol_flow_node *node, void *data) \
    {                                                             \
        return gtk_close(node, data);                             \
    }

#define DEFINE_DEFAULT_OPEN_CLOSE(NAME) \
    DEFINE_DEFAULT_OPEN(NAME);          \
    DEFINE_DEFAULT_CLOSE(NAME);


#define DEFINE_DEFAULT_HEADER(NAME)                                        \
    int gtk_ ## NAME ## _open(struct sol_flow_node *node, void *data,       \
    const struct sol_flow_node_options *options); \
    void gtk_ ## NAME ## _close(struct sol_flow_node *node, void *data);
