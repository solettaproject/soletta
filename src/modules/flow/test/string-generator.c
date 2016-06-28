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
#include "string-generator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

static bool
timer_tick(void *data)
{
    struct sol_flow_node *node = data;
    struct string_generator_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_str_slice *val;

    val = sol_vector_get(&mdata->values, mdata->next_index);
    sol_flow_send_string_slice_packet
        (node, SOL_FLOW_NODE_TYPE_TEST_STRING_GENERATOR__OUT__OUT, *val);
    mdata->next_index++;

    return mdata->next_index != mdata->values.len;
}

int
string_generator_open(
    struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_generator_data *mdata = data;
    const struct sol_flow_node_type_test_string_generator_options *opts =
        (const struct sol_flow_node_type_test_string_generator_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_STRING_GENERATOR_OPTIONS_API_VERSION,
        -EINVAL);
    sol_vector_init(&mdata->values, sizeof(struct sol_str_slice));
    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }
    mdata->sequence = strdup(opts->sequence);
    SOL_NULL_CHECK_GOTO(mdata->sequence, no_memory);

    if (opts->interval < 0)
        SOL_WRN("Option 'interval' < 0, setting it to 0.");

    mdata->interval = opts->interval >= 0 ? opts->interval : 0;
    mdata->next_index = 0;

    mdata->values = sol_str_slice_split(sol_str_slice_from_str
            (mdata->sequence), opts->separator, SIZE_MAX);

    mdata->timer = sol_timeout_add(mdata->interval, timer_tick, node);
    SOL_NULL_CHECK_GOTO(mdata->timer, error);

    return 0;

no_memory:
    errno = -ENOMEM;
error:
    sol_vector_clear(&mdata->values);
    return errno;
}

void
string_generator_close(struct sol_flow_node *node, void *data)
{
    struct string_generator_data *mdata = data;

    if (mdata->values.len != mdata->next_index)
        sol_timeout_del(mdata->timer);

    sol_vector_clear(&mdata->values);
    free(mdata->sequence);
}
