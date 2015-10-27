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
#include <string.h>

#include "sol-flow.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "test-module.h"
#include "boolean-generator.h"
#include "sol-flow/test.h"

static bool
timer_tick(void *data)
{
    struct sol_flow_node *node = data;
    struct boolean_generator_data *mdata = sol_flow_node_get_private_data(node);
    bool out_packet;

    if (*mdata->it == 'T') {
        out_packet = true;
    } else if (*mdata->it == 'F') {
        out_packet = false;
    } else {
        sol_flow_send_error_packet(node, ECANCELED,
            "Unknown sample: %c. Option 'sequence' must be composed by 'T' and/or 'F' chars.",
            *mdata->it);
        return false;
    }

    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_TEST_BOOLEAN_GENERATOR__OUT__OUT,
        out_packet);

    mdata->it++;
    return *mdata->it != '\0';
}

int
boolean_generator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct boolean_generator_data *mdata = data;
    const struct sol_flow_node_type_test_boolean_generator_options *opts =
        (const struct sol_flow_node_type_test_boolean_generator_options *)options;

    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }

    mdata->it = mdata->sequence = strdup(opts->sequence);
    SOL_NULL_CHECK(mdata->sequence, -errno);

    if (opts->interval < 0)
        SOL_WRN("Option 'interval' < 0, setting it to 0.");

    mdata->interval = opts->interval >= 0 ? opts->interval : 0;
    mdata->timer = sol_timeout_add(mdata->interval, timer_tick, node);
    SOL_NULL_CHECK_GOTO(mdata->timer, error);

    return 0;

error:
    free(mdata->sequence);
    return -ENOMEM;
}

void
boolean_generator_close(struct sol_flow_node *node, void *data)
{
    struct boolean_generator_data *mdata = data;

    if (*mdata->it != '\0')
        sol_timeout_del(mdata->timer);

    free(mdata->sequence);
}
