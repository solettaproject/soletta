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
    const uint8_t *mem;
    const struct sol_flow_packet_member_description *desc;
    bool has_members, single_member;

    fputc('<', stdout);
    if (type->name)
        fputs(type->name, stdout);
    else
        fprintf(stdout, "%p", type);

    if (type->data_size == 0)
        goto end;

    fputc(' ', stdout);

    mem = sol_flow_packet_get_memory(packet);
    desc = type->members;
    if (!desc) {
        fprintf(stdout, "packet %p has no members description", packet);
        goto end;
    }

    has_members = (desc[0].name != NULL && desc[0].data_type != NULL);
    single_member =  has_members &&
        (desc[1].name == NULL || desc[1].data_type == NULL);

    for (; desc->name != NULL && desc->data_type != NULL; desc++) {
        const char *data_type = desc->data_type;
        const void *member_mem;
        const uint16_t member_size = desc->size;
        if (unlikely(desc->offset + member_size > type->data_size))
            continue;
        if (likely(!single_member)) {
            if (desc > type->members)
                fputc('|', stdout);
            fprintf(stdout, "%s:", desc->name);
        }

        member_mem = mem + desc->offset;
#define CHECK_SIZE(expected) (member_size == sizeof(expected))
#define CHECK_CTYPE(expected) CHECK_SIZE(expected) && streq(data_type, #expected)
        if (CHECK_CTYPE(bool)) {
            const bool *v = member_mem;
            fputs(*v ? "true" : "false", stdout);
        } else if (CHECK_SIZE(char *) && streq(data_type, "string")) {
            const char *const *v = member_mem;
            if (*v)
                fprintf(stdout, "\"%s\"", *v);
            else
                fputs("(null)", stdout);
        } else if (CHECK_CTYPE(int)) {
            const int *v = member_mem;
            fprintf(stdout, "%d", *v);
        } else if (CHECK_CTYPE(unsigned)) {
            const unsigned *v = member_mem;
            fprintf(stdout, "%u", *v);
        } else if (CHECK_CTYPE(int8_t)) {
            const int8_t *v = member_mem;
            fprintf(stdout, "%" PRId8 "", *v);
        } else if (CHECK_CTYPE(int16_t)) {
            const int16_t *v = member_mem;
            fprintf(stdout, "%" PRId16 "", *v);
        } else if (CHECK_CTYPE(int32_t)) {
            const int32_t *v = member_mem;
            fprintf(stdout, "%" PRId32 "", *v);
        } else if (CHECK_CTYPE(int64_t)) {
            const int64_t *v = member_mem;
            fprintf(stdout, "%" PRId64 "", *v);
        } else if (CHECK_CTYPE(uint8_t)) {
            const uint8_t *v = member_mem;
            fprintf(stdout, "%" PRIu8 "", *v);
        } else if (CHECK_CTYPE(uint16_t)) {
            const uint16_t *v = member_mem;
            fprintf(stdout, "%" PRIu16 "", *v);
        } else if (CHECK_CTYPE(uint32_t)) {
            const uint32_t *v = member_mem;
            fprintf(stdout, "%" PRIu32 "", *v);
        } else if (CHECK_CTYPE(uint64_t)) {
            const uint64_t *v = member_mem;
            fprintf(stdout, "%" PRIu64 "", *v);
        } else if (CHECK_CTYPE(double)) {
            const double *v = member_mem;
            fprintf(stdout, "%f", *v);
        } else if (CHECK_CTYPE(size_t)) {
            const size_t *v = member_mem;
            fprintf(stdout, "%zu", *v);
        } else if (CHECK_CTYPE(ssize_t)) {
            const ssize_t *v = member_mem;
            fprintf(stdout, "%zd", *v);
        } else {
            size_t len = strlen(data_type);
            if (CHECK_SIZE(void *) && len > 0 && data_type[len - 1] == '*') {
                const void *const *v = member_mem;
                fprintf(stdout, "(%s)%p", data_type, *v);
            } else {
                fprintf(stdout, "[unsupported](%s*)%p",
                    data_type, member_mem);
            }
        }
#undef CHECK_CTYPE
#undef CHECK_SIZE
    }

end:
    fputc('>', stdout);
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
