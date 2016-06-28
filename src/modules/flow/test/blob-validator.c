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
#include <string.h>

#include "sol-flow.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"

#include "blob-validator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

int
blob_validator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct blob_validator_data *mdata = data;
    const struct sol_flow_node_type_test_blob_validator_options *opts =
        (const struct sol_flow_node_type_test_blob_validator_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_BLOB_VALIDATOR_OPTIONS_API_VERSION,
        -EINVAL);

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
    int r;

    if (mdata->done) {
        sol_flow_send_error_packet(node, ECANCELED,
            "Input stream already deviated from expected data, ignoring packets.");
        return 0;
    }

    r = sol_flow_packet_get_blob(packet, &val);
    SOL_INT_CHECK(r, < 0, r);
    match = (mdata->expected.size == val->size) && memcmp(mdata->expected.mem, val->mem, val->size) == 0;

    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_TEST_BOOLEAN_VALIDATOR__OUT__OUT, match);
    mdata->done = true;

    return 0;
}

void
blob_validator_close(struct sol_flow_node *node, void *data)
{
    struct blob_validator_data *mdata = data;

    free(mdata->expected.mem);
}
