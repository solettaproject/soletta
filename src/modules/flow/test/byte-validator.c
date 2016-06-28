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
#include "sol-util-internal.h"

#include "byte-validator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

static int
_populate_values(void *data, const char *sequence)
{
    struct byte_validator_data *mdata = data;
    const char *it;
    char *tail;
    uint8_t *val;

    sol_vector_init(&mdata->values, sizeof(uint8_t));
    it = sequence;
    do {
        int int_val;
        val = sol_vector_append(&mdata->values);
        SOL_NULL_CHECK_GOTO(val, no_memory);

        errno = 0;
        int_val = strtol(it, &tail, 10);

        if (errno) {
            SOL_WRN("Failed do convert option 'sequence' to int %s: %d",
                it, errno);
            return -errno;
        }
        if (int_val < 0 || int_val > 255) {
            SOL_WRN("Byte value out of range %d", int_val);
            return -ERANGE;
        }
        if (it == tail) {
            SOL_WRN("Failed to convert option 'sequence' to int %s", it);
            return -EINVAL;
        }
        it = tail;
        *val = int_val;
    } while (*tail != '\0');

    return 0;

no_memory:
    sol_vector_clear(&mdata->values);
    return -ENOMEM;
}

int
byte_validator_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct byte_validator_data *mdata = data;
    const struct sol_flow_node_type_test_byte_validator_options *opts =
        (const struct sol_flow_node_type_test_byte_validator_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_BYTE_VALIDATOR_OPTIONS_API_VERSION,
        -EINVAL);
    mdata->done = false;

    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }

    return _populate_values(data, opts->sequence);
}

int
byte_validator_process(
    struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct byte_validator_data *mdata = data;
    uint8_t *op;
    int r;
    uint8_t val;
    bool match;

    if (mdata->done) {
        sol_flow_send_error_packet(node, ECANCELED,
            "Input stream already deviated from expected data, ignoring packets.");
        return 0;
    }
    r = sol_flow_packet_get_byte(packet, &val);
    SOL_INT_CHECK(r, < 0, r);
    op = sol_vector_get(&mdata->values, mdata->next_index);
    match = val == *op;
    mdata->next_index++;

    if (mdata->next_index == mdata->values.len || !match) {
        sol_flow_send_bool_packet(node,
            SOL_FLOW_NODE_TYPE_TEST_INT_VALIDATOR__OUT__OUT, match);
        mdata->done = true;
    }
    return 0;
}

void
byte_validator_close(struct sol_flow_node *node, void *data)
{
    struct byte_validator_data *mdata = data;

    sol_vector_clear(&mdata->values);
}
