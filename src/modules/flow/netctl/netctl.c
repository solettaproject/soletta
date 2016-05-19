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
    struct sol_flow_node *node;
    char *ap_name;
    bool connect;
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
network_connection_cb(void *data, const struct sol_netctl_service *service)
{
    struct connection_service_data *mdata = data;
    struct sol_flow_node *node = mdata->node;
    enum sol_netctl_service_state state;
    int r;
    const char *name;

    state = sol_netctl_service_get_state(service);
    name = sol_netctl_service_get_name(service);

    if (name && strcmp(name, mdata->ap_name) == 0)
        if (state == SOL_NETCTL_SERVICE_STATE_IDLE && mdata->connect) {
            sol_netctl_service_connect(service);
            sol_flow_send_string_packet(node,
                SOL_FLOW_NODE_TYPE_NETCTL_CONNECTION__OUT__OUT,
                "Connect AP!");
        } else if ((state == SOL_NETCTL_SERVICE_STATE_READY) && (!mdata->connect)) {
            sol_netctl_service_disconnect(service);
            sol_flow_send_string_packet(node,
                SOL_FLOW_NODE_TYPE_NETCTL_CONNECTION__OUT__OUT,
                "Disconnect AP!");
        }
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
set_name(void *data, const struct sol_flow_packet *packet, bool connect)
{
    struct connection_service_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->ap_name = strdup(in_value);
    SOL_NULL_CHECK(mdata->ap_name, -ENOMEM);
    mdata->connect = connect;

    return 0;
}

static int
set_connect_ap_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
   return set_name(data, packet, true); 
}

static int
set_disconnect_ap_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
   return set_name(data, packet, false);
}

static int
open_service_connection(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct connection_service_data *mdata = data;
    const struct sol_flow_node_type_netctl_connection_options *opts;
    static const char *errmsg = "Could not add service monitor";
    mdata->node = node;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_NETCTL_CONNECTION_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_netctl_connection_options *)
        options;

    if (opts->connect_ap) {
        mdata->ap_name = strdup(opts->connect_ap);
        SOL_NULL_CHECK(mdata->ap_name, -ENOMEM);
        mdata->connect = true;
    }

    if (opts->disconnect_ap) {
        mdata->ap_name = strdup(opts->disconnect_ap);
        SOL_NULL_CHECK(mdata->ap_name, -ENOMEM);
        mdata->connect = false;
    }

   if (sol_netctl_add_service_monitor(network_connection_cb, mdata))
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

    if (sol_netctl_del_service_monitor(network_connection_cb, mdata))
            goto error;

    sol_netctl_del_error_monitor(error_cb, mdata);

    return;

error:
    sol_flow_send_error_packet(node, EINVAL, "%s", errmsg);
    SOL_WRN("%s", errmsg);
}

#include "netctl-gen.c"
