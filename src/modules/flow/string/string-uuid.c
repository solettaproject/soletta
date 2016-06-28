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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "sol-flow/string.h"
#include "sol-flow-internal.h"
#include "sol-platform.h"

#include "string-uuid.h"

int
string_uuid_gen(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    SOL_BUFFER_DECLARE_STATIC(buf, 37);
    int r;
    struct string_uuid_data *mdata = data;

    r = sol_util_uuid_gen(mdata->upcase, mdata->with_hyphens, &buf);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_UUID__OUT__OUT, buf.data);
}

int
string_uuid_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct string_uuid_data *mdata = data;
    const struct sol_flow_node_type_string_uuid_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_UUID_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_string_uuid_options *)options;

    mdata->node = node;
    mdata->with_hyphens = opts->with_hyphens;
    mdata->upcase = opts->upcase;

    return string_uuid_gen(node, data, 0, 0, NULL);
}
