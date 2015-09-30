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

#include "sol-flow/console.h"
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

SOL_ATTR_PRINTF(5, 6) static void
console_output(struct console_data *mdata, const char *prefix, const char *suffix, char separator, const char *fmt, ...)
{
    va_list ap;

    if (prefix)
        fputs(mdata->prefix, mdata->fp);

    va_start(ap, fmt);
    vfprintf(mdata->fp, fmt, ap);
    va_end(ap);

    if (suffix)
        fprintf(mdata->fp, "%s%c", mdata->suffix, separator);
    else
        fprintf(mdata->fp, "%c", separator);
}

static int
print_packet_content(const struct sol_flow_packet *packet, struct sol_flow_node *node, struct console_data *mdata,
    const char *prefix, const char *suffix, char separator)
{
    const struct sol_flow_packet_type *packet_type = sol_flow_packet_get_type(packet);

    if (packet_type == SOL_FLOW_PACKET_TYPE_EMPTY) {
        console_output(mdata, prefix, suffix, separator, "(empty)");
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN) {
        bool value;
        int r = sol_flow_packet_get_boolean(packet, &value);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "%s (boolean)", value ? "true" : "false");
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE) {
        unsigned char value;
        int r = sol_flow_packet_get_byte(packet, &value);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "#%02x (byte)", value);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t val;
        int r = sol_flow_packet_get_irange_value(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "%" PRId32 " (integer range)", val);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE) {
        double val;
        int r = sol_flow_packet_get_drange_value(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "%f (float range)", val);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_RGB) {
        uint32_t red, green, blue;
        int r = sol_flow_packet_get_rgb_components(packet, &red, &green, &blue);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "(%" PRIu32 ", %" PRIu32 ", %" PRIu32 ") (rgb)", red, green, blue);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR) {
        double x, y, z;
        int r = sol_flow_packet_get_direction_vector_components(packet, &x, &y, &z);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "(%lf, %lf, %lf) (direction-vector)", x, y, z);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_LOCATION) {
        struct sol_location location;
        int r = sol_flow_packet_get_location(packet, &location);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "latitude=%g, longitude=%g altitude=%g (location)",
            location.lat, location.lon, location.alt);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_STRING) {
        const char *val;

        int r = sol_flow_packet_get_string(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        console_output(mdata, prefix, suffix, separator, "%s (string)", val);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP) {
        struct timespec timestamp;
        struct tm cur_time;
        char buf[32];
        int r;

        r = sol_flow_packet_get_timestamp(packet, &timestamp);
        SOL_INT_CHECK(r, < 0, r);

        tzset();
        if (!gmtime_r(&timestamp.tv_sec, &cur_time)) {
            SOL_WRN("Failed to convert time");
            return -EINVAL;
        }

        r = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &cur_time);
        SOL_INT_CHECK(r, == 0, -EINVAL);

        console_output(mdata, prefix, suffix, separator, "%s (timestamp)", buf);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_BLOB) {
        struct sol_blob *val;
        const char *buf, *bufend;

        int r = sol_flow_packet_get_blob(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        fprintf(mdata->fp, "%stype=%p, parent=%p, size=%zd, refcnt=%hu, mem=%p {",
            mdata->prefix, val->type, val->parent, val->size, val->refcnt, val->mem);

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

        fprintf(mdata->fp, "} (blob)%s%c", mdata->suffix, separator);
    } else if (packet_type == SOL_FLOW_PACKET_TYPE_ERROR) {
        int code;
        const char *msg;
        int r = sol_flow_packet_get_error(packet, &code, &msg);
        SOL_INT_CHECK(r, < 0, r);
        fprintf(mdata->fp, "%s#%02x (error)%s - %s%c",
            mdata->prefix, code, mdata->suffix, msg ? : "", separator);
    } else {
        sol_flow_send_error_packet(node, -EINVAL, "Unsupported packet=%p type=%p (%s)",
            packet, packet_type, packet_type->name);
        return -EINVAL;
    }
    return 0;
}

static int
console_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct console_data *mdata = data;
    const struct sol_flow_packet_type *packet_type = sol_flow_packet_get_type(packet);

    if (sol_flow_packet_is_composed_type(packet_type)) {
        uint16_t len, i;
        struct sol_flow_packet **packets;

        sol_flow_packet_get_composed_members_len(packet_type, &len);
        packets = malloc(len * sizeof(struct sol_flow_packet *));
        SOL_NULL_CHECK(packets, -ENOMEM);
        sol_flow_packet_get(packet, packets);
        console_output(mdata, mdata->prefix, NULL, 0, "Composed packet {");
        for (i = 0; i < len; i++) {
            char sep = ',';
            if (i == len - 1)
                sep = 0;
            r = print_packet_content(packets[i], node, mdata, NULL, NULL, sep);
            SOL_INT_CHECK(r, < 0, r);
        }
        console_output(mdata, NULL, mdata->suffix, '\n',  "} (%s)", packet_type->name);
        free(packets);
    } else {
        r = print_packet_content(packet, node, mdata, mdata->prefix, mdata->suffix,  '\n');
        SOL_INT_CHECK(r, < 0, r);
    }

    if (mdata->flush)
        fflush(mdata->fp);

    return 0;
}

static const char empty_string[] = "";

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

    if (!mdata->suffix) {
        mdata->suffix = (char *)empty_string;
    }

end:
    return 0;
}

static void
console_close(struct sol_flow_node *node, void *data)
{
    struct console_data *mdata = data;

    free(mdata->prefix);
    if (mdata->suffix != empty_string)
        free(mdata->suffix);
}

#include "console-gen.c"
