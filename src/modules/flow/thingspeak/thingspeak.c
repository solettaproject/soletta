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

#include "thingspeak-gen.h"
#include "sol-flow-internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include <sol-http-client.h>
#include <sol-mainloop.h>
#include <sol-util.h>

struct talkback {
    char *api_key;
    char *endpoint;
};

struct thingspeak_execute_data {
    struct sol_flow_node *node;
    struct sol_timeout *timeout;
    struct talkback talkback;
};

struct thingspeak_add_data {
    struct sol_flow_node *node;
    struct talkback talkback;
    int position;
};

struct thingspeak_channel_update_data {
    struct sol_flow_node *node;
    struct sol_timeout *timeout;
    char *fields[8];
    char *endpoint;
    char *api_key;
    char *status;
};

#define RESPONSE_CHECK_API(response_, mdata_) \
    do { \
        if (unlikely(!response_)) { \
            sol_flow_send_error_packet(mdata_->node, EINVAL, \
                "Error while reaching Thingspeak"); \
            return; \
        } \
        if (unlikely(response_->api_version != SOL_HTTP_RESPONSE_API_VERSION)) { \
            SOL_ERR("Unexpected API version (response is %u, expected %u)", \
                response->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
            return; \
        } \
    } while (0)

static bool
init_talkback(struct talkback *talkback, const char *api_key,
    const char *talkback_id, const char *endpoint, const char *suffix)
{
    int r;

    r = asprintf(&talkback->endpoint, "%s/talkbacks/%s/%s",
        endpoint, talkback_id, suffix);
    if (r < 0)
        return false;

    talkback->api_key = strdup(api_key);
    if (!talkback->api_key) {
        free(talkback->endpoint);
        return false;
    }

    return true;
}

static void
free_talkback(struct talkback *talkback)
{
    free(talkback->api_key);
    free(talkback->endpoint);
}

static void
thingspeak_execute_close(struct sol_flow_node *node, void *data)
{
    struct thingspeak_execute_data *mdata = data;

    sol_timeout_del(mdata->timeout);
    free_talkback(&mdata->talkback);
    /* FIXME: Cancel pending connections. Need HTTP API. */
    sol_http_shutdown();
}

static void
thingspeak_execute_poll_finished(void *data, struct sol_http_response *response)
{
    struct thingspeak_execute_data *mdata = data;
    char *body;

    RESPONSE_CHECK_API(response, mdata);

    if (!response->content.used) {
        /* Empty response means no talkback commands */
        return;
    }

    if (response->response_code != 200) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Thingspeak returned an unknown response code: %d",
            response->response_code);
        return;
    }

    body = strndupa(response->content.data, response->content.used);
    sol_flow_send_string_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_THINGSPEAK_TALKBACK_EXECUTE__OUT__OUT, body);
}

static bool
thingspeak_execute_poll(void *data)
{
    struct thingspeak_execute_data *mdata = data;
    struct sol_http_param params;
    int r;

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("api_key", mdata->talkback.api_key))) {
        SOL_WRN("Could not set API key");
        mdata->timeout = NULL;
        sol_http_param_free(&params);
        return false;
    }

    r = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->talkback.endpoint,
        &params, thingspeak_execute_poll_finished, mdata);

    sol_http_param_free(&params);

    if (r < 0) {
        SOL_WRN("Could not create HTTP request");
        mdata->timeout = NULL;
        return false;
    }

    return true;
}

static int
thingspeak_execute_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct thingspeak_execute_data *mdata = data;
    const struct sol_flow_node_type_thingspeak_talkback_execute_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_THINGSPEAK_TALKBACK_EXECUTE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_thingspeak_talkback_execute_options *)options;

    if (!sol_http_init())
        return -EINVAL;

    if (!init_talkback(&mdata->talkback, opts->api_key, opts->talkback_id,
        opts->endpoint, "commands/execute"))
        goto no_memory;

    mdata->timeout = sol_timeout_add(opts->interval.val * 1000,
        thingspeak_execute_poll, mdata);
    if (!mdata->timeout)
        goto no_memory;

    mdata->node = node;
    return 0;

no_memory:
    sol_http_shutdown();
    return -ENOMEM;
}

static void
thingspeak_add_request_finished(void *data, struct sol_http_response *response)
{
    struct thingspeak_add_data *mdata = data;

    RESPONSE_CHECK_API(response, mdata);

    if (!response->content.used) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Command ID not received back from Thingspeak");
        return;
    }

    if (response->response_code != 200) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Thingspeak returned an unknown response code: %d",
            response->response_code);
        return;
    }
}

static int
thingspeak_add_in_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct thingspeak_add_data *mdata = data;
    const char *cmd_str;
    struct sol_http_param params;
    int error_code = 0;
    int r;

    r = sol_flow_packet_get_string(packet, &cmd_str);
    if (r < 0) {
        SOL_WRN("Could not get command string from packet");
        return -EINVAL;
    }

    sol_http_param_init(&params);

    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("api_key", mdata->talkback.api_key))) {
        SOL_WRN("Could not add API key");
        error_code = -ENOMEM;
        goto out;
    }

    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("command_string", cmd_str))) {
        SOL_WRN("Could not add command string");
        error_code = -ENOMEM;
        goto out;
    }

    if (mdata->position >= 0) {
        char position_str[3 * sizeof(int)];

        r = snprintf(position_str, sizeof(position_str), "%d", mdata->position);
        if (r < 0 || r >= (int)sizeof(position_str)) {
            SOL_WRN("Could not convert position to string");
            error_code = -ENOMEM;
            goto out;
        }

        if (!sol_http_param_add(&params,
            SOL_HTTP_REQUEST_PARAM_POST_FIELD("position", position_str))) {
            SOL_WRN("Could not add position");
            error_code = -ENOMEM;
            goto out;
        }
    }

    r = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->talkback.endpoint,
        &params, thingspeak_add_request_finished, mdata);
    if (r < 0) {
        SOL_WRN("Could not create HTTP request");
        error_code = -EINVAL;
    }

out:
    sol_http_param_free(&params);
    return error_code;
}

static void
thingspeak_add_close(struct sol_flow_node *node, void *data)
{
    struct thingspeak_add_data *mdata = data;

    free_talkback(&mdata->talkback);
    /* FIXME: Cancel pending connections. Need HTTP API. */
    sol_http_shutdown();
}

static int
thingspeak_add_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct thingspeak_add_data *mdata = data;
    const struct sol_flow_node_type_thingspeak_talkback_add_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_THINGSPEAK_TALKBACK_ADD_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_thingspeak_talkback_add_options *)options;

    if (!sol_http_init())
        return -EINVAL;

    if (!init_talkback(&mdata->talkback, opts->api_key, opts->talkback_id,
        opts->endpoint, "commands")) {
        sol_http_shutdown();
        return -EINVAL;
    }

    mdata->node = node;
    mdata->position = opts->position.val;

    return 0;
}

static void
thingspeak_channel_update_finished(void *data, struct sol_http_response *response)
{
    struct thingspeak_channel_update_data *mdata = data;

    RESPONSE_CHECK_API(response, mdata);

    if (!strncmp(response->content.data, "0", response->content.used)) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Could not update Thingspeak channel");
        return;
    }
}

static bool
thingspeak_channel_update_send(void *data)
{
    struct thingspeak_channel_update_data *mdata = data;
    struct sol_http_param params;
    char field_name[] = "fieldX";
    size_t i;
    int r;

    sol_http_param_init(&params);

    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_POST_FIELD("api_key", mdata->api_key))) {
        SOL_WRN("Could not add API key");
        goto out;
    }

    if (mdata->status) {
        if (!sol_http_param_add(&params,
            SOL_HTTP_REQUEST_PARAM_POST_FIELD("status", mdata->status))) {
            SOL_WRN("Could not add status field to POST parameters");
            goto out;
        }
    }

    for (i = 0; i < ARRAY_SIZE(mdata->fields); i++) {
        if (!mdata->fields[i])
            continue;

        field_name[sizeof("field") - 1] = i + '1';
        if (!sol_http_param_add(&params,
            SOL_HTTP_REQUEST_PARAM_POST_FIELD(strdupa(field_name), mdata->fields[i]))) {
            SOL_WRN("Could not add status field to POST %s parameters",
                field_name);
            goto out;
        }
    }

    r = sol_http_client_request(SOL_HTTP_METHOD_POST, mdata->endpoint, &params,
        thingspeak_channel_update_finished, mdata);
    if (r < 0) {
        SOL_WRN("Could not create HTTP request");
        goto out;
    }

out:
    sol_http_param_free(&params);

    mdata->timeout = NULL;
    return false;
}

static void
thingspeak_channel_update_queue(struct thingspeak_channel_update_data *mdata)
{
    if (mdata->timeout)
        sol_timeout_del(mdata->timeout);
    mdata->timeout = sol_timeout_add(500, thingspeak_channel_update_send,
        mdata);
    if (!mdata->timeout)
        SOL_WRN("Could not create timeout to update Thingspeak channel");
}

static int
thingspeak_channel_update_field_process(struct sol_flow_node *node,
    void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct thingspeak_channel_update_data *mdata = data;
    int n_field = port - SOL_FLOW_NODE_TYPE_THINGSPEAK_CHANNEL_UPDATE__IN__FIELD;
    const char *field;

    if (n_field < 0 || n_field >= (int)ARRAY_SIZE(mdata->fields)) {
        SOL_WRN("Invalid field ID: %d, expecting 0 to 7", n_field);
        return -EINVAL;
    }

    if (sol_flow_packet_get_string(packet, &field) < 0) {
        SOL_WRN("Could not get field <%d> string", n_field);
        return -EINVAL;
    }

    free(mdata->fields[n_field]);
    mdata->fields[n_field] = strdup(field);
    if (!mdata->fields[n_field]) {
        SOL_WRN("Could not store field <%d> value", n_field);
        return -ENOMEM;
    }

    thingspeak_channel_update_queue(mdata);
    return 0;
}

static int
thingspeak_channel_update_status_process(struct sol_flow_node *node,
    void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct thingspeak_channel_update_data *mdata = data;
    const char *status;

    if (sol_flow_packet_get_string(packet, &status) < 0) {
        SOL_WRN("Could not get status string");
        return -EINVAL;
    }

    free(mdata->status);
    mdata->status = strdup(status);
    if (!mdata->status) {
        SOL_WRN("Could not store status string");
        return -ENOMEM;
    }

    thingspeak_channel_update_queue(mdata);
    return 0;
}

static void
thingspeak_channel_update_close(struct sol_flow_node *node, void *data)
{
    struct thingspeak_channel_update_data *mdata = data;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(mdata->fields); i++)
        free(mdata->fields[i]);
    free(mdata->status);

    free(mdata->api_key);
    free(mdata->endpoint);

    sol_timeout_del(mdata->timeout);

    /* FIXME: Cancel pending connections. Need HTTP API. */
    sol_http_shutdown();
}

static int
thingspeak_channel_update_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct thingspeak_channel_update_data *mdata = data;
    const struct sol_flow_node_type_thingspeak_channel_update_options *opts;
    size_t i;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_THINGSPEAK_CHANNEL_UPDATE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_thingspeak_channel_update_options *)options;

    if (!sol_http_init())
        return -EINVAL;

    mdata->api_key = strdup(opts->api_key);
    if (!mdata->api_key)
        goto no_memory;

    r = asprintf(&mdata->endpoint, "%s/update", opts->endpoint);
    if (r < 0) {
        free(mdata->api_key);
        goto no_memory;
    }

    for (i = 0; i < ARRAY_SIZE(mdata->fields); i++)
        mdata->fields[i] = NULL;
    mdata->status = NULL;
    mdata->timeout = NULL;

    return 0;

no_memory:
    sol_http_shutdown();
    return -ENOMEM;
}

#include "thingspeak-gen.c"
