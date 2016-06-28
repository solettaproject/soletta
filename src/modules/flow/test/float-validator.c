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

#include "test-module.h"
#include "float-validator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

int
float_validator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct float_validator_data *mdata = data;
    const struct sol_flow_node_type_test_float_validator_options *opts =
        (const struct sol_flow_node_type_test_float_validator_options *)options;
    const char *it;
    char *tail;
    double *val;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_FLOAT_VALIDATOR_OPTIONS_API_VERSION,
        -EINVAL);
    mdata->done = false;

    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }

    sol_vector_init(&mdata->values, sizeof(double));
    it = opts->sequence;
    do {
        val = sol_vector_append(&mdata->values);
        SOL_NULL_CHECK_GOTO(val, no_memory);

        *val = sol_util_strtod_n(it, &tail, -1, false);
        if (errno) {
            SOL_WRN("Failed do convert option 'sequence' to double %s: %d", it, errno);
            goto error;
        }
        if (it == tail) {
            SOL_WRN("Failed to convert option 'sequence' to double %s", it);
            errno = EINVAL;
            goto error;
        }
        it = tail;
    } while (*tail != '\0');

    return 0;

no_memory:
    errno = ENOMEM;
error:
    sol_vector_clear(&mdata->values);
    return -errno;
}

int
float_validator_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct float_validator_data *mdata = data;
    struct sol_drange input;
    double *op;
    bool match;
    int r;

    if (mdata->done) {
        sol_flow_send_error_packet(node, ECANCELED,
            "Input stream already deviated from expected data, ignoring packets.");
        return 0;
    }

    r = sol_flow_packet_get_drange(packet, &input);
    SOL_INT_CHECK(r, < 0, r);
    op = sol_vector_get(&mdata->values, mdata->next_index);
    SOL_NULL_CHECK(op, -EINVAL);
    match = sol_util_double_eq(input.val, *op);
    mdata->next_index++;

    if (mdata->next_index == mdata->values.len || !match) {
        sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_TEST_FLOAT_VALIDATOR__OUT__OUT,
            match);
        mdata->done = true;
    }
    return 0;
}

void
float_validator_close(struct sol_flow_node *node, void *data)
{
    struct float_validator_data *mdata = data;

    sol_vector_clear(&mdata->values);
}
