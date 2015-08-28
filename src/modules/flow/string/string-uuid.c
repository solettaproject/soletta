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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "sol-flow-internal.h"

#include "string-gen.h"
#include "string-uuid.h"

int
string_uuid_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    char id[37] = { 0 };
    struct string_uuid_data *mdata = data;
    const struct sol_flow_node_type_string_uuid_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_UUID_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_string_uuid_options *)options;

    mdata->node = node;
    mdata->with_hyphens = opts->with_hyphens;
    mdata->upcase = opts->upcase;

    r = sol_util_uuid_gen(mdata->upcase, mdata->with_hyphens, id);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_UUID__OUT__OUT, id);
}

int
string_uuid_gen(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    char id[37] = { 0 };
    struct string_uuid_data *mdata = data;

    r = sol_util_uuid_gen(mdata->upcase, mdata->with_hyphens, id);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_UUID__OUT__OUT, id);
}

static int
fix_case_and_hyphens(bool upcase,
    bool with_hyphens,
    char id[37])
{
    bool has_hyphens = false;

    int (*case_func)(int c);
    unsigned i;
    int r = 0;
    char *p;

    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice slice = SOL_STR_SLICE_STR(id, strlen(id));

    r = sol_buffer_set_slice(&buf, slice);
    SOL_INT_CHECK(r, < 0, r);

    if (upcase)
        case_func = toupper;
    else
        case_func = tolower;

    for (i = 0, p = sol_buffer_at(&buf, i);
        *p; i++, p = sol_buffer_at(&buf, i)) {
        if (*p == '-')
            has_hyphens = true;
        *p = case_func(*p);
    }

    if (with_hyphens && !has_hyphens) {
        const int hyphens_pos[] = { 8, 13, 18, 23 };
        struct sol_str_slice hyphen = SOL_STR_SLICE_LITERAL("-");

        for (i = 0; i < ARRAY_SIZE(hyphens_pos); i++) {
            r = sol_buffer_insert_slice(&buf, hyphens_pos[i], hyphen);
            if (r < 0) {
                sol_buffer_fini(&buf);
                return r;
            }
        }
    }

    p = sol_buffer_at(&buf, buf.used > 1 ? buf.used - 1 : 0);
    if (*p == '\n')
        *p = '\0';

    memcpy(id, buf.data, buf.used);
    sol_buffer_fini(&buf);

    return r;
}

int
string_machine_id_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    char id[37] = { 0 };
    struct string_machine_id_data *mdata = data;
    const struct sol_flow_node_type_string_machine_id_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_STRING_MACHINE_ID_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_string_machine_id_options *)options;

    mdata->node = node;
    mdata->with_hyphens = opts->with_hyphens;
    mdata->upcase = opts->upcase;

    r = sol_util_get_machine_id(id);
    SOL_INT_CHECK(r, < 0, r);

    fix_case_and_hyphens(mdata->upcase, mdata->with_hyphens, id);

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_STRING_MACHINE_ID__OUT__OUT, id);
}
