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
    char *errmsg;
    int r;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_EMPTY) {
        fprintf(mdata->fp, "%s(empty)%s\n",
            mdata->prefix ? mdata->prefix : "",
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOLEAN) {
        bool value;
        r = sol_flow_packet_get_boolean(packet, &value);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s%s (boolean)%s\n",
            mdata->prefix ? mdata->prefix : "",
            value ? "true" : "false",
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BYTE) {
        unsigned char value;
        r = sol_flow_packet_get_byte(packet, &value);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s#%02x (byte)%s\n",
            mdata->prefix ? mdata->prefix : "",
            value,
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t val;
        r = sol_flow_packet_get_irange_value(packet, &val);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s%" PRId32 " (integer range)%s\n",
            mdata->prefix ? mdata->prefix : "",
            val,
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_DRANGE) {
        double val;
        r = sol_flow_packet_get_drange_value(packet, &val);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s%f (float range)%s\n",
            mdata->prefix ? mdata->prefix : "",
            val,
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_RGB) {
        uint32_t red, green, blue;
        r = sol_flow_packet_get_rgb_components(packet, &red, &green, &blue);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s(%d, %d, %d) (rgb)%s\n",
            mdata->prefix ? mdata->prefix : "",
            red, green, blue,
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_STRING) {
        const char *val;

        r = sol_flow_packet_get_string(packet, &val);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s%s (string)%s\n",
            mdata->prefix ? mdata->prefix : "",
            val,
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BLOB) {
        struct sol_blob *val;
        const char *buf, *bufend;

        int r = sol_flow_packet_get_blob(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        fprintf(mdata->fp, "%stype=%p, parent=%p, size=%zd, refcnt=%hu, mem=%p {",
            mdata->prefix ? mdata->prefix : "",
            val->type, val->parent, val->size, val->refcnt, val->mem);

        buf = val->mem;
        bufend = buf + val->size;
        for (; buf < bufend; buf++) {
            if (isprint(*buf))
                fprintf(mdata->fp, "%#x(%c)", *buf, *buf);
            else
                fprintf(mdata->fp, "%#x", *buf);
            if (buf + 1 < bufend)
                fputs(", ", mdata->fp);
        }

        fprintf(mdata->fp, "} (blob)%s\n",
            mdata->suffix ? mdata->suffix : "");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_ERROR) {
        int code;
        const char *msg;
        r = sol_flow_packet_get_error(packet, &code, &msg);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        fprintf(mdata->fp, "%s#%02x (error)%s - %s\n",
            mdata->prefix ? mdata->prefix : "",
            code,
            mdata->suffix ? mdata->suffix : "",
            msg ? : "");
    } else {
        asprintf(&errmsg, "Unsupported packet=%p type=%p (%s). %s",
            packet, sol_flow_packet_get_type(packet), sol_flow_packet_get_type(packet)->name,
            sol_util_strerrora(-EINVAL));
        r = -EINVAL;
        goto error;
    }

    if (mdata->flush)
        fflush(mdata->fp);

    return 0;

error:
    if (errmsg) {
        sol_flow_send_error_packet(node, -r, errmsg);
        free(errmsg);
    } else {
        sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    }
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
