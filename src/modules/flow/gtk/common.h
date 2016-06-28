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

#include <errno.h>
#include <gtk/gtk.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_gtk_log_domain
extern struct sol_log_domain _gtk_log_domain;
#include "sol-log-internal.h"
#endif

#include "sol-flow.h"
#include "sol-util-internal.h"

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
