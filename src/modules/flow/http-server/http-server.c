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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http-server-gen.h"
#include "sol-flow.h"
#include "sol-http.h"
#include "sol-http-server.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"
#include "sol_config.h"

struct http_data {
    struct sol_flow_node *node;
    union {
        bool b;
    } value;

    char *path;
};

static struct sol_http_server *server = NULL;
static int init_count = 0;

static int
start_server(struct http_data *http, const char *path,
    int (*handler)(void *data, struct sol_http_response *response, const enum sol_http_method method,
    const struct sol_http_param *params))
{
    int r;

    if (!server) {
        server = sol_http_server_new(HTTP_SERVER_PORT);
        SOL_NULL_CHECK(server, -1);
    } else {
        sol_http_server_ref(server);
    }

    init_count++;

    http->path = strdup(path);
    SOL_NULL_CHECK_GOTO(http->path, err);

    r = sol_http_server_register_handler(server, http->path, handler, http);
    SOL_INT_CHECK_GOTO(r, < 0, err_handler);

    return 0;

err_handler:
    free(http->path);
err:
    sol_http_server_unref(server);
    init_count--;
    if (!init_count)
        server = NULL;
    return -1;
}

static void
stop_server(struct http_data *http)
{
    sol_http_server_unregister_handler(server, http->path);
    free(http->path);
    sol_http_server_unref(server);
    init_count--;
    if (!init_count)
        server = NULL;
}

static int
boolean_response_cb(void *data, struct sol_http_response *response, const enum sol_http_method method,
    const struct sol_http_param *params)
{
    int r;
    uint16_t idx;
    struct http_data *mdata = data;
    static char str[] = "false";
    static struct sol_str_slice slice = { .data = str };
    struct sol_http_param_value *value;

    response->response_code = 200;
    SOL_HTTP_PARAM_FOREACH_IDX(params, value, idx) {
        switch (value->type) {
        case SOL_HTTP_PARAM_POST_FIELD:
            if (streq(value->value.key_value.key, "value")) {
                if (streq(value->value.key_value.value, "true"))
                    mdata->value.b = true;
                else if (streq(value->value.key_value.value, "false"))
                    mdata->value.b = false;
            }
            break;
        case SOL_HTTP_PARAM_HEADER:
        default:
            break;
        }
    }

    r = snprintf(str, sizeof(str), "%s", mdata->value.b == true ? "true" : "false");
    SOL_INT_CHECK(r, < 0, r);
    slice.len = strlen(str);

    r = sol_buffer_set_slice(&response->content, slice);
    SOL_INT_CHECK(r, < 0, r);

    if (method == SOL_HTTP_METHOD_POST) {
        sol_flow_send_boolean_packet(mdata->node, SOL_FLOW_NODE_TYPE_HTTP_SERVER_BOOLEAN__OUT__OUT, mdata->value.b);
    }

    return 0;
}

static int
boolean_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_data *mdata = data;
    int r;

    r = sol_flow_packet_get_boolean(packet, &mdata->value.b);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static void
boolean_close(struct sol_flow_node *node, void *data)
{
    struct http_data *mdata = data;

    stop_server(mdata);
}

static int
boolean_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct http_data *mdata = data;
    struct sol_flow_node_type_http_server_boolean_options *opts =
        (struct sol_flow_node_type_http_server_boolean_options *)options;

    r = start_server(mdata, opts->path, boolean_response_cb);
    SOL_INT_CHECK(r, < 0, r);

    mdata->value.b = opts->value;
    mdata->node = node;

    return 0;
}
#include "http-server-gen.c"
