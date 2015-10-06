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

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "sol-flow.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "test-module.h"
#include "string-generator.h"
#include "sol-flow/test.h"

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
    sol_vector_init(&mdata->values, sizeof(struct sol_str_slice));
    if (opts->sequence == NULL || *opts->sequence == '\0') {
        SOL_ERR("Option 'sequence' is either NULL or empty.");
        return -EINVAL;
    }
    mdata->sequence = strdup(opts->sequence);
    SOL_NULL_CHECK_GOTO(mdata->sequence, no_memory);

    if (opts->interval.val < 0)
        SOL_WRN("Option 'interval' < 0, setting it to 0.");

    mdata->interval = opts->interval.val >= 0 ? opts->interval.val : 0;
    mdata->next_index = 0;

    mdata->values = sol_util_str_split(sol_str_slice_from_str
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
