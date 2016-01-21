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
check_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct update_node_type *type;

    type = (struct update_node_type *)sol_flow_node_get_type(node);
    type->progress_port = SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__PROGRESS;

    return 0;
}

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
    sol_flow_send_boolean_packet(node,
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

static int
fetch_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct update_node_type *type;

    type = (struct update_node_type *)sol_flow_node_get_type(node);
    type->progress_port = SOL_FLOW_NODE_TYPE_UPDATE_FETCH__OUT__PROGRESS;

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

    sol_flow_send_boolean_packet(node,
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
install_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct update_node_type *type;

    type = (struct update_node_type *)sol_flow_node_get_type(node);
    type->progress_port = SOL_FLOW_NODE_TYPE_UPDATE_INSTALL__OUT__PROGRESS;

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

    sol_flow_send_boolean_packet(node,
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
