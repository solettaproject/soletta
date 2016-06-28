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

#include "sol-buffer.h"
#include "sol-flow/update.h"
#include "sol-flow-internal.h"
#include "sol-json.h"
#include "sol-macros.h"
#include "sol-str-slice.h"
#include "sol-types.h"
#include "sol-update.h"

#include <sol-util-internal.h>
#include <errno.h>

struct update_data {
    struct sol_update_handle *handle;
};

struct update_node_type {
    struct sol_flow_node_type base;
    uint16_t progress_port;
};

static int
cancel_check_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        if (!sol_update_cancel(mdata->handle))
            sol_flow_send_error_packet(node, EINVAL,
                "Could not cancel check process");
        else
            mdata->handle = NULL;
    } else {
        SOL_WRN("No current check in process, ignoring request to cancel");
    }

    return 0;
}

static void
check_cb(void *data, int status, const struct sol_update_info *response)
{
    struct sol_flow_node *node = data;
    struct update_data *mdata = sol_flow_node_get_private_data(node);

    if (status < 0) {
        sol_flow_send_error_packet(node, -status,
            "Error while checking for updates: %s", sol_util_strerrora(-status));
        goto end;
    }

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(response->api_version != SOL_UPDATE_INFO_API_VERSION)) {
        SOL_WRN("Update info config version '%u' is unexpected, expected '%u'",
            response->api_version, SOL_UPDATE_INFO_API_VERSION);
        return;
    }
#endif

    sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__VERSION, response->version);
    sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__SIZE, response->size);
    sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__NEED_UPDATE, response->need_update);

end:
    mdata->handle = NULL;
}

static int
check_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        SOL_DBG("Check already in progress. Ignoring packet");
        return 0;
    }

    mdata->handle = sol_update_check(check_cb, node);
    if (!mdata->handle) {
        sol_flow_send_error_packet(node, EINVAL,
            "Could not check for updates");
        return -EINVAL;
    }

    return 0;
}

static void
common_close(struct sol_flow_node *node, void *data)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        if (!sol_update_cancel(mdata->handle))
            SOL_WRN("Could not cancel ongoing task: %p", mdata->handle);
    }
}

static int
cancel_fetch_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        if (!sol_update_cancel(mdata->handle))
            sol_flow_send_error_packet(node, EINVAL,
                "Could not cancel fetch process");
        else
            mdata->handle = NULL;
    } else {
        SOL_WRN("No current fetch in process, ignoring request to cancel");
    }

    return 0;
}

static void
fetch_cb(void *data, int status)
{
    struct sol_flow_node *node = data;
    struct update_data *mdata = sol_flow_node_get_private_data(node);

    if (status < 0) {
        sol_flow_send_error_packet(node, -status,
            "Error while fetching update file: %s",
            sol_util_strerrora(-status));
    }

    sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_UPDATE_FETCH__OUT__SUCCESS, status == 0);

    mdata->handle = NULL;
}

static int
fetch_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        SOL_DBG("Fetch already in progress. Ignoring packet");
        return 0;
    }

    mdata->handle = sol_update_fetch(fetch_cb, node, true);
    if (!mdata->handle) {
        sol_flow_send_error_packet(node, EINVAL, "Could not fetch update file");
        return -EINVAL;
    }

    return 0;
}

static int
cancel_install_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        if (!sol_update_cancel(mdata->handle))
            sol_flow_send_error_packet(node, EINVAL,
                "Could not cancel install process");
        else
            mdata->handle = NULL;
    } else {
        SOL_WRN("No current install in process, ignoring request to cancel");
    }

    return 0;
}

static void
install_cb(void *data, int status)
{
    struct sol_flow_node *node = data;
    struct update_data *mdata = sol_flow_node_get_private_data(node);

    if (status < 0) {
        sol_flow_send_error_packet(node, -status,
            "Error while installing update: %s", sol_util_strerrora(-status));
    }

    sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_UPDATE_INSTALL__OUT__SUCCESS, status == 0);

    mdata->handle = NULL;
}

static int
install_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;

    if (mdata->handle) {
        SOL_DBG("Install already in progress. Ignoring packet");
        return 0;
    }

    mdata->handle = sol_update_install(install_cb, node);
    if (!mdata->handle) {
        sol_flow_send_error_packet(node, EINVAL, "Could not install update file");
        return -EINVAL;
    }

    return 0;
}

static int
common_get_progress(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct update_data *mdata = data;
    struct update_node_type *type;
    struct sol_irange irange = {
        .step = 1,
        .min = 0,
        .max = 100
    };

    type = (struct update_node_type *)sol_flow_node_get_type(node);

    if (mdata->handle) {
        irange.val = sol_update_get_progress(mdata->handle);
        if (irange.val >= 0 && irange.val <= 100)
            sol_flow_send_irange_packet(node, type->progress_port, &irange);
        else
            sol_flow_send_error_packet(node, EINVAL,
                "Could not get progress of task");
    } else {
        SOL_DBG("No current operation in process, ignoring request to get progress");
    }

    return 0;
}

#include "update-gen.c"
