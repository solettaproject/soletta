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

#include <errno.h>
#include <stdlib.h>

#include "sol-flow/app.h"
#include "sol-flow-internal.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"

static int
check_index(struct sol_flow_node *node, int index)
{
    int count = sol_argc();

    if (index < 0) {
        sol_flow_send_error_packet(node, EINVAL,
            "Argument position (%d) must be non negative value.",
            index);
        return -EINVAL;
    }

    if (index >= count) {
        sol_flow_send_error_packet(node, EINVAL,
            "Argument position (%d) is greater than arguments length (%d)",
            index, count);
        return -EINVAL;
    }

    return 0;
}

static int
argv_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_app_argv_options *opts;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_APP_ARGV_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_app_argv_options *)options;

    r = check_index(node, opts->index);
    if (r < 0)
        return 0;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_APP_ARGV__OUT__OUT, sol_argv()[opts->index]);
}

static int
argc_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_APP_ARGC_ARGV__OUT__ARGC, sol_argc());
}

static int
argv_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = check_index(node, in_value);
    if (r < 0)
        return 0;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_APP_ARGC_ARGV__OUT__OUT, sol_argv()[in_value]);
}

static int
quit_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    sol_quit();
    return 0;
}

static int
quit_with_code_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int32_t in_value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_quit_with_code(in_value);
    return 0;
}

static int
quit_with_error_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int in_value;
    int r;

    r = sol_flow_packet_get_error(packet, &in_value, NULL);
    SOL_INT_CHECK(r, < 0, r);

    sol_quit_with_code(in_value);
    return 0;
}

static int
getenv_send_value(struct sol_flow_node *node, const char *var_name)
{
    const char *var_value;
    int r;

    var_value = getenv(var_name);

    r = sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_APP_GETENV__OUT__FOUND,
        (!!var_value));
    SOL_INT_CHECK(r, < 0, r);

    if (!var_value)
        return 0;

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_APP_GETENV__OUT__VALUE,
        var_value);
}

static int
getenv_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_app_getenv_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_APP_GETENV_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_app_getenv_options *)options;

    if (opts->variable_name)
        return getenv_send_value(node, opts->variable_name);

    return 0;
}

static int
getenv_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *var_name;
    int r;

    r = sol_flow_packet_get_string(packet, &var_name);
    SOL_INT_CHECK(r, < 0, r);

    return getenv_send_value(node, var_name);
}

static int
unset_name(struct sol_flow_node *node, const char *var_name)
{
    int r;

    r = unsetenv(var_name);
    if (r < 0)
        return sol_flow_send_error_packet_errno(node, errno);

    return 0;
}

static int
unsetenv_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_app_unsetenv_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_APP_UNSETENV_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_app_unsetenv_options *)options;

    if (opts->variable_name)
        return unset_name(node, opts->variable_name);

    return 0;
}

static int
unsetenv_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const char *var_name;
    int r;

    r = sol_flow_packet_get_string(packet, &var_name);
    SOL_INT_CHECK(r, < 0, r);

    return unset_name(node, var_name);
}

struct setenv_data {
    char *name;
    char *value;
    bool overwrite : 1;
};

static int
set_name(struct sol_flow_node *node, struct setenv_data *mdata)
{
    int r;

    if (!(mdata->name && mdata->value))
        return 0;

    r = setenv(mdata->name, mdata->value, mdata->overwrite);
    if (r < 0)
        return sol_flow_send_error_packet_errno(node, errno);

    return 0;
}

static int
setenv_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_app_setenv_options *opts;
    struct setenv_data *mdata = data;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_APP_SETENV_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_app_setenv_options *)options;

    if (opts->variable_name) {
        mdata->name = strdup(opts->variable_name);
        SOL_NULL_CHECK(mdata->name, -ENOMEM);
    }

    mdata->overwrite = opts->overwrite;

    return 0;
}

static int
setenv_name_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct setenv_data *mdata = data;
    const char *var_name;
    int r;

    r = sol_flow_packet_get_string(packet, &var_name);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->name);
    mdata->name = strdup(var_name);
    SOL_NULL_CHECK(mdata->name, -ENOMEM);

    return set_name(node, mdata);
}

static int
setenv_value_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct setenv_data *mdata = data;
    const char *var_value;
    int r;

    r = sol_flow_packet_get_string(packet, &var_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->value);
    mdata->value = strdup(var_value);
    SOL_NULL_CHECK(mdata->value, -ENOMEM);

    return set_name(node, mdata);
}

static void
setenv_close(struct sol_flow_node *node, void *data)
{
    struct setenv_data *mdata = data;

    free(mdata->name);
    free(mdata->value);
}

#include "app-gen.c"
