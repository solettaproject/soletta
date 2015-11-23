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

struct base_data {
    struct sol_update_handle *handle;
    struct sol_flow_node *node;
    uint16_t progress_port;
};

struct check_data {
    struct base_data base;
    char *url;
};

struct fetch_data {
    struct base_data base;
    struct sol_blob *update_info;
};

struct install_data {
    struct base_data base;
    char *file_path;
};

static int
check_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct check_data *mdata = data;
    const struct sol_flow_node_type_update_check_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_UPDATE_CHECK_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_update_check_options *)options;

    mdata->base.node = node;
    mdata->base.progress_port = SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__PROGRESS;

    if (opts->url && strlen(opts->url) > 0) {
        mdata->url = strdup(opts->url);
        SOL_NULL_CHECK(mdata->url, -ENOMEM);
    }

    return 0;
}

static void
check_close(struct sol_flow_node *node, void *data)
{
    struct check_data *mdata = data;

    if (mdata->base.handle)
        sol_update_cancel(mdata->base.handle);

    free(mdata->url);
}

static int
cancel_check_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct check_data *mdata = data;

    if (mdata->base.handle) {
        if (!sol_update_cancel(mdata->base.handle))
            sol_flow_send_error_packet(node, EINVAL,
                "Could not cancel check process");
        else
            mdata->base.handle = NULL;
    } else {
        SOL_WRN("No current check in process, ignoring request to cancel");
    }

    return 0;
}

static struct sol_blob *
serialize_info(const struct sol_update_info *response)
{
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_blob *blob;
    size_t size;
    void *v;
    int r;

    r = sol_buffer_append_slice(&buffer, (const struct sol_str_slice)SOL_STR_SLICE_LITERAL("{\"version\":"));
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_json_serialize_string(&buffer, response->version);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    r = sol_buffer_append_slice(&buffer, (const struct sol_str_slice)SOL_STR_SLICE_LITERAL(",\"url\":"));
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_json_serialize_string(&buffer, response->url);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    r = sol_buffer_append_slice(&buffer, (const struct sol_str_slice)SOL_STR_SLICE_LITERAL(",\"hash\":"));
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_json_serialize_string(&buffer, response->hash);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    r = sol_buffer_append_slice(&buffer, (const struct sol_str_slice)SOL_STR_SLICE_LITERAL(",\"hash-algorithm\":"));
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_json_serialize_string(&buffer, response->hash_algorithm);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    r = sol_buffer_append_char(&buffer, '}');
    SOL_INT_CHECK_GOTO(r, < 0, err);

    v = sol_buffer_steal(&buffer, &size);
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, v, size);
    SOL_NULL_CHECK_GOTO(blob, err);

    return blob;
err:
    sol_buffer_fini(&buffer);

    return NULL;
}

static void
check_cb(void *data, int status, const struct sol_update_info *response)
{
    struct base_data *mdata = data;
    struct sol_blob *blob;

    if (status < 0) {
        sol_flow_send_error_packet(mdata->node, -status,
            "Error while checking for updates");
        goto end;
    }

    /* TODO maybe a custom packet instead of JSON? Maybe sol_update_info have
     * the raw json? */
    blob = serialize_info(response);
    if (blob) {
        sol_flow_send_json_object_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__UPDATE_INFO, blob);
        sol_blob_unref(blob);
    } else {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Could not create JSON for UPDATE_INFO");
    }

    sol_flow_send_string_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UPDATE_CHECK__OUT__VERSION, response->version);

end:
    mdata->handle = NULL;
}

static int
check_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct check_data *mdata = data;

    if (!mdata->url) {
        SOL_WRN("No URL to check for updates");
        return -EINVAL;
    }

    if (mdata->base.handle) {
        sol_flow_send_error_packet(node, EINVAL,
            "Check already in progress. You may want to CANCEL before issuing a new CHECK");
        return -EINVAL;
    }

    mdata->base.handle = sol_update_check(mdata->url, check_cb, mdata);
    if (!mdata->base.handle) {
        sol_flow_send_error_packet(node, EINVAL,
            "Could not check for updates");
        return -EINVAL;
    }

    return 0;
}

static int
url_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct check_data *mdata = data;

    int r;

    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->url, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
fetch_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct install_data *mdata = data;

    mdata->base.node = node;
    mdata->base.progress_port = SOL_FLOW_NODE_TYPE_UPDATE_FETCH__OUT__PROGRESS;

    return 0;
}

static void
fetch_close(struct sol_flow_node *node, void *data)
{
    struct fetch_data *mdata = data;

    if (mdata->base.handle)
        sol_update_cancel(mdata->base.handle);

    if (mdata->update_info)
        sol_blob_unref(mdata->update_info);
}

static int
cancel_fetch_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct fetch_data *mdata = data;

    if (mdata->base.handle) {
        if (!sol_update_cancel(mdata->base.handle))
            sol_flow_send_error_packet(node, EINVAL,
                "Could not cancel fetch process");
        else
            mdata->base.handle = NULL;
    } else {
        SOL_WRN("No current fetch in process, ignoring request to cancel");
    }

    return 0;
}

static int
update_info_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct fetch_data *mdata = data;

    int r;

    struct sol_blob *in_value;

    r = sol_flow_packet_get_json_object(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->update_info)
        sol_blob_unref(mdata->update_info);

    mdata->update_info = sol_blob_ref(in_value);
    SOL_NULL_CHECK(in_value, -ENOMEM);

    return 0;
}

static void
fill_update_info(struct sol_update_info *info, struct sol_blob *blob)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, blob->mem, blob->size);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "url"))
            info->url = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "version"))
            info->version = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "hash"))
            info->hash = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "hash-algorithm"))
            info->hash_algorithm = sol_json_token_get_unescaped_string_copy(&value);
        else
            SOL_WRN("Unknown info member: %.*s",
                SOL_STR_SLICE_PRINT(sol_json_token_to_slice(&token)));
    }
}

static void
fetch_cb(void *data, int status, const char *file_path)
{
    struct base_data *mdata = data;

    if (status < 0) {
        sol_flow_send_error_packet(mdata->node, -status,
            "Error while fetching update file");
        goto end;
    }

    sol_flow_send_string_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UPDATE_FETCH__OUT__FILE_PATH, file_path);
end:
    mdata->handle = NULL;
}

static void
free_update_info(struct sol_update_info *info)
{
    free((void *)info->version);
    free((void *)info->url);
    free((void *)info->hash);
    free((void *)info->hash_algorithm);
}

static int
fetch_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct fetch_data *mdata = data;
    struct sol_update_info info;

    if (!mdata->update_info) {
        SOL_WRN("No UPDATE_INFO to fetch update file");
        return -EINVAL;
    }

    if (mdata->base.handle) {
        sol_flow_send_error_packet(node, EINVAL,
            "FETCH already in progress. You may want to CANCEL before issuing a new FETCH");
        return -EINVAL;
    }

    fill_update_info(&info, mdata->update_info);
    mdata->base.handle = sol_update_fetch(&info, fetch_cb, mdata, true);
    free_update_info(&info);
    if (!mdata->base.handle) {
        sol_flow_send_error_packet(node, EINVAL, "Could not fetch update file");
        return -EINVAL;
    }

    return 0;
}

static int
install_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct install_data *mdata = data;
    const struct sol_flow_node_type_update_install_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_UPDATE_INSTALL_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_update_install_options *)options;

    mdata->base.node = node;
    mdata->base.progress_port = SOL_FLOW_NODE_TYPE_UPDATE_INSTALL__OUT__PROGRESS;

    if (opts->file_path && strlen(opts->file_path) > 0) {
        mdata->file_path = strdup(opts->file_path);
        SOL_NULL_CHECK(mdata->file_path, -ENOMEM);
    }

    return 0;
}

static void
install_close(struct sol_flow_node *node, void *data)
{
    struct install_data *mdata = data;

    if (mdata->base.handle)
        sol_update_cancel(mdata->base.handle);

    free(mdata->file_path);
}

static int
file_path_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct install_data *mdata = data;
    const char *in_value;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->file_path, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
cancel_install_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct install_data *mdata = data;

    if (mdata->base.handle) {
        if (!sol_update_cancel(mdata->base.handle))
            sol_flow_send_error_packet(node, EINVAL,
                "Could not cancel install process");
        else
            mdata->base.handle = NULL;
    } else {
        SOL_WRN("No current install in process, ignoring request to cancel");
    }

    return 0;
}

static void
install_cb(void *data, int status)
{
    struct base_data *mdata = data;

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
    struct install_data *mdata = data;

    if (!mdata->file_path) {
        SOL_WRN("No file path to install");
        return -EINVAL;
    }

    if (mdata->base.handle) {
        sol_flow_send_error_packet(node, EINVAL,
            "Install already in progress. You may want to CANCEL before issuing a new INSTALL");
        return -EINVAL;
    }

    mdata->base.handle = sol_update_install(mdata->file_path, install_cb, mdata);
    if (!mdata->base.handle) {
        sol_flow_send_error_packet(node, EINVAL, "Could not install update file");
        return -EINVAL;
    }

    return 0;
}

static int
common_get_progress(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct base_data *mdata = data;
    int progress;

    if (mdata->handle) {
        progress = sol_update_get_progress(mdata->handle);
        sol_flow_send_irange_value_packet(node, mdata->progress_port, progress);
    } else {
        SOL_WRN("No current operation in process, ignoring request to get progress");
    }

    return 0;
}

#include "update-gen.c"
