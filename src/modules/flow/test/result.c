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

#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "test-module.h"
#include "result.h"
#include "sol-flow/test.h"

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

    SOL_WRN("Timeout expired! Failing test...");

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

    d->timer = sol_timeout_add(opts->timeout.val, on_timeout, node);
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

    int r = sol_flow_packet_get_boolean(packet, &passed);

    SOL_INT_CHECK(r, < 0, r);

    if (passed)
        pass(node);
    else
        fail(node);
    return 0;
}
