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

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "sol-flow.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"

#include "string-validator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

static int
_populate_values(void *data)
{
    struct string_validator_data *mdata = data;
    char *it;
    struct sol_str_slice *val;
    size_t len = 0;

    sol_vector_init(&mdata->values, sizeof(struct sol_str_slice));
    it = mdata->sequence;
    do {
        val = sol_vector_append(&mdata->values);
        SOL_NULL_CHECK_GOTO(val, no_memory);

        val->data = it;
        while (*it != '\0') {
            if (*it == '|') {
                val->len = len;
                len = 0;
                it++;
                break; // Go back to 'do...while'
            }
            it++;
            len++;
        }
    } while (*it != '\0');

    val->len = len;

    return 0;

no_memory:
    sol_vector_clear(&mdata->values);
    return -ENOMEM;
}

int
string_validator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_validator_data *mdata = data;
    const struct sol_flow_node_type_test_string_validator_options *opts =
        (const struct sol_flow_node_type_test_string_validator_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_STRING_VALIDATOR_OPTIONS_API_VERSION,
        -EINVAL);
    mdata->done = false;

    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }
    mdata->sequence = strdup(opts->sequence);
    SOL_NULL_CHECK(mdata->sequence, -errno);

    return _populate_values(data);
}

int
string_validator_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct string_validator_data *mdata = data;
    struct sol_str_slice *next;
    bool match;
    const char *val = NULL;
    int r;

    if (mdata->done) {
        sol_flow_send_error_packet(node, ECANCELED,
            "Input stream already deviated from expected data, ignoring packets.");
        return 0;
    }
    r = sol_flow_packet_get_string(packet, &val);
    SOL_INT_CHECK(r, < 0, r);
    next = sol_vector_get(&mdata->values, mdata->next_index);
    match = sol_str_slice_str_eq(*next, val);
    mdata->next_index++;

    if (mdata->next_index == mdata->values.len || !match) {
        sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_TEST_STRING_VALIDATOR__OUT__OUT, match);
        mdata->done = true;
    }
    return 0;
}

void
string_validator_close(struct sol_flow_node *node, void *data)
{
    struct string_validator_data *mdata = data;

    sol_vector_clear(&mdata->values);
    free(mdata->sequence);
}
