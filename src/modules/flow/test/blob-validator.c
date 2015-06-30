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

#include "blob-validator.h"
#include "test-gen.h"

int
blob_validator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct blob_validator_data *mdata = data;
    const struct sol_flow_node_type_test_blob_validator_options *opts =
        (const struct sol_flow_node_type_test_blob_validator_options *)options;

    mdata->done = false;

    if (opts->expected == NULL || *opts->expected == '\0') {
        SOL_ERR("Option 'expected' is either NULL or empty.");
        return -EINVAL;
    }
    mdata->expected.mem = strdup(opts->expected);
    SOL_NULL_CHECK(mdata->expected.mem, -errno);

    mdata->expected.size = strlen(opts->expected);
    if (opts->expect_terminating_null_byte)
        mdata->expected.size++;

    return 0;
}

int
blob_validator_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct blob_validator_data *mdata = data;
    struct sol_blob *val;
    bool match;

    if (mdata->done) {
        sol_flow_send_error_packet(node, ECANCELED,
            "Input stream already deviated from expected data, ignoring packets.");
        return 0;
    }

    int r = sol_flow_packet_get_blob(packet, &val);
    SOL_INT_CHECK(r, < 0, r);
    match = (mdata->expected.size == val->size) && memcmp(mdata->expected.mem, val->mem, val->size) == 0;

    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_TEST_BOOLEAN_VALIDATOR__OUT__OUT, match);
    mdata->done = true;

    return 0;
}

void
blob_validator_close(struct sol_flow_node *node, void *data)
{
    struct blob_validator_data *mdata = data;

    free(mdata->expected.mem);
}
