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

#include "sol-flow/netctl.h"
#include "sol-flow-internal.h"

#include <sol-netctl.h>
#include <sol-str-table.h>
#include <sol-util-internal.h>
#include <errno.h>

struct network_service_data {
    char *service_name;
    enum sol_netctl_service_state state;
    enum pending_service_operation {
        NONE, CONNECT, DISCONNECT,
    } pending;
};

static void
service_list_cb(void *data, const struct sol_netctl_service *service)
{
    struct sol_flow_node *node = data;
    const char *name;
    int r;

    name = sol_netctl_service_get_name(service);

    if (sol_netctl_service_get_state(service) ==
        SOL_NETCTL_SERVICE_STATE_REMOVE) {
        r = sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_NETCTL_LIST_SERVICES__OUT__REMOVED,
            name);
        if (r < 0)
            SOL_WRN("Failed to send removed service name: %s", name);
        return;
    }

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_NETCTL_LIST_SERVICES__OUT__ADDED,
        name);
    if (r < 0)
        SOL_WRN("Failed to send added service name: %s", name);
}

static int
open_list_services(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_ptr_vector *service_list;
    struct sol_netctl_service *service;
    int r;
    uint16_t i;

    r = sol_netctl_add_service_monitor(service_list_cb, node);
    SOL_INT_CHECK(r, < 0, r);

    service_list = sol_netctl_get_services();
    if (!service_list)
        return -ENOENT;

    SOL_PTR_VECTOR_FOREACH_IDX (service_list, service, i) {
        enum sol_netctl_service_state state = sol_netctl_service_get_state(service);
        const char *name;

        if (state == SOL_NETCTL_SERVICE_STATE_UNKNOWN ||
            state == SOL_NETCTL_SERVICE_STATE_REMOVE)
            continue;

        name = sol_netctl_service_get_name(service);
        r = sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_NETCTL_LIST_SERVICES__OUT__ADDED,
            name);
        if (r < 0)
            SOL_WRN("Failed to send added service name: %s", name);
    }

    return 0;
}

static void
close_list_services(struct sol_flow_node *node, void *data)
{
    if (sol_netctl_del_service_monitor(service_list_cb, node))
        SOL_WRN("Could not delete service monitor !");
}

static void
service_status_cb(void *data, const struct sol_netctl_service *service)
{
    struct sol_flow_node *node = data;
    struct network_service_data *mdata = sol_flow_node_get_private_data(node);
    enum sol_netctl_service_state current_state;
    const char *name;
    int r;

    name = sol_netctl_service_get_name(service);

    if (!name || strcmp(name, mdata->service_name))
        return;

    current_state = sol_netctl_service_get_state(service);
    if (current_state == mdata->state)
        return;

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_NETCTL_SERVICE__OUT__OUT,
        sol_netctl_service_state_to_str(current_state));
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (mdata->state == SOL_NETCTL_SERVICE_STATE_ONLINE) {
        r = sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_NETCTL_SERVICE__OUT__ONLINE,
            false);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    if (current_state == SOL_NETCTL_SERVICE_STATE_ONLINE) {
        r = sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_NETCTL_SERVICE__OUT__ONLINE,
            true);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    mdata->state = current_state;

    return;

error:
    mdata->state = current_state;

    SOL_WRN("service %s failed to send online state", name);
}

static void
error_cb(void *data, const struct sol_netctl_service *service,
    unsigned int error)
{
    struct sol_flow_node *node = data;
    struct network_service_data *mdata = sol_flow_node_get_private_data(node);
    const char *name;

    name = sol_netctl_service_get_name(service);

    if (!name || strcmp(name, mdata->service_name))
        return;

    sol_flow_send_error_packet(node, error, "Service %s error is: %s",
        name, sol_util_strerrora(error));
}

static int
connect_service_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct network_service_data *mdata = data;
    struct sol_netctl_service *service;
    int r;

    service = sol_netctl_find_service_by_name(mdata->service_name);

    if (!service)
        mdata->pending = CONNECT;
    else {
        r = sol_netctl_service_connect(service);
        if (r < 0)
            sol_flow_send_error_packet(node, -r, "Could not connect to service: %s: %s",
                mdata->service_name, sol_util_strerrora(-r));

        mdata->pending = NONE;
    }

    return 0;
}

static int
disconnect_service_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct network_service_data *mdata = data;
    struct sol_netctl_service *service;
    int r;

    service = sol_netctl_find_service_by_name(mdata->service_name);

    if (!service)
        mdata->pending = DISCONNECT;
    else {
        r = sol_netctl_service_disconnect(service);
        if (r < 0)
            sol_flow_send_error_packet(node, -r, "Could not disconnect to service: %s: %s",
                mdata->service_name, sol_util_strerrora(-r));

        mdata->pending = NONE;
    }

    return 0;
}

static int
state_dispatch(struct sol_flow_node *node, struct network_service_data *mdata)
{
    struct sol_netctl_service *service;
    int r;

    service = sol_netctl_find_service_by_name(mdata->service_name);
    if (!service) {
        sol_flow_send_error_packet(node, ENOENT, "Did not found service name %s", mdata->service_name);
        return 0;
    }
    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_NETCTL_SERVICE__OUT__OUT,
        sol_netctl_service_state_to_str(mdata->state));
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_NETCTL_SERVICE__OUT__ONLINE,
        mdata->state == SOL_NETCTL_SERVICE_STATE_ONLINE ? true : false);

    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
get_service_state(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct network_service_data *mdata = data;

    return state_dispatch(node, mdata);
}

static int
set_service_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct network_service_data *mdata = data;
    struct sol_netctl_service *service;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->service_name, in_value);
    SOL_NULL_CHECK(mdata->service_name, r);

    if (mdata->pending == NONE)
        goto done;

    service = sol_netctl_find_service_by_name(mdata->service_name);
    if (!service)
        goto done;

    if (mdata->pending == CONNECT)
        r = sol_netctl_service_connect(service);
    else
        r = sol_netctl_service_disconnect(service);

    if (r < 0)
        sol_flow_send_error_packet(node, -r, "Could not handle pending connection to service: %s: %s",
            mdata->service_name, sol_util_strerrora(-r));

    mdata->pending = NONE;

done:
    return state_dispatch(node, mdata);
}

static int
open_network_service(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct network_service_data *mdata = data;
    const struct sol_flow_node_type_netctl_service_options *opts;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_NETCTL_SERVICE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_netctl_service_options *)
        options;

    if (opts->name) {
        mdata->service_name = strdup(opts->name);
        SOL_NULL_CHECK(mdata->service_name, -ENOMEM);
    }

    mdata->state = SOL_NETCTL_SERVICE_STATE_UNKNOWN;
    mdata->pending = NONE;

    r = sol_netctl_add_service_monitor(service_status_cb, node);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_netctl_add_error_monitor(error_cb, node);
    SOL_INT_CHECK(r, < 0, r);

    return state_dispatch(node, mdata);
}

static void
close_network_service(struct sol_flow_node *node, void *data)
{
    struct network_service_data *mdata = data;

    free(mdata->service_name);

    if (sol_netctl_del_service_monitor(service_status_cb, node))
        SOL_WRN("Could not delete service monitor !");

    if (sol_netctl_del_error_monitor(error_cb, node))
        SOL_WRN("Could not delete error monitor !");
}

static void
manager_cb(void *data)
{
    struct sol_flow_node *node = data;
    bool offline;
    int r;

    offline = sol_netctl_get_radios_offline();

    r = sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_NETCTL_RADIO_OFFLINE__OUT__OUT,
        offline);
    if (r < 0)
        SOL_WRN("Failed to send radio offline status !");
}

static int
enable_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool value;
    int r;

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_netctl_set_radios_offline(value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
open_network_radio(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;

    r = sol_netctl_add_manager_monitor(manager_cb, node);
    SOL_INT_CHECK(r, < 0, r);

    manager_cb(node);

    return 0;
}

static void
close_network_radio(struct sol_flow_node *node, void *data)
{
    if (sol_netctl_del_manager_monitor(manager_cb, node))
        SOL_WRN("Could not delete manager monitor !");
}

#include "netctl-gen.c"
