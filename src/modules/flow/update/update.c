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
#include "sol-str-slice.h"
#include "sol-types.h"
#include "sol-update.h"

#include <sol-util.h>
#include <errno.h>

struct update_data {
    struct sol_update_handle *handle;
    struct sol_flow_node *node;
    uint16_t progress_port;
};

static int
check_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct update_data *mdata = data;

    mdata->node = node;
    mdata->progress_port = SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__PROGRESS;

    return 0;
}

static void
check_close(struct sol_flow_node *node, void *data)
{
    struct update_data *mdata = data;

    if (mdata->handle)
        sol_update_cancel(mdata->handle);
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
    struct update_data *mdata = data;

    if (status < 0) {
        sol_flow_send_error_packet(mdata->node, -status,
            "Error while checking for updates");
        goto end;
    }

    sol_flow_send_string_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__VERSION, response->version);
    sol_flow_send_irange_value_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__SIZE, response->size);

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

    mdata->handle = sol_update_check(check_cb, mdata);
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
    struct update_data *mdata = data;

    mdata->node = node;
    mdata->progress_port = SOL_FLOW_NODE_TYPE_UPDATE_FETCH__OUT__PROGRESS;

    return 0;
}

static void
fetch_close(struct sol_flow_node *node, void *data)
{
    struct update_data *mdata = data;

    if (mdata->handle)
        sol_update_cancel(mdata->handle);
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
    struct update_data *mdata = data;

    if (status < 0) {
        sol_flow_send_error_packet(mdata->node, -status,
            "Error while fetching update file: %s",
            sol_util_strerrora(-status));
    }

    sol_flow_send_boolean_packet(mdata->node,
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

    mdata->handle = sol_update_fetch(fetch_cb, mdata, true);
    if (!mdata->handle) {
        sol_flow_send_error_packet(node, EINVAL, "Could not fetch update file");
        return -EINVAL;
    }

    return 0;
}

static int
install_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct update_data *mdata = data;

    mdata->node = node;
    mdata->progress_port = SOL_FLOW_NODE_TYPE_UPDATE_INSTALL__OUT__PROGRESS;

    return 0;
}

static void
install_close(struct sol_flow_node *node, void *data)
{
    struct update_data *mdata = data;

    if (mdata->handle)
        sol_update_cancel(mdata->handle);
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
    struct update_data *mdata = data;

    if (status < 0) {
        sol_flow_send_error_packet(mdata->node, -status,
            "Error while installing update");
    }

    sol_flow_send_boolean_packet(mdata->node,
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

    mdata->handle = sol_update_install(install_cb, mdata);
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
    int progress;

    if (mdata->handle) {
        progress = sol_update_get_progress(mdata->handle);
        sol_flow_send_irange_value_packet(node, mdata->progress_port, progress);
    } else {
        SOL_DBG("No current operation in process, ignoring request to get progress");
    }

    return 0;
}

#include "update-gen.c"
