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

#include "sol-flow/power-supply.h"
#include "sol-flow-internal.h"

#include <sol-power-supply.h>
#include <sol-str-table.h>
#include <sol-util.h>
#include <errno.h>


struct get_list_data {
    enum sol_power_supply_type type;
    bool type_defined;
};

struct get_props_data {
    char *name;
};

static void
set_type(struct get_list_data *mdata, const char *type)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("Unknown", SOL_POWER_SUPPLY_TYPE_UNKNOWN),
        SOL_STR_TABLE_ITEM("Battery", SOL_POWER_SUPPLY_TYPE_BATTERY),
        SOL_STR_TABLE_ITEM("UPS", SOL_POWER_SUPPLY_TYPE_UPS),
        SOL_STR_TABLE_ITEM("Mains", SOL_POWER_SUPPLY_TYPE_MAINS),
        SOL_STR_TABLE_ITEM("USB", SOL_POWER_SUPPLY_TYPE_USB),
        SOL_STR_TABLE_ITEM("USB_DCP", SOL_POWER_SUPPLY_TYPE_USB_DCP),
        SOL_STR_TABLE_ITEM("USB_CDP", SOL_POWER_SUPPLY_TYPE_USB_CDP),
        SOL_STR_TABLE_ITEM("USB_ACA", SOL_POWER_SUPPLY_TYPE_USB_ACA),
        { }
    };

    if (!strcasecmp(type, "any")) {
        mdata->type_defined = false;
        return;
    }

    mdata->type = sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(type),
        SOL_POWER_SUPPLY_TYPE_UNKNOWN);
    mdata->type_defined = true;
}

static int
get_list(struct sol_flow_node *node, struct get_list_data *mdata)
{
    struct sol_ptr_vector list;
    char *name;
    int r;
    uint16_t i;

    if (mdata->type_defined)
        r = sol_power_supply_get_list_by_type(&list, mdata->type);
    else
        r = sol_power_supply_get_list(&list);

    SOL_INT_CHECK(r, < 0, r);

    SOL_PTR_VECTOR_FOREACH_IDX (&list, name, i) {
        r = sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_LIST__OUT__OUT,
            name);
        if (r < 0)
            SOL_WRN("Failed to send power supply name: %s", name);
    }

    sol_power_supply_free_list(&list);

    return 0;
}

static int
get_list_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct get_list_data *mdata = data;
    const struct sol_flow_node_type_power_supply_get_list_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_LIST_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_power_supply_get_list_options *)
        options;

    if (opts->type)
        set_type(mdata, opts->type);

    return 0;
}

static int
get_list_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return get_list(node, data);
}

static int
set_type_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct get_list_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    set_type(mdata, in_value);

    return 0;
}

static int
set_name(struct sol_flow_node *node, struct get_props_data *mdata, const char *name)
{
    int r;
    bool exist;

    mdata->name = strdup(name);
    SOL_NULL_CHECK(mdata->name, -ENOMEM);

    r = sol_power_supply_exist(mdata->name, &exist);
    SOL_INT_CHECK(r, < 0, r);

    if (!exist)
        return sol_flow_send_error_packet(node, ENOENT,
            "Power supply %s doesn't exist.", mdata->name);

    return 0;
}

static int
get_props_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct get_props_data *mdata = data;
    const struct sol_flow_node_type_power_supply_get_capacity_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_CAPACITY_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_power_supply_get_capacity_options *)
        options;

    if (opts->name)
        return set_name(node, mdata, opts->name);

    return 0;
}

static void
get_props_close(struct sol_flow_node *node, void *data)
{
    struct get_props_data *mdata = data;

    free(mdata->name);
}

static int
set_supply_name(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct get_props_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    free(mdata->name);
    return set_name(node, mdata, in_value);
}

static int
get_capacity(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct get_props_data *mdata = data;
    struct sol_irange capacity = { 0, 0, 100, 1 };
    enum sol_power_supply_capacity_level capacity_level;
    int r;
    bool exist;

    static const char *level_msgs[] = {
        [SOL_POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN] = "Unknown",
        [SOL_POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL] = "Critical",
        [SOL_POWER_SUPPLY_CAPACITY_LEVEL_LOW] = "Low",
        [SOL_POWER_SUPPLY_CAPACITY_LEVEL_NORMAL] = "Normal",
        [SOL_POWER_SUPPLY_CAPACITY_LEVEL_HIGH] = "High",
        [SOL_POWER_SUPPLY_CAPACITY_LEVEL_FULL] = "Full",
    };

    if (!mdata->name)
        return sol_flow_send_error_packet(node, EINVAL,
            "Missing power supply name.");

    r = sol_power_supply_exist(mdata->name, &exist);
    SOL_INT_CHECK(r, < 0, r);
    if (!exist)
        return sol_flow_send_error_packet(node, EINVAL,
            "Power supply %s doesn't exist.", mdata->name);

    r = sol_power_supply_get_capacity(mdata->name, &capacity.val);
    if (r < 0) {
        r = sol_flow_send_error_packet(node, ENOENT,
            "Couldn't get power supply %s capacity.", mdata->name);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        r = sol_flow_send_irange_packet(node,
            SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_CAPACITY__OUT__CAPACITY,
            &capacity);
        SOL_INT_CHECK(r, < 0, r);
    }

    r = sol_power_supply_get_capacity_level(mdata->name, &capacity_level);
    if (r < 0) {
        r = sol_flow_send_error_packet(node, EINVAL,
            "Couldn't get power supply %s capacity level.", mdata->name);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        r = sol_flow_send_string_packet(node,
            SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_CAPACITY__OUT__CAPACITY_LEVEL,
            level_msgs[capacity_level]);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
send_string_prop(struct sol_flow_node *node, const char *name, int (*func)(const char *name, char **prop), uint16_t port, const char *err_msg)
{
    char *prop;
    int r;

    r = func(name, &prop);
    if (r < 0) {
        r = sol_flow_send_error_packet_str(node, EINVAL, err_msg);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        r = sol_flow_send_string_packet(node, port, prop);
        free(prop);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
get_info(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct get_props_data *mdata = data;
    int r;
    bool exist;

    if (!mdata->name)
        return sol_flow_send_error_packet(node, EINVAL,
            "Missing power supply name.");

    r = sol_power_supply_exist(mdata->name, &exist);
    SOL_INT_CHECK(r, < 0, r);
    if (!exist)
        return sol_flow_send_error_packet(node, ENOENT,
            "Power supply %s doesn't exist.", mdata->name);

    r = send_string_prop(node, mdata->name, sol_power_supply_get_manufacturer,
        SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_INFO__OUT__MANUFACTURER,
        "Couldn't get power supply manufacturer.");
    SOL_INT_CHECK(r, < 0, r);

    send_string_prop(node, mdata->name, sol_power_supply_get_model_name,
        SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_INFO__OUT__MODEL,
        "Couldn't get power supply model.");
    SOL_INT_CHECK(r, < 0, r);

    send_string_prop(node, mdata->name, sol_power_supply_get_serial_number,
        SOL_FLOW_NODE_TYPE_POWER_SUPPLY_GET_INFO__OUT__SERIAL,
        "Couldn't get power supply serial.");
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

#include "power-supply-gen.c"
