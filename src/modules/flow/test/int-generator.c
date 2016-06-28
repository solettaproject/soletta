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

#include "test-module.h"
#include "int-generator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

static bool
timer_tick(void *data)
{
    struct sol_flow_node *node = data;
    struct int_generator_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_irange output = { };
    int32_t *val;

    val = sol_vector_get(&mdata->values, mdata->next_index);
    output.val = *val;
    output.step = 1;
    sol_flow_send_irange_packet(node, SOL_FLOW_NODE_TYPE_TEST_INT_GENERATOR__OUT__OUT, &output);
    mdata->next_index++;

    return mdata->next_index != mdata->values.len;
}

int
int_generator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct int_generator_data *mdata = data;
    const struct sol_flow_node_type_test_int_generator_options *opts =
        (const struct sol_flow_node_type_test_int_generator_options *)options;
    const char *it;
    char *tail;
    int32_t *val;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_INT_GENERATOR_OPTIONS_API_VERSION,
        -EINVAL);

    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }
    it = opts->sequence;

    if (opts->interval < 0)
        SOL_WRN("Option 'interval' < 0, setting it to 0.");

    mdata->interval = opts->interval >= 0 ? opts->interval : 0;
    mdata->next_index = 0;

    sol_vector_init(&mdata->values, sizeof(int32_t));
    do {
        val = sol_vector_append(&mdata->values);
        SOL_NULL_CHECK_GOTO(val, no_memory);

        errno = 0;
        *val = strtol(it, &tail, 10);
        if (errno) {
            SOL_WRN("Failed do convert option 'sequence' to int %s: %d", it, errno);
            goto error;
        }
        if (it == tail) {
            SOL_WRN("Failed to convert option 'sequence' to int %s", it);
            errno = EINVAL;
            goto error;
        }
        it = tail;
    } while (*tail != '\0');

    mdata->timer = sol_timeout_add(mdata->interval, timer_tick, node);
    SOL_NULL_CHECK_GOTO(mdata->timer, error);

    return 0;

no_memory:
    errno = ENOMEM;
error:
    sol_vector_clear(&mdata->values);
    return -errno;
}

void
int_generator_close(struct sol_flow_node *node, void *data)
{
    struct int_generator_data *mdata = data;

    if (mdata->values.len != mdata->next_index)
        sol_timeout_del(mdata->timer);

    sol_vector_clear(&mdata->values);
}
