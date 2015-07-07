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

#include "console-gen.h"
#include "sol-flow-internal.h"
#include "sol-types.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

struct console_data {
    FILE *fp;
    char *prefix;
    char *suffix;
    bool flush;
};

static int
console_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct console_data *mdata = data;
    const struct sol_flow_packet_type *type = sol_flow_packet_get_type(packet);
    const uint8_t *mem;
    const struct sol_flow_packet_member_description *desc;
    bool has_members, single_member;

    SOL_NULL_CHECK(type, -EINVAL);

    if (mdata->prefix)
        fputs(mdata->prefix, mdata->fp);

    if (type->data_size == 0)
        goto end;

    mem = sol_flow_packet_get_memory(packet);
    desc = type->members;
    if (!desc) {
        fprintf(mdata->fp, "<packet %p has no members description>", packet);
        goto end;
    }

    has_members = (desc[0].name != NULL && desc[0].data_type != NULL);
    single_member =  has_members &&
        (desc[1].name == NULL || desc[1].data_type == NULL);
    if (has_members && !single_member) {
        if (type->data_type)
            fprintf(mdata->fp, "(%s)", type->data_type);
        fputc('{', mdata->fp);
    }

    for (; desc->name != NULL && desc->data_type != NULL; desc++) {
        const char *data_type = desc->data_type;
        const void *member_mem;
        const uint16_t member_size = desc->size;
        if (unlikely(desc->offset + member_size > type->data_size)) {
            SOL_WRN("packet type %p (%s) data_size=%hu smaller than member "
                "'%s' offset=%hu, size=%hu. Member skipped.",
                type, type->name ? type->name : "",
                type->data_size,
                desc->name, desc->offset, member_size);
            continue;
        }
        if (likely(!single_member)) {
            if (desc > type->members)
                fputs(", ", mdata->fp);
            fprintf(mdata->fp, ".%s=", desc->name);
        }

        member_mem = mem + desc->offset;
#define CHECK_SIZE(expected) (member_size == sizeof(expected))
#define CHECK_CTYPE(expected) CHECK_SIZE(expected) && streq(data_type, #expected)
        if (CHECK_CTYPE(bool)) {
            const bool *v = member_mem;
            fputs(*v ? "true" : "false", mdata->fp);
        } else if (CHECK_SIZE(char *) && streq(data_type, "string")) {
            const char *const *v = member_mem;
            if (*v)
                fprintf(mdata->fp, "\"%s\"", *v);
            else
                fputs("(null)", mdata->fp);
        } else if (CHECK_CTYPE(int)) {
            const int *v = member_mem;
            fprintf(mdata->fp, "%d", *v);
        } else if (CHECK_CTYPE(unsigned)) {
            const unsigned *v = member_mem;
            fprintf(mdata->fp, "%u", *v);
        } else if (CHECK_CTYPE(int8_t)) {
            const int8_t *v = member_mem;
            fprintf(mdata->fp, "%" PRId8 "", *v);
        } else if (CHECK_CTYPE(int16_t)) {
            const int16_t *v = member_mem;
            fprintf(mdata->fp, "%" PRId16 "", *v);
        } else if (CHECK_CTYPE(int32_t)) {
            const int32_t *v = member_mem;
            fprintf(mdata->fp, "%" PRId32 "", *v);
        } else if (CHECK_CTYPE(int64_t)) {
            const int64_t *v = member_mem;
            fprintf(mdata->fp, "%" PRId64 "", *v);
        } else if (CHECK_CTYPE(uint8_t)) {
            const uint8_t *v = member_mem;
            fprintf(mdata->fp, "%" PRIu8 "", *v);
        } else if (CHECK_CTYPE(uint16_t)) {
            const uint16_t *v = member_mem;
            fprintf(mdata->fp, "%" PRIu16 "", *v);
        } else if (CHECK_CTYPE(uint32_t)) {
            const uint32_t *v = member_mem;
            fprintf(mdata->fp, "%" PRIu32 "", *v);
        } else if (CHECK_CTYPE(uint64_t)) {
            const uint64_t *v = member_mem;
            fprintf(mdata->fp, "%" PRIu64 "", *v);
        } else if (CHECK_CTYPE(double)) {
            const double *v = member_mem;
            fprintf(mdata->fp, "%f", *v);
        } else if (CHECK_CTYPE(size_t)) {
            const size_t *v = member_mem;
            fprintf(mdata->fp, "%zu", *v);
        } else if (CHECK_CTYPE(ssize_t)) {
            const ssize_t *v = member_mem;
            fprintf(mdata->fp, "%zd", *v);
        } else {
            size_t len = strlen(data_type);
            if (CHECK_SIZE(void *) && len > 0 && data_type[len - 1] == '*') {
                const void *const *v = member_mem;
                fprintf(mdata->fp, "(%s)%p", data_type, *v);
            } else {
                fprintf(mdata->fp, "[unsupported](%s*)%p",
                    data_type, member_mem);
            }
        }
#undef CHECK_CTYPE
#undef CHECK_SIZE
    }

    if (has_members && !single_member)
        fputc('}', mdata->fp);

end:
    if (type->name)
        fprintf(mdata->fp, "(%s)", type->name);
    else
        fprintf(mdata->fp, "(%p)", type);

    if (mdata->suffix)
        fputs(mdata->suffix, mdata->fp);

    fputc('\n', mdata->fp);

    if (mdata->flush)
        fflush(mdata->fp);

    return 0;
}

static int
console_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct console_data *mdata = data;

    if (!options)
        mdata->fp = stderr;
    else {
        const struct sol_flow_node_type_console_options *opts = (const struct sol_flow_node_type_console_options *)options;
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_CONSOLE_OPTIONS_API_VERSION, -EINVAL);
        mdata->fp = opts->output_on_stdout ? stdout : stderr;
        mdata->prefix = opts->prefix ? strdup(opts->prefix) : NULL;
        mdata->suffix = opts->suffix ? strdup(opts->suffix) : NULL;
        mdata->flush = opts->flush;
    }

    if (!mdata->prefix) {
        char buf[512];
        int r;

        r = snprintf(buf, sizeof(buf), "%s ", sol_flow_node_get_id(node));
        SOL_INT_CHECK_GOTO(r, >= (int)sizeof(buf), end);
        SOL_INT_CHECK_GOTO(r, < 0, end);
        mdata->prefix = strdup(buf);
    }

end:
    return 0;
}

static void
console_close(struct sol_flow_node *node, void *data)
{
    struct console_data *mdata = data;

    free(mdata->prefix);
    free(mdata->suffix);
}

#include "console-gen.c"
