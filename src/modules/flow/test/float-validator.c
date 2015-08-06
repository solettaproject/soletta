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
#include "float-validator.h"
#include "test-gen.h"

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

        *val = sol_util_strtodn(it, &tail, -1, false);
        if (errno) {
            SOL_WRN("Failed do convert option 'sequence' to double %s: %d", it, errno);
            goto error;
        }
        if (it == tail) {
            SOL_WRN("Failed to convert option 'sequence' to double %s", it);
            errno = -EINVAL;
            goto error;
        }
        it = tail;
    } while (*tail != '\0');

    return 0;

no_memory:
    errno = -ENOMEM;
error:
    sol_vector_clear(&mdata->values);
    return errno;
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
    match = sol_drange_val_equal(input.val, *op);
    mdata->next_index++;

    if (mdata->next_index == mdata->values.len || !match) {
        sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_TEST_FLOAT_VALIDATOR__OUT__OUT,
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
