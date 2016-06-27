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

#include "sol-flow/netctl.h"
#include "sol-flow-internal.h"

#include <sol-netctl.h>
#include <sol-str-table.h>
#include <sol-util-internal.h>
#include <errno.h>

struct list_service_data {
    struct sol_ptr_vector service_list;
    struct sol_flow_node *node;
};

struct connection_service_data {
    char *service_name;
};

struct get_state_data {
    char *service_name;
};

void
network_service_cb(void *data, const struct sol_netctl_service *service)
{
    struct list_service_data *mdata = data;
    struct sol_flow_node *node = mdata->node;
    int r;
    const char *name;

    if (sol_netctl_service_get_state(service) ==
        SOL_NETCTL_SERVICE_STATE_REMOVE) {
        sol_ptr_vector_del_element(&mdata->service_list, service);
        return;
    }

    name = sol_netctl_service_get_name(service);
    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_NETCTL_LIST_SERVICES__OUT__OUT,
        name);
    if (r < 0)
        SOL_WRN("Failed to send network service name: %s", name);

    sol_ptr_vector_append(&mdata->service_list, service);
}

void
error_cb(void *data, const struct sol_netctl_service *service,
    unsigned int error)
{
    const char *name;

    name = sol_netctl_service_get_name(service);

    if (name)
        SOL_ERR("Service %s error is %d", name, error);
}

static int
open_list_services(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    static const char *errmsg = "Could not add service monitor";
    struct list_service_data *mdata = data;

    mdata->node = node;

    sol_ptr_vector_init(&mdata->service_list);

    if (sol_netctl_add_service_monitor(network_service_cb, mdata))
        goto error;

    return 0;

error:
    sol_flow_send_error_packet(node, EINVAL, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EINVAL;
}

static void
close_list_services(struct sol_flow_node *node, void *data)
{
    static const char *errmsg = "Could not delete service monitor";
    struct list_service_data *mdata = data;

    if (sol_netctl_del_service_monitor(network_service_cb, mdata))
        goto error;

    sol_ptr_vector_clear(&mdata->service_list);

    return;

error:
    sol_flow_send_error_packet(node, EINVAL, "%s", errmsg);
    SOL_WRN("%s", errmsg);
}

static int
connection_handle(struct sol_flow_node *node, void *data, bool connect)
{
    struct connection_service_data *mdata = data;
    const struct sol_ptr_vector *service_list;
    struct sol_netctl_service *service;
    uint16_t i = 0;
    enum sol_netctl_service_state state;
    int r;
    const char *name;

    static const char *state_msgs[] = {
        [SOL_NETCTL_SERVICE_STATE_UNKNOWN] = "Unknown",
        [SOL_NETCTL_SERVICE_STATE_IDLE] = "Idle",
        [SOL_NETCTL_SERVICE_STATE_ASSOCIATION] = "Association",
        [SOL_NETCTL_SERVICE_STATE_CONFIGURATION] = "Configuration",
        [SOL_NETCTL_SERVICE_STATE_READY] = "Ready",
        [SOL_NETCTL_SERVICE_STATE_ONLINE] = "Online",
        [SOL_NETCTL_SERVICE_STATE_DISCONNECT] = "Disconnect",
        [SOL_NETCTL_SERVICE_STATE_FAILURE] = "Failure",
        [SOL_NETCTL_SERVICE_STATE_REMOVE] = "Remove",
    };

    service_list = sol_netctl_get_services();

    while (service = sol_ptr_vector_get(service_list,i)) {
        state = sol_netctl_service_get_state(service);
        name = sol_netctl_service_get_name(service);

        if (name && strcmp(name, mdata->service_name) == 0) {
            if (state == SOL_NETCTL_SERVICE_STATE_IDLE && connect) {
                sol_netctl_service_connect(service);
            } else if ((state == SOL_NETCTL_SERVICE_STATE_READY) && (!connect)) {
                  sol_netctl_service_disconnect(service);
            }
            state = sol_netctl_service_get_state(service);
            r = sol_flow_send_string_packet(node,
                    SOL_FLOW_NODE_TYPE_NETCTL_SERVICE__OUT__OUT,
                    state_msgs[state]);
            if (r < 0)
                SOL_WRN("Failed to send network service connect state");
            return 0;
        }
        i++;
    }

    sol_flow_send_error_packet(node, ENOENT, "Did not found service name %s", mdata->service_name);

    return 0;
}

static int
connect_service_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return connection_handle(node, data, true);
}

static int
disconnect_service_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return connection_handle(node, data, false);
}

static int
open_service_connection(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct connection_service_data *mdata = data;
    const struct sol_flow_node_type_netctl_service_options *opts;
    static const char *errmsg = "Could not add service monitor";

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_NETCTL_SERVICE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_netctl_service_options *)
        options;

    if (opts->name) {
        mdata->service_name = strdup(opts->name);
        SOL_NULL_CHECK(mdata->service_name, -ENOMEM);
    }

    if (sol_netctl_add_service_monitor(NULL, NULL))
        goto error;

    sol_netctl_add_error_monitor(error_cb, mdata);

    return 0;

error:
    sol_flow_send_error_packet(node, EINVAL, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EINVAL;

}

static void
close_service_connection(struct sol_flow_node *node, void *data)
{
    static const char *errmsg = "Could not delete service monitor";
    struct connection_service_data *mdata = data;

    free(mdata->service_name);

    if (sol_netctl_del_service_monitor(NULL, NULL))
        goto error;

    sol_netctl_del_error_monitor(error_cb, mdata);

    return;

error:
    sol_flow_send_error_packet(node, EINVAL, "%s", errmsg);
    SOL_WRN("%s", errmsg);
}


static int
get_state_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct get_state_data *mdata = data;
    const struct sol_flow_node_type_netctl_state_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_NETCTL_STATE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_netctl_state_options *)
        options;

    if (opts->name) {
        mdata->service_name = strdup(opts->name);
        SOL_NULL_CHECK(mdata->service_name, -ENOMEM);
    }
}

static void
get_state_close(struct sol_flow_node *node, void *data)
{
    struct get_state_data *mdata = data;

    free(mdata->service_name);
}

static int
get_state(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct get_state_data *mdata = data;
    const struct sol_ptr_vector *service_list;
    struct sol_netctl_service *service;
    uint16_t i = 0;
    enum sol_netctl_service_state state;
    int r;
    const char *name;

    static const char *state_msgs[] = {
        [SOL_NETCTL_SERVICE_STATE_UNKNOWN] = "Unknown",
        [SOL_NETCTL_SERVICE_STATE_IDLE] = "Idle",
        [SOL_NETCTL_SERVICE_STATE_ASSOCIATION] = "Association",
        [SOL_NETCTL_SERVICE_STATE_CONFIGURATION] = "Configuration",
        [SOL_NETCTL_SERVICE_STATE_READY] = "Ready",
        [SOL_NETCTL_SERVICE_STATE_ONLINE] = "Online",
        [SOL_NETCTL_SERVICE_STATE_DISCONNECT] = "Disconnect",
        [SOL_NETCTL_SERVICE_STATE_FAILURE] = "Failure",
        [SOL_NETCTL_SERVICE_STATE_REMOVE] = "Remove",
    };

    service_list = sol_netctl_get_services();

    while (service = sol_ptr_vector_get(service_list,i)) {
        name = sol_netctl_service_get_name(service);

        if (name && strcmp(name, mdata->service_name) == 0) {
            state = sol_netctl_service_get_state(service);
            r = sol_flow_send_string_packet(node,
                    SOL_FLOW_NODE_TYPE_NETCTL_STATE__OUT__OUT,
                    state_msgs[state]);
            SOL_INT_CHECK(r, < 0, r);

            if (state == SOL_NETCTL_SERVICE_STATE_ONLINE)
                r = sol_flow_send_bool_packet(node,
                        SOL_FLOW_NODE_TYPE_NETCTL_STATE__OUT__ONLINE,
                        true);
            else
                r = sol_flow_send_bool_packet(node,
                        SOL_FLOW_NODE_TYPE_NETCTL_STATE__OUT__ONLINE,
                        false);

            SOL_INT_CHECK(r, < 0, r);

            return 0;
        }
        i++;
    }

    sol_flow_send_error_packet(node, ENOENT, "Did not found service name %s", mdata->service_name);

    return 0;

}

static int
enable_radios(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool value;
    int r;

    r = sol_netctl_add_service_monitor(NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_packet_get_bool(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    SOL_WRN("%d", value);
    r = sol_netctl_set_radios_offline(value);
    SOL_INT_CHECK(r, < 0, r);

    value = sol_netctl_get_radios_offline();

    r = sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_NETCTL_RADIOS__OUT__OUT,
                        value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

#include "netctl-gen.c"
