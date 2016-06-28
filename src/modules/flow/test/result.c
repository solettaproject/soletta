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

#include <errno.h>

#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"

#include "test-module.h"
#include "result.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

static int node_count;

static void
mark_done(const struct sol_flow_node *node)
{
    struct test_result_data *d = sol_flow_node_get_private_data(node);

    if (!d->done) {
        d->done = true;
        return;
    }
    SOL_WRN("test/result node '%s' got more results than expected", sol_flow_node_get_id(node));
}

static void
pass(const struct sol_flow_node *node)
{
    struct test_result_data *d = sol_flow_node_get_private_data(node);

    if (!d->done)
        node_count--;

    mark_done(node);
    if (node_count == 0)
        sol_quit();
}

static void
fail(const struct sol_flow_node *node)
{
    mark_done(node);
    SOL_ERR("test/result node '%s' failed", sol_flow_node_get_id(node));
    sol_quit_with_code(EXIT_FAILURE);
}

static bool
on_timeout(void *data)
{
    struct test_result_data *d;

    SOL_WRN("Timeout expired! Failing test %s", sol_flow_node_get_id(data));

    fail(data);
    d = sol_flow_node_get_private_data(data);
    d->timer = NULL;

    return false;
}

int
test_result_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct test_result_data *d = data;
    const struct sol_flow_node_type_test_result_options *opts =
        (const struct sol_flow_node_type_test_result_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_RESULT_OPTIONS_API_VERSION,
        -EINVAL);

    d->timer = sol_timeout_add(opts->timeout, on_timeout, node);
    SOL_NULL_CHECK_GOTO(d->timer, error);

    node_count++;
    d->done = false;
    return 0;

error:
    return -ENOMEM;
}

void
test_result_close(struct sol_flow_node *node, void *data)
{
    struct test_result_data *d = data;

    if (d->timer)
        sol_timeout_del(d->timer);
}

int
test_pass_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    pass(node);
    return 0;
}

int
test_fail_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    fail(node);
    return 0;
}

int
test_result_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    bool passed = false;

    int r = sol_flow_packet_get_bool(packet, &passed);

    SOL_INT_CHECK(r, < 0, r);

    if (passed)
        pass(node);
    else
        fail(node);
    return 0;
}
