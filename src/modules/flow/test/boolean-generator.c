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
#include "boolean-generator.h"
#include "sol-flow/test.h"
#include "sol-flow-internal.h"

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
        mdata->timer = NULL;
        sol_flow_send_error_packet(node, ECANCELED,
            "Unknown sample: %c. Option 'sequence' must be composed by 'T' and/or 'F' chars.",
            *mdata->it);
        return false;
    }

    sol_flow_send_bool_packet(node, SOL_FLOW_NODE_TYPE_TEST_BOOLEAN_GENERATOR__OUT__OUT,
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

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEST_BOOLEAN_GENERATOR_OPTIONS_API_VERSION,
        -EINVAL);

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
