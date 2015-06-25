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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sol-flow-internal.h"
#include "sol-list.h"
#include "sol-mainloop.h"
#include "sol-util.h"

struct node_info {
    uint16_t first_conn_idx;

    /* TODO: check if we can move this to be per-type instead of per-node. */
    uint16_t ports_count_in;
    uint16_t ports_count_out;
};

struct conn_info {
    uint16_t out_conn_id;
    uint16_t in_conn_id;
};

struct flow_static_type {
    struct sol_flow_node_container_type base;

    const struct sol_flow_static_node_spec *node_specs;
    const struct sol_flow_static_conn_spec *conn_specs;

    struct node_info *node_infos;
    struct conn_info *conn_infos;

    unsigned int node_storage_size;

    uint16_t node_count;
    uint16_t conn_count;

    const struct sol_flow_static_port_spec *exported_in_specs;
    const struct sol_flow_static_port_spec *exported_out_specs;

    void (*child_opts_set)(uint16_t child_index,
        const struct sol_flow_node_options *opts,
        struct sol_flow_node_options *child_opts);

    struct sol_flow_port_type_in *ports_in;
    struct sol_flow_port_type_out *ports_out;

    uint16_t *ports_in_base_conn_id;
    uint16_t *ports_out_base_conn_id;

    uint16_t ports_in_count;
    uint16_t ports_out_count;

    /* This type was created for a single node, so when the node goes
     * down, the type will be finalized. */
    bool owned_by_node;
};

struct flow_static_data {
    struct sol_flow_node **nodes;
    void *node_storage;
    struct sol_timeout *delay_send;
    struct sol_list delayed_packets;
};

struct delayed_packet {
    struct sol_list list;
    struct sol_flow_packet *packet;
    uint16_t source_idx;
    uint16_t source_port_idx;
};

static int
dispatch_connect_out(struct sol_flow_node *node, uint16_t port, uint16_t conn_id,
    const struct sol_flow_port_type_out *port_type)
{
    SOL_FLOW_PORT_TYPE_OUT_API_CHECK(port_type,
        SOL_FLOW_PORT_TYPE_OUT_API_VERSION, 0);

    if (port_type->connect)
        return port_type->connect(node, node->data, port, conn_id);
    return 0;
}

static int
dispatch_connect_in(struct sol_flow_node *node, uint16_t port, uint16_t conn_id,
    const struct sol_flow_port_type_in *port_type)
{
    SOL_FLOW_PORT_TYPE_IN_API_CHECK(port_type, SOL_FLOW_PORT_TYPE_IN_API_VERSION, 0);

    if (port_type->connect)
        return port_type->connect(node, node->data, port, conn_id);
    return 0;
}

static int
dispatch_disconnect_out(struct sol_flow_node *node, uint16_t port, uint16_t conn_id, const struct sol_flow_port_type_out *port_type)
{
    if (port_type->disconnect)
        return port_type->disconnect(node, node->data, port, conn_id);
    return 0;
}

static int
dispatch_disconnect_in(struct sol_flow_node *node, uint16_t port, uint16_t conn_id, const struct sol_flow_port_type_in *port_type)
{
    if (port_type->disconnect)
        return port_type->disconnect(node, node->data, port, conn_id);
    return 0;
}

static int
dispatch_process(struct sol_flow_node *node, uint16_t port, uint16_t conn_id, const struct sol_flow_port_type_in *port_type, const struct sol_flow_packet *packet)
{
    SOL_FLOW_PORT_TYPE_IN_API_CHECK(port_type, SOL_FLOW_PORT_TYPE_IN_API_VERSION, 0);

    inspector_will_deliver_packet(node, port, conn_id, packet);
    if (port_type->process)
        return port_type->process(node, node->data, port, conn_id, packet);
    return 0;
}

static bool
match_packets(const struct sol_flow_packet_type *a, const struct sol_flow_packet_type *b)
{
    if (a == SOL_FLOW_PACKET_TYPE_ANY || b == SOL_FLOW_PACKET_TYPE_ANY)
        return true;

    return a == b;
}

#define CONNECT_NODES_WRN(spec, src_id, dst_id, reason, ...)            \
    SOL_WRN("Invalid connection specification: "                         \
    "{ .src_id=%s, .src=%hu, .src_port=%hu, .dst_id=%s .dst=%hu, .dst_port=%hu }: " # reason, \
    src_id, spec->src, spec->src_port,                           \
    dst_id, spec->dst, spec->dst_port, ## __VA_ARGS__)

static int
connect_nodes(struct flow_static_type *type, struct flow_static_data *fsd)
{
    const struct sol_flow_static_conn_spec *spec;
    int i, r;

    for (i = 0, spec = type->conn_specs; i < type->conn_count; i++, spec++) {
        const struct sol_flow_port_type_out *src_port_type;
        const struct sol_flow_port_type_in *dst_port_type;
        struct sol_flow_node *src, *dst;
        struct conn_info *ci;

        src = fsd->nodes[spec->src];
        dst = fsd->nodes[spec->dst];
        ci = &type->conn_infos[i];

        src_port_type = sol_flow_node_type_get_port_out(src->type, spec->src_port);
        dst_port_type = sol_flow_node_type_get_port_in(dst->type, spec->dst_port);


        SOL_FLOW_PORT_TYPE_OUT_API_CHECK(src_port_type, SOL_FLOW_PORT_TYPE_OUT_API_VERSION, -EINVAL);
        SOL_FLOW_PORT_TYPE_IN_API_CHECK(dst_port_type, SOL_FLOW_PORT_TYPE_IN_API_VERSION, -EINVAL);

        if (!src_port_type->packet_type) {
            CONNECT_NODES_WRN(spec, src->id, dst->id, "Invalid packet type for source port");
            return -EINVAL;
        }
        if (!dst_port_type->packet_type) {
            CONNECT_NODES_WRN(spec, src->id, dst->id, "Invalid packet type for destination port");
            return -EINVAL;
        }

        if (!match_packets(src_port_type->packet_type, dst_port_type->packet_type)) {
            CONNECT_NODES_WRN(spec, src->id, dst->id,
                "Error matching source and destination packet types: %s != %s: %s",
                src_port_type->packet_type->name, dst_port_type->packet_type->name,
                sol_util_strerrora(EINVAL));
            r = -EINVAL;
            goto dispatch_error;
        }

        r = dispatch_connect_out(src, spec->src_port, ci->out_conn_id, src_port_type);
        if (r < 0) {
            CONNECT_NODES_WRN(spec, src->id, dst->id, "Error connecting source: %s", sol_util_strerrora(-r));
            goto dispatch_error;
        }

        r = dispatch_connect_in(dst, spec->dst_port, ci->in_conn_id, dst_port_type);
        if (r < 0) {
            CONNECT_NODES_WRN(spec, src->id, dst->id, "Error connecting destination: %s", sol_util_strerrora(-r));
            dispatch_disconnect_out(src, spec->src_port, ci->out_conn_id, src_port_type);
            goto dispatch_error;
        }

        inspector_did_connect_port(src, spec->src_port, ci->out_conn_id,
            dst, spec->dst_port, ci->in_conn_id);
    }
    SOL_DBG("Making %u connections.", i);

    return 0;

dispatch_error:
    /* Dispatch disconnections in reverse order. Skip current failed
     * iteration since it was handled inside the loop. */
    i--;
    spec--;
    for (; i >= 0; i--, spec--) {
        const struct sol_flow_port_type_out *src_port_type;
        const struct sol_flow_port_type_in *dst_port_type;
        struct sol_flow_node *src, *dst;
        struct conn_info *ci;

        src = fsd->nodes[spec->src];
        dst = fsd->nodes[spec->dst];
        ci = &type->conn_infos[i];

        dst_port_type = sol_flow_node_type_get_port_in(dst->type, spec->dst_port);
        dispatch_disconnect_in(dst, spec->dst_port, ci->in_conn_id, dst_port_type);

        src_port_type = sol_flow_node_type_get_port_out(src->type, spec->src_port);
        dispatch_disconnect_out(src, spec->src_port, ci->out_conn_id, src_port_type);
    }

    return r;
}

#undef CONNECT_NODES_WRN

static inline unsigned int
align_to_ptr(unsigned int u)
{
    return (u + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);
}

/* Node storage hold both the memory for struct sol_flow_node and its
 * trailing private data. The size of private data is defined by the
 * node type, and could be smaller than a pointer size. Adding a
 * padding to the size ensures that the next address is aligned for
 * pointer types -- so the next struct sol_flow_node will be
 * aligned. */
static inline unsigned int
calc_node_size(const struct sol_flow_static_node_spec *spec)
{
    return align_to_ptr(sizeof(struct sol_flow_node) + spec->type->data_size);
}

static void
flow_send_do(struct sol_flow_node *flow, struct flow_static_data *fsd, uint16_t src_idx, uint16_t source_out_port_idx, struct sol_flow_packet *packet)
{
    struct flow_static_type *type = (struct flow_static_type *)flow->type;
    const struct sol_flow_static_conn_spec *spec;
    unsigned int i;
    bool is_error_packet, dispatched;

    is_error_packet = sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_ERROR;
    dispatched = false;

    for (i = type->node_infos[src_idx].first_conn_idx, spec = type->conn_specs + i; spec->src == src_idx; spec++, i++) {
        const struct sol_flow_port_type_in *dst_port_type;
        struct sol_flow_node *dst;
        struct conn_info *ci;

        if (spec->src_port != source_out_port_idx)
            continue;

        dst = fsd->nodes[spec->dst];
        dst_port_type = sol_flow_node_type_get_port_in(dst->type, spec->dst_port);
        ci = &type->conn_infos[i];

        dispatch_process(dst, spec->dst_port, ci->in_conn_id, dst_port_type, packet);
        dispatched = true;
    }

    if (type->ports_out_count > 0) {
        const struct sol_flow_static_port_spec *pspec;
        uint16_t exported_out;
        for (pspec = type->exported_out_specs, exported_out = 0; pspec->node < UINT16_MAX; pspec++, exported_out++) {
            if (pspec->node == src_idx && pspec->port == source_out_port_idx) {
                /* Export the packet. Note that ownership of packet
                 * will pass to the send() function. */
                sol_flow_send_packet(flow, exported_out, packet);
                return;
            }
        }
    }

    if (is_error_packet && !dispatched) {
        const char *msg;
        int code;

        if (sol_flow_packet_get_error(packet, &code, &msg) == 0) {
            SOL_WRN("Error packet \'%d (%s)\' sent from \'%s (%p)\' was not handled", code,
                msg, flow->id, flow);
        }
    }

    sol_flow_packet_del(packet);
}

static bool
flow_send_idle(void *data)
{
    struct sol_flow_node *flow = data;
    struct flow_static_data *fsd;
    struct sol_list tmplist;

    fsd = sol_flow_node_get_private_data(flow);
    /* If during packet processing more stuff is sent, we want a new idler
     * to be added, so make sure this pointer is NULL by then */
    fsd->delay_send = NULL;

    sol_list_steal(&fsd->delayed_packets, &tmplist);
    while (!sol_list_is_empty(&tmplist)) {
        struct delayed_packet *dp;
        dp = SOL_LIST_GET_CONTAINER(tmplist.next, struct delayed_packet, list);
        sol_list_remove(&dp->list);
        flow_send_do(flow, fsd, dp->source_idx, dp->source_port_idx, dp->packet);
        free(dp);
    }

    return false;
}

static int
flow_delay_send(struct sol_flow_node *flow, struct flow_static_data *fsd)
{
    if (!fsd->delay_send) {
        /* We want to ensure that all packets will be processed in the
         * main loop iteration immediately following the current one, even
         * when the system is loaded enough that it barely has any idle time,
         * thus a timeout with a 0 value instead of an idler.
         */
        fsd->delay_send = sol_timeout_add(0, flow_send_idle, flow);
        SOL_NULL_CHECK(fsd->delay_send, -ENOMEM);
    }

    return 0;
}

static int
flow_node_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;
    const struct sol_flow_static_node_spec *spec;
    char *node_storage_it;
    int r, i;

    type = (struct flow_static_type *)node->type;
    fsd = data;

    fsd->nodes = calloc(type->node_count, sizeof(struct sol_flow_node *));
    fsd->node_storage = calloc(1, type->node_storage_size);
    if (!fsd->nodes || !fsd->node_storage) {
        r = -ENOMEM;
        goto error_alloc;
    }

    /* Assure flow_send_idle()'s timeout is the first registered, so
     * that timeouts coming from nodes' init/open functions and that
     * may produce packets will always have them delivered */
    r = flow_delay_send(node, fsd);
    SOL_INT_CHECK(r, < 0, r);

    sol_list_init(&fsd->delayed_packets);

    /* Set all pointers before calling nodes methods */
    node_storage_it = fsd->node_storage;
    for (spec = type->node_specs, i = 0; spec->type != NULL; spec++, i++) {
        struct sol_flow_node *child_node = (struct sol_flow_node *)node_storage_it;

        fsd->nodes[i] = child_node;
        child_node->parent_data = INT_TO_PTR(i);
        node_storage_it += calc_node_size(spec);
    }

    for (spec = type->node_specs, i = 0; spec->type != NULL; spec++, i++) {
        struct sol_flow_node *child_node = fsd->nodes[i];
        struct sol_flow_node_options *child_opts;

        child_opts = sol_flow_node_get_options(spec->type, spec->opts);
        if (!child_opts) {
            SOL_WRN("failed to get options for node #%u, type=%p: %s",
                (unsigned)(spec - type->node_specs), spec->type,
                sol_util_strerrora(errno));
        }

        if (type->child_opts_set)
            type->child_opts_set(i, options, child_opts);
        r = sol_flow_node_init(child_node, node, spec->name, spec->type,
            child_opts);
        sol_flow_node_free_options(spec->type, child_opts);
        if (r < 0) {
            SOL_WRN("failed to init node #%u, type=%p, opts=%p: %s",
                (unsigned)(spec - type->node_specs), spec->type, spec->opts,
                sol_util_strerrora(-r));
            goto error_nodes;
        }
    }

    r = connect_nodes(type, fsd);
    if (r < 0)
        goto error_conns;

    return 0;

error_conns:
error_nodes:
    /* Skip the failed index, since it doesn't need fini. */
    for (i--; i >= 0; i--)
        sol_flow_node_fini(fsd->nodes[i]);

error_alloc:
    free(fsd->node_storage);
    free(fsd->nodes);

    return r;
}

static void
teardown_connections(struct flow_static_type *type, struct flow_static_data *fsd)
{
    const struct sol_flow_static_conn_spec *spec;
    int i;

    for (i = type->conn_count - 1, spec = type->conn_specs + i; i >= 0; i--, spec--) {
        const struct sol_flow_port_type_out *src_port_type;
        const struct sol_flow_port_type_in *dst_port_type;
        struct sol_flow_node *src, *dst;
        struct conn_info *ci;

        src = fsd->nodes[spec->src];
        dst = fsd->nodes[spec->dst];
        ci = &type->conn_infos[i];

        src_port_type = sol_flow_node_type_get_port_out(src->type, spec->src_port);
        dst_port_type = sol_flow_node_type_get_port_in(dst->type, spec->dst_port);

        inspector_will_disconnect_port(src, spec->src_port, ci->out_conn_id,
            dst, spec->dst_port, ci->in_conn_id);

        dispatch_disconnect_out(src, spec->src_port, ci->out_conn_id, src_port_type);
        dispatch_disconnect_in(dst, spec->dst_port, ci->in_conn_id, dst_port_type);
    }
}

static void flow_static_type_fini(struct flow_static_type *type);

static void
flow_node_close(struct sol_flow_node *node, void *data)
{
    struct flow_static_type *type = (struct flow_static_type *)node->type;
    struct flow_static_data *fsd = data;
    int i;

    if (fsd->delay_send)
        sol_timeout_del(fsd->delay_send);
    while (!sol_list_is_empty(&fsd->delayed_packets)) {
        struct delayed_packet *dp;
        struct sol_list *itr = fsd->delayed_packets.next;
        dp = SOL_LIST_GET_CONTAINER(itr, struct delayed_packet, list);
        sol_list_remove(itr);
        sol_flow_packet_del(dp->packet);
        free(dp);
    }

    teardown_connections(type, fsd);

    for (i = type->node_count - 1; i >= 0; i--)
        sol_flow_node_fini(fsd->nodes[i]);

    free(fsd->node_storage);
    free(fsd->nodes);

    if (type->owned_by_node)
        sol_flow_static_del_type(&type->base.base);
}

static bool
flow_port_out_is_valid(struct node_info *ninfo, uint16_t port_idx)
{
    if (port_idx == SOL_FLOW_NODE_PORT_ERROR)
        return true;
    SOL_INT_CHECK(port_idx, >= ninfo->ports_count_out, false);
    return true;
}

static bool
flow_port_in_is_valid(struct node_info *ninfo, uint16_t port_idx)
{
    SOL_INT_CHECK(port_idx, >= ninfo->ports_count_in, false);
    return true;
}

static int
flow_send(struct sol_flow_node *flow, struct sol_flow_node *source_node, uint16_t source_out_port_idx, struct sol_flow_packet *packet)
{
    struct flow_static_type *type = (struct flow_static_type *)flow->type;
    struct flow_static_data *fsd;
    struct delayed_packet *dp;
    const struct sol_flow_port_type_out *ptype;
    uint16_t src_idx;
    int r;

    fsd = sol_flow_node_get_private_data(source_node->parent);

    src_idx = PTR_TO_INT(source_node->parent_data);

    ptype = sol_flow_node_type_get_port_out(source_node->type, source_out_port_idx);

    if (!sol_flow_packet_get_type(packet)) {
        SOL_WRN("Invalid packet type for packet");
        return -EINVAL;
    }

    if (!match_packets(ptype->packet_type, sol_flow_packet_get_type(packet))) {
        SOL_WRN("Outgoing packet does not match output type: %s != %s", ptype->packet_type->name,
            sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    if (!flow_port_out_is_valid(&type->node_infos[src_idx], source_out_port_idx)) {
        SOL_WRN("Out port %d is not valid", source_out_port_idx);
        return -EINVAL;
    }

    r = flow_delay_send(flow, fsd);
    SOL_INT_CHECK(r, < 0, r);

    dp = malloc(sizeof(*dp));
    SOL_NULL_CHECK(dp, -ENOMEM);

    dp->packet = packet;
    dp->source_idx = src_idx;
    dp->source_port_idx = source_out_port_idx;
    sol_list_append(&fsd->delayed_packets, &dp->list);

    return 0;
}

static void
flow_get_ports_counts(const struct sol_flow_node_type *type, uint16_t *ports_in_count, uint16_t *ports_out_count)
{
    struct flow_static_type *fst = (struct flow_static_type *)type;

    if (ports_in_count)
        *ports_in_count = fst->ports_in_count;
    if (ports_out_count)
        *ports_out_count = fst->ports_out_count;
}

static const struct sol_flow_port_type_in *
flow_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    struct flow_static_type *fst = (struct flow_static_type *)type;

    return &fst->ports_in[port];
}

static const struct sol_flow_port_type_out *
flow_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    struct flow_static_type *fst = (struct flow_static_type *)type;

    return &fst->ports_out[port];
}

static int
setup_node_specs(struct flow_static_type *type)
{
    const struct sol_flow_static_node_spec *spec;
    unsigned int storage_size = 0;
    uint16_t count = 0, u;

    for (spec = type->node_specs; count < UINT16_MAX && spec->type != NULL; spec++, count++) {
        unsigned int node_size = calc_node_size(spec);
        if (UINT_MAX - node_size < storage_size) {
            SOL_WRN("no memory to fit node size: %u in %u", node_size, storage_size);
            return -ENOMEM;
        }
        storage_size += node_size;
    }

    SOL_INT_CHECK(count, == 0, -EINVAL);
    if (spec->type != NULL) {
        SOL_WRN("too many nodes");
        return -EINVAL;
    }

    type->node_infos = calloc(count, sizeof(struct node_info));
    if (!type->node_infos)
        return -ENOMEM;

    for (u = 0, spec = type->node_specs; u < count; u++, spec++) {
        struct node_info *ni;
        ni = &type->node_infos[u];
        spec->type->get_ports_counts(spec->type, &ni->ports_count_in, &ni->ports_count_out);
    }

    type->node_count = count;
    type->node_storage_size = storage_size;
    return 0;
}

static void
teardown_node_specs(struct flow_static_type *type)
{
    free(type->node_infos);
}

#define INVALID_SPEC_WRN(spec, reason, ...)                     \
    SOL_WRN("Invalid connection specification "                          \
    "{ .src=%hu, .src_port=%hu, .dst=%hu, .dst_port=%hu }: " # reason, \
    spec->src, spec->src_port, spec->dst, spec->dst_port, ## __VA_ARGS__)

static bool
is_valid_spec(struct flow_static_type *type, const struct sol_flow_static_conn_spec *spec)
{
    if (spec->src >= type->node_count) {
        INVALID_SPEC_WRN(spec, "invalid source index (node_count=%hu)", type->node_count);
        return false;
    }

    if (spec->dst >= type->node_count) {
        INVALID_SPEC_WRN(spec, "invalid destination index (node_count=%hu)", type->node_count);
        return false;
    }

    if (!flow_port_out_is_valid(&type->node_infos[spec->src], spec->src_port)) {
        INVALID_SPEC_WRN(spec, "invalid source out port index (ports count out=%hu)",
            type->node_infos[spec->src].ports_count_out);
        return false;
    }

    if (!flow_port_in_is_valid(&type->node_infos[spec->dst], spec->dst_port)) {
        INVALID_SPEC_WRN(spec, "invalid destination in port index (ports count in=%hu)",
            type->node_infos[spec->dst].ports_count_in);
        return false;
    }

    return true;
}

#undef INVALID_SPEC_WRN

static void
setup_conn_ids(struct flow_static_type *type)
{
    uint16_t node, exported_in = 0, exported_out = 0;

    for (node = 0; node < type->node_count; node++) {
        uint16_t port, port_count;
        port_count = type->node_infos[node].ports_count_out;

        for (port = 0; port < port_count; port++) {
            const struct sol_flow_static_conn_spec *spec;
            unsigned int i;
            uint16_t in_conn_id = 0, out_conn_id = 0;

            for (spec = type->conn_specs, i = 0; i < type->conn_count; spec++, i++) {
                if (spec->dst == node && spec->dst_port == port)
                    type->conn_infos[i].in_conn_id = in_conn_id++;
                if (spec->src == node && spec->src_port == port)
                    type->conn_infos[i].out_conn_id = out_conn_id++;
            }

            if (exported_in < type->ports_in_count) {
                const struct sol_flow_static_port_spec *pspec;
                pspec = &type->exported_in_specs[exported_in];
                if (pspec->node == node && pspec->port == port)
                    type->ports_in_base_conn_id[exported_in++] = in_conn_id;
            }

            if (exported_out < type->ports_out_count) {
                const struct sol_flow_static_port_spec *pspec;
                pspec = &type->exported_out_specs[exported_out];
                if (pspec->node == node && pspec->port == port)
                    type->ports_out_base_conn_id[exported_out++] = out_conn_id;
            }
        }
    }
}

static int
setup_conn_specs(struct flow_static_type *type)
{
    const struct sol_flow_static_conn_spec *spec, *prev = NULL;
    uint16_t count = 0;

    for (spec = type->conn_specs; count < UINT16_MAX && spec->src != UINT16_MAX; prev = spec, spec++, count++) {
        if (!is_valid_spec(type, spec))
            return -EINVAL;

        if (!prev || spec->src != prev->src)
            type->node_infos[spec->src].first_conn_idx = count;

        if (!prev)
            continue;

        if (spec->src < prev->src || ((spec->src == prev->src) && (spec->src_port < prev->src_port))) {
            SOL_WRN("Connection specification is not ordered: "
                "src=%hu (previous: %hu), "
                "src_port=%hu (previous: %hu)",
                spec->src, prev->src,
                spec->src_port,
                (spec->src == prev->src) ? prev->src_port : 0);
            return -EINVAL;
        }
    }

    if (spec->src != UINT16_MAX) {
        SOL_WRN("too many connections");
        return -EINVAL;
    }

    if (count > 0) {
        type->conn_infos = calloc(count, sizeof(struct conn_info));
        if (!type->conn_infos)
            return -ENOMEM;
    } else {
        type->conn_infos = NULL;
    }

    type->conn_count = count;

    setup_conn_ids(type);

    return 0;
}

static void
teardown_conn_specs(struct flow_static_type *type)
{
    free(type->conn_infos);
}

static int
validate_port_specs(const struct sol_flow_static_port_spec *specs, uint16_t *count)
{
    const struct sol_flow_static_port_spec *spec, *prev = NULL;
    uint16_t c;

    if (specs == NULL) {
        *count = 0;
        return 0;
    }

    for (c = 0, spec = specs; c < UINT16_MAX && spec->node != UINT16_MAX; prev = spec, c++, spec++) {
        if (!prev)
            continue;
        if (spec->node < prev->node || ((spec->node == prev->node) && (spec->port <= prev->port)))
            return -EINVAL;
    }

    if (c == UINT16_MAX)
        return -E2BIG;

    *count = c;
    return 0;
}

static int
flow_exported_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;
    struct sol_flow_node *child_node;
    uint16_t child_port, child_conn_id;

    type = (struct flow_static_type *)node->type;
    fsd = data;

    child_port = type->exported_in_specs[port].port;
    child_node = fsd->nodes[type->exported_in_specs[port].node];
    child_conn_id = type->ports_in_base_conn_id[port] + conn_id;

    dispatch_process(child_node, child_port, child_conn_id,
        sol_flow_node_type_get_port_in(child_node->type, child_port), packet);

    return 0;
}

static int
flow_exported_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;
    struct sol_flow_node *child_node;
    uint16_t child_port, child_conn_id;

    type = (struct flow_static_type *)node->type;
    fsd = data;

    child_port = type->exported_in_specs[port].port;
    child_node = fsd->nodes[type->exported_in_specs[port].node];
    child_conn_id = type->ports_in_base_conn_id[port] + conn_id;

    return dispatch_connect_in(child_node, child_port, child_conn_id,
        sol_flow_node_type_get_port_in(child_node->type, child_port));
}

static int
flow_exported_port_in_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;
    struct sol_flow_node *child_node;
    uint16_t child_port, child_conn_id;

    type = (struct flow_static_type *)node->type;
    fsd = data;

    child_port = type->exported_in_specs[port].port;
    child_node = fsd->nodes[type->exported_in_specs[port].node];
    child_conn_id = type->ports_in_base_conn_id[port] + conn_id;

    return dispatch_disconnect_in(child_node, child_port, child_conn_id,
        sol_flow_node_type_get_port_in(child_node->type, child_port));
}

static int
flow_exported_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;
    struct sol_flow_node *child_node;
    uint16_t child_port, child_conn_id;

    type = (struct flow_static_type *)node->type;
    fsd = data;

    child_port = type->exported_out_specs[port].port;
    child_node = fsd->nodes[type->exported_out_specs[port].node];
    child_conn_id = type->ports_out_base_conn_id[port] + conn_id;

    return dispatch_connect_out(child_node, child_port, child_conn_id,
        sol_flow_node_type_get_port_out(child_node->type, child_port));
}

static int
flow_exported_port_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;
    struct sol_flow_node *child_node;
    uint16_t child_port, child_conn_id;

    type = (struct flow_static_type *)node->type;
    fsd = data;

    child_port = type->exported_out_specs[port].port;
    child_node = fsd->nodes[type->exported_out_specs[port].node];
    child_conn_id = type->ports_out_base_conn_id[port] + conn_id;

    return dispatch_disconnect_out(child_node, child_port, child_conn_id,
        sol_flow_node_type_get_port_out(child_node->type, child_port));
}

static void
teardown_exported_ports_specs(struct flow_static_type *type)
{
    if (type->ports_in_count > 0) {
        free(type->ports_in);
        free(type->ports_in_base_conn_id);
    }
    if (type->ports_out_count > 0) {
        free(type->ports_out);
        free(type->ports_out_base_conn_id);
    }
}

static int
setup_exported_ports_specs(struct flow_static_type *type)
{
    uint16_t in_count = 0, out_count = 0, u, node, port;
    int r;

    r = validate_port_specs(type->exported_in_specs, &in_count);
    if (r < 0) {
        if (r == -E2BIG)
            SOL_WRN("too many exported in ports");
        else if (r == -EINVAL)
            SOL_WRN("exported in ports not sorted");
        return -EINVAL;
    }

    r = validate_port_specs(type->exported_out_specs, &out_count);
    if (r < 0) {
        if (r == -E2BIG)
            SOL_WRN("too many exported out ports");
        else if (r == -EINVAL)
            SOL_WRN("exported out ports not sorted");
        return -EINVAL;
    }

    type->ports_in = NULL;
    type->ports_in_base_conn_id = NULL;
    type->ports_out = NULL;
    type->ports_out_base_conn_id = NULL;

    if (in_count > 0) {
        struct sol_flow_port_type_in *port_type;
        type->ports_in_count = in_count;
        type->ports_in = calloc(in_count, sizeof(struct sol_flow_port_type_in));
        type->ports_in_base_conn_id = calloc(in_count, sizeof(uint16_t));
        if (!type->ports_in || !type->ports_in_base_conn_id)
            goto fail_nomem;

        for (u = 0; u < in_count; u++) {
            node = type->exported_in_specs[u].node;
            port = type->exported_in_specs[u].port;
            port_type = &type->ports_in[u];
            port_type->api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION;
            port_type->packet_type = sol_flow_node_type_get_port_in(type->node_specs[node].type, port)->packet_type;
            port_type->process = flow_exported_port_process;
            port_type->connect = flow_exported_port_in_connect;
            port_type->disconnect = flow_exported_port_in_disconnect;
        }
    }

    if (out_count > 0) {
        struct sol_flow_port_type_out *port_type;
        type->ports_out_count = out_count;
        type->ports_out = calloc(out_count, sizeof(struct sol_flow_port_type_out));
        type->ports_out_base_conn_id = calloc(out_count, sizeof(uint16_t));
        if (!type->ports_out || !type->ports_out_base_conn_id)
            goto fail_nomem;

        for (u = 0; u < out_count; u++) {
            node = type->exported_out_specs[u].node;
            port = type->exported_out_specs[u].port;
            port_type = &type->ports_out[u];
            port_type->api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION;
            port_type->packet_type = sol_flow_node_type_get_port_out(type->node_specs[node].type, port)->packet_type;
            port_type->connect = flow_exported_port_out_connect;
            port_type->disconnect = flow_exported_port_out_disconnect;
        }
    }

    return 0;

fail_nomem:
    teardown_exported_ports_specs(type);
    return -ENOMEM;
}

static int
flow_static_type_init(
    struct flow_static_type *type,
    const struct sol_flow_static_node_spec nodes[],
    const struct sol_flow_static_conn_spec conns[],
    const struct sol_flow_static_port_spec exported_in[],
    const struct sol_flow_static_port_spec exported_out[],
    void (*child_opts_set)(uint16_t child_index,
        const struct sol_flow_node_options *opts,
        struct sol_flow_node_options *child_opts))
{
    int r;

    *type = (const struct flow_static_type) {
        .base = {
            .base = {
                .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
                .data_size = sizeof(struct flow_static_data),
                .flags = SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER,
                .open = flow_node_open,
                .close = flow_node_close,
                .get_ports_counts = flow_get_ports_counts,
                .get_port_in = flow_get_port_in,
                .get_port_out = flow_get_port_out,
            },
            .send = flow_send,
        },
        .node_specs = nodes,
        .conn_specs = conns,
        .exported_in_specs = exported_in,
        .exported_out_specs = exported_out,
        .child_opts_set = child_opts_set
    };

    r = setup_node_specs(type);
    if (r < 0)
        return r;

    r = setup_conn_specs(type);
    if (r < 0) {
        teardown_node_specs(type);
        return r;
    }

    r = setup_exported_ports_specs(type);
    if (r < 0) {
        teardown_conn_specs(type);
        teardown_node_specs(type);
        return r;
    }

    setup_conn_ids(type);

    return 0;
}

static void
flow_static_type_fini(struct flow_static_type *type)
{
    teardown_exported_ports_specs(type);
    teardown_conn_specs(type);
    teardown_node_specs(type);
}

SOL_API struct sol_flow_node *
sol_flow_static_new(struct sol_flow_node *parent, const struct sol_flow_static_node_spec nodes[], const struct sol_flow_static_conn_spec conns[])
{
    struct sol_flow_node *node;
    struct flow_static_type *type;

    type = (struct flow_static_type *)sol_flow_static_new_type(nodes, conns, NULL, NULL, NULL);
    if (!type)
        return NULL;
    type->owned_by_node = true;

    node = sol_flow_node_new(parent,
        NULL /* no id for the enclosing flow itself */,
        &type->base.base,
        NULL);

    if (!node)
        sol_flow_static_del_type(&type->base.base);

    return node;
}

/* Different from simpler node types, we don't have a single type to check. The trick here is to use
 * one of the pointers in sol_flow_node_type that will always be the same in all static flows we
 * create. */
#define SOL_FLOW_STATIC_TYPE_CHECK(type, ...)                            \
    do {                                                                \
        if (!(type)) {                                                  \
            SOL_WRN("" # type " == NULL");                               \
            return __VA_ARGS__;                                         \
        }                                                               \
        if (((type)->flags & SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER) == 0    \
            || ((type)->get_ports_counts != flow_get_ports_counts)) { \
            SOL_WRN("" # type " isn't a static flow type!");             \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

SOL_API struct sol_flow_node *
sol_flow_static_get_node(struct sol_flow_node *flow, uint16_t index)
{
    struct flow_static_type *type;
    struct flow_static_data *fsd;

    errno = EBADR;
    SOL_NULL_CHECK(flow, NULL);
    SOL_FLOW_STATIC_TYPE_CHECK(flow->type, NULL);

    type = (struct flow_static_type *)flow->type;
    fsd = sol_flow_node_get_private_data(flow);

    if (index >= type->node_count) {
        errno = EINVAL;
        return NULL;
    }

    errno = 0;
    return fsd->nodes[index];
}

SOL_API struct sol_flow_node_type *
sol_flow_static_new_type(
    const struct sol_flow_static_node_spec nodes[],
    const struct sol_flow_static_conn_spec conns[],
    const struct sol_flow_static_port_spec exported_in[],
    const struct sol_flow_static_port_spec exported_out[],
    void (*child_opts_set)(uint16_t child_index,
        const struct sol_flow_node_options *opts,
        struct sol_flow_node_options *child_opts))
{
    struct flow_static_type *type;
    int r;

    type = calloc(1, sizeof(*type));
    if (!type)
        return NULL;

    r = flow_static_type_init(type, nodes, conns, exported_in, exported_out, child_opts_set);
    if (r < 0) {
        free(type);
        return NULL;
    }

    return &type->base.base;
}

SOL_API void
sol_flow_static_del_type(struct sol_flow_node_type *type)
{
    struct flow_static_type *fst;

    SOL_FLOW_STATIC_TYPE_CHECK(type);

    fst = (struct flow_static_type *)type;
    flow_static_type_fini(fst);
    free(fst);
}
