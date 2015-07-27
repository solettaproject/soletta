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

#include <stdio.h>
#include "sol-flow.h"
#include "sol-flow-inspector.h"
#include "sol-util.h"

struct timespec start;

static void
inspector_prefix(const char *prefix, const struct sol_flow_node *node)
{
    struct timespec now = sol_util_timespec_get_current();
    struct timespec diff;

    sol_util_timespec_sub(&now, &start, &diff);
    fprintf(stdout, "DEBUG:%ld.%010ld:%s:", diff.tv_sec, diff.tv_nsec, prefix);
    while ((node = sol_flow_node_get_parent(node)) != NULL)
        fputc('~', stdout);
    fputc(' ', stdout);
}

static const char *
inspector_get_node_typename(const struct sol_flow_node *node)
{
    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);

    if (!type)
        return NULL;
    return type->description ? type->description->name : NULL;
}

static void
inspector_show_node_id(const struct sol_flow_node *node)
{
    const char *id = sol_flow_node_get_id(node);

    if (id && *id)
        fputs(id, stdout);
    else
        fprintf(stdout, "%p", node);
}

static void
inspector_print_port_name(uint16_t port, const struct sol_flow_port_description *desc)
{
    if (desc->array_size == 0) {
        fputs(desc->name, stdout);
        return;
    }
    fprintf(stdout, "%s[%d]", desc->name, port - desc->base_port_idx);
}

static void
inspector_show_in_port(const struct sol_flow_node *node, uint16_t port_idx)
{
    const struct sol_flow_port_description *port;

    port = sol_flow_node_get_port_in_description(sol_flow_node_get_type(node), port_idx);
    if (port) {
        if (port->name) {
            inspector_print_port_name(port_idx, port);
            if (port->data_type)
                fprintf(stdout, "(%s)", port->data_type);
            return;
        }
    }
    fprintf(stdout, "%hu", port_idx);
}

static void
inspector_show_out_port(const struct sol_flow_node *node, uint16_t port_idx)
{
    const struct sol_flow_port_description *port;

    if (port_idx == SOL_FLOW_NODE_PORT_ERROR) {
        fputs(SOL_FLOW_NODE_PORT_ERROR_NAME, stdout);
        return;
    }

    port = sol_flow_node_get_port_out_description(sol_flow_node_get_type(node), port_idx);
    if (port) {
        if (port->name) {
            inspector_print_port_name(port_idx, port);
            if (port->data_type)
                fprintf(stdout, "(%s)", port->data_type);
            return;
        }
    }
    fprintf(stdout, "%hu", port_idx);
}

static void
inspector_show_packet(const struct sol_flow_packet *packet)
{
    const struct sol_flow_packet_type *type = sol_flow_packet_get_type(packet);

    if (type == SOL_FLOW_PACKET_TYPE_EMPTY) {
        fputs("<empty>", stdout);
        return;
    } else if (type == SOL_FLOW_PACKET_TYPE_ANY) {
        fputs("<any>", stdout);
        return;
    } else if (type == SOL_FLOW_PACKET_TYPE_ERROR) {
        int code;
        const char *msg;
        if (sol_flow_packet_get_error(packet, &code, &msg) == 0) {
            fprintf(stdout, "<error:%d \"%s\">", code, msg ? msg : "");
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_BOOLEAN) {
        bool v;
        if (sol_flow_packet_get_boolean(packet, &v) == 0) {
            fprintf(stdout, "<%s>", v ? "true" : "false");
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_BYTE) {
        unsigned char v;
        if (sol_flow_packet_get_byte(packet, &v) == 0) {
            fprintf(stdout, "<%#x>", v);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_IRANGE) {
        struct sol_irange v;
        if (sol_flow_packet_get_irange(packet, &v) == 0) {
            fprintf(stdout, "<val:%d|min:%d|max:%d|step:%d>",
                v.val, v.min, v.max, v.step);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_DRANGE) {
        struct sol_drange v;
        if (sol_flow_packet_get_drange(packet, &v) == 0) {
            fprintf(stdout, "<val:%g|min:%g|max:%g|step:%g>",
                v.val, v.min, v.max, v.step);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_STRING) {
        const char *v;
        if (sol_flow_packet_get_string(packet, &v) == 0) {
            fprintf(stdout, "<\"%s\">", v);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_BLOB) {
        struct sol_blob *v;
        if (sol_flow_packet_get_blob(packet, &v) == 0) {
            fprintf(stdout, "<mem=%p|size=%zd|refcnt=%hu|type=%p|parent=%p>",
                v->mem, v->size, v->refcnt, v->type, v->parent);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_RGB) {
        struct sol_rgb v;
        if (sol_flow_packet_get_rgb(packet, &v) == 0) {
            fprintf(stdout,
                "<red=%u|green=%u|blue=%u"
                "|red_max=%u|green_max=%u|blue_max=%u>",
                v.red, v.green, v.blue,
                v.red_max, v.green_max, v.blue_max);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR) {
        struct sol_direction_vector v;
        if (sol_flow_packet_get_direction_vector(packet, &v) == 0) {
            fprintf(stdout,
                "<x=%g|y=%g|z=%g|min=%g|max=%g>",
                v.x, v.y, v.z, v.min, v.max);
            return;
        }
    } else if (type == SOL_FLOW_PACKET_TYPE_LOCATION) {
        struct sol_location v;
        if (sol_flow_packet_get_location(packet, &v) == 0) {
            fprintf(stdout, "<lat=%g|lon=%g|alt=%g>", v.lat, v.lon, v.alt);
            return;
        }
    }

    fputs("<?>", stdout);
}

static void
inspector_did_open_node(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node, const struct sol_flow_node_options *options)
{
    const char *typename = inspector_get_node_typename(node);
    const struct sol_flow_node_type *type = sol_flow_node_get_type(node);
    const struct sol_flow_node_type_description *desc;
    const struct sol_flow_node_options_description *opt_desc;

    if (!type)
        return;
    desc = type->description;
    opt_desc = desc ? desc->options : NULL;

    inspector_prefix("+node", node);
    inspector_show_node_id(node);

    if (!typename)
        goto end;

    fprintf(stdout, "(%s", typename);

    if (opt_desc && opt_desc->members) {
        const struct sol_flow_node_options_member_description *itr;
        fputs(":", stdout);
        for (itr = opt_desc->members; itr->name != NULL; itr++) {
            const void *mem = (const uint8_t *)options + itr->offset;

            if (itr > opt_desc->members)
                fputs(",", stdout);

            fprintf(stdout, "%s=", itr->name);
            if (streq(itr->data_type, "string")) {
                const char *const *s = mem;
                fprintf(stdout, "\"%s\"", *s);
            } else if (streq(itr->data_type, "boolean")) {
                const bool *b = mem;
                fputs(*b ? "true" : "false", stdout);
            } else if (streq(itr->data_type, "byte")) {
                const uint8_t *b = mem;
                fprintf(stdout, "%#x", *b);
            } else if (streq(itr->data_type, "int")) {
                const struct sol_irange *i = mem;
                fprintf(stdout, "val:%d|min:%d|max:%d|step:%d",
                    i->val, i->min, i->max, i->step);
            } else if (streq(itr->data_type, "float")) {
                const struct sol_drange *d = mem;
                fprintf(stdout, "val:%g|min:%g|max:%g|step:%g",
                    d->val, d->min, d->max, d->step);
            } else {
                fputs("???", stdout);
            }
        }
    }

    fputc(')', stdout);

end:
    fputc('\n', stdout);
}

static void
inspector_will_close_node(const struct sol_flow_inspector *inspector, const struct sol_flow_node *node)
{
    inspector_prefix("-node", node);
    inspector_show_node_id(node);
    fputc('\n', stdout);
}

static void
inspector_did_connect_port(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id)
{
    inspector_prefix("+conn", src_node);
    inspector_show_node_id(src_node);
    fputc(' ', stdout);
    inspector_show_out_port(src_node, src_port);
    fprintf(stdout, " %hu->%hu ", src_conn_id, dst_conn_id);
    inspector_show_in_port(dst_node, dst_port);
    fputc(' ', stdout);
    inspector_show_node_id(dst_node);
    fputc('\n', stdout);
}

static void
inspector_will_disconnect_port(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id)
{
    inspector_prefix("-conn", src_node);
    inspector_show_node_id(src_node);
    fputc(' ', stdout);
    inspector_show_out_port(src_node, src_port);
    fprintf(stdout, " %hu->%hu ", src_conn_id, dst_conn_id);
    inspector_show_in_port(dst_node, dst_port);
    fputc(' ', stdout);
    inspector_show_node_id(dst_node);
    fputc('\n', stdout);
}

static void
inspector_will_send_packet(const struct sol_flow_inspector *inspector, const struct sol_flow_node *src_node, uint16_t src_port, const struct sol_flow_packet *packet)
{
    inspector_prefix(">send", src_node);
    inspector_show_node_id(src_node);
    fputc(' ', stdout);
    inspector_show_out_port(src_node, src_port);
    fputs(" -> ", stdout);
    inspector_show_packet(packet);
    fputc('\n', stdout);
}

static void
inspector_will_deliver_packet(const struct sol_flow_inspector *inspector, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id, const struct sol_flow_packet *packet)
{
    inspector_prefix("<recv", dst_node);
    inspector_show_packet(packet);
    fprintf(stdout, " ->%hu ", dst_conn_id);
    inspector_show_in_port(dst_node, dst_port);
    fputc(' ', stdout);
    inspector_show_node_id(dst_node);
    fputc('\n', stdout);
}

static const struct sol_flow_inspector inspector = {
    .api_version = SOL_FLOW_INSPECTOR_API_VERSION,
    .did_open_node = inspector_did_open_node,
    .will_close_node = inspector_will_close_node,
    .did_connect_port = inspector_did_connect_port,
    .will_disconnect_port = inspector_will_disconnect_port,
    .will_send_packet = inspector_will_send_packet,
    .will_deliver_packet = inspector_will_deliver_packet,
};

void inspector_init(void);

void
inspector_init(void)
{
    start = sol_util_timespec_get_current();
    sol_flow_set_inspector(&inspector);
}
