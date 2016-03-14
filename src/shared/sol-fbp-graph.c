/*
 * This file is part of the Soletta Project
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

#include "sol-fbp.h"
#include "sol-util-internal.h"

#include "sol-fbp-internal-log.h"

int
sol_fbp_graph_init(struct sol_fbp_graph *g)
{
    sol_fbp_init_log_domain();
    SOL_NULL_CHECK(g, -EINVAL);
    sol_vector_init(&g->nodes, sizeof(struct sol_fbp_node));
    sol_vector_init(&g->conns, sizeof(struct sol_fbp_conn));
    sol_vector_init(&g->exported_in_ports, sizeof(struct sol_fbp_exported_port));
    sol_vector_init(&g->exported_out_ports, sizeof(struct sol_fbp_exported_port));
    sol_vector_init(&g->declarations, sizeof(struct sol_fbp_declaration));
    sol_vector_init(&g->options, sizeof(struct sol_fbp_option));
    g->arena = sol_arena_new();
    return 0;
}

int
sol_fbp_graph_fini(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *n;
    uint16_t i;

    SOL_NULL_CHECK(g, -EINVAL);
    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        sol_vector_clear(&n->meta);
        sol_vector_clear(&n->in_ports);
        sol_vector_clear(&n->out_ports);
    }

    sol_vector_clear(&g->nodes);
    sol_vector_clear(&g->conns);

    sol_vector_clear(&g->exported_in_ports);
    sol_vector_clear(&g->exported_out_ports);
    sol_vector_clear(&g->declarations);
    sol_vector_clear(&g->options);

    sol_arena_del(g->arena);

    return 0;
}

int
sol_fbp_graph_add_node(struct sol_fbp_graph *g,
    struct sol_str_slice name, struct sol_str_slice component,
    struct sol_fbp_position position, struct sol_fbp_node **out_node)
{
    struct sol_fbp_node *n;
    bool declared_component = component.len != 0;
    int r;
    uint16_t i;

    SOL_NULL_CHECK(g, -EINVAL);

    if (name.len == 1 && name.data[0] == '_') {
        if (component.len == 0)
            return -EINVAL;

        r = sol_arena_slice_sprintf(g->arena, &name, "#anon:%d:%d", position.line, position.column);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
            if (!sol_str_slice_eq(n->name, name))
                continue;

            if (declared_component) {
                if (n->component.len != 0) {
                    /* component already exists for this node in the graph */
                    if (out_node)
                        *out_node = n;
                    return -EEXIST;
                }
                n->component = component;
            }
            return i;
        }
    }

    n = sol_vector_append(&g->nodes);
    SOL_NULL_CHECK(n, -errno);

    n->name = name;
    n->component = component;
    n->position = position;
    n->user_data = NULL;
    sol_vector_init(&n->meta, sizeof(struct sol_fbp_meta));
    sol_vector_init(&n->in_ports, sizeof(struct sol_fbp_port));
    sol_vector_init(&n->out_ports, sizeof(struct sol_fbp_port));

    if (out_node)
        *out_node = n;
    return g->nodes.len - 1;
}

int
sol_fbp_graph_add_meta(struct sol_fbp_graph *g,
    int node, struct sol_str_slice key, struct sol_str_slice value, struct sol_fbp_position position)
{
    struct sol_fbp_node *n;
    struct sol_fbp_meta *m;
    uint16_t i;

    SOL_NULL_CHECK(g, -EINVAL);
    SOL_INT_CHECK(node, < 0, -EINVAL);

    n = sol_vector_get(&g->nodes, node);
    SOL_NULL_CHECK(n, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&n->meta, m, i) {
        if (sol_str_slice_eq(m->key, key))
            return -EEXIST;
    }

    m = sol_vector_append(&n->meta);
    SOL_NULL_CHECK(m, -errno);

    m->key = key;
    m->value = value;
    m->position = position;
    return 0;
}

int
sol_fbp_graph_add_in_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice name, struct sol_fbp_position position)
{
    struct sol_fbp_node *n;
    struct sol_fbp_port *p;
    uint16_t i;

    SOL_NULL_CHECK(g, -EINVAL);
    SOL_INT_CHECK(node, < 0, -EINVAL);

    n = sol_vector_get(&g->nodes, node);
    SOL_NULL_CHECK(n, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&n->in_ports, p, i) {
        if (sol_str_slice_eq(p->name, name))
            return 0;
    }

    p = sol_vector_append(&n->in_ports);
    SOL_NULL_CHECK(p, -errno);

    p->name = name;
    p->position = position;
    return 0;
}

int
sol_fbp_graph_add_out_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice name, struct sol_fbp_position position)
{
    struct sol_fbp_node *n;
    struct sol_fbp_port *p;
    uint16_t i;

    SOL_NULL_CHECK(g, -EINVAL);
    SOL_INT_CHECK(node, < 0, -EINVAL);

    n = sol_vector_get(&g->nodes, node);
    SOL_NULL_CHECK(n, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&n->out_ports, p, i) {
        if (sol_str_slice_eq(p->name, name))
            return 0;
    }

    p = sol_vector_append(&n->out_ports);
    SOL_NULL_CHECK(p, -errno);

    p->name = name;
    p->position = position;
    return 0;
}

int
sol_fbp_graph_add_conn(struct sol_fbp_graph *g,
    int src, struct sol_str_slice src_port, int src_port_idx,
    int dst, struct sol_str_slice dst_port, int dst_port_idx,
    struct sol_fbp_position position)
{
    struct sol_fbp_conn *conn;
    uint16_t i;

    SOL_NULL_CHECK(g, -EINVAL);
    SOL_INT_CHECK(src, < 0, -EINVAL);
    SOL_INT_CHECK(dst, < 0, -EINVAL);
    if (!src_port.len || !dst_port.len)
        return -EINVAL;

    SOL_VECTOR_FOREACH_IDX (&g->conns, conn, i) {
        if (conn->src == src && conn->dst == dst
            && conn->src_port_idx == src_port_idx
            && conn->dst_port_idx == dst_port_idx
            && sol_str_slice_eq(conn->src_port, src_port)
            && sol_str_slice_eq(conn->dst_port, dst_port))
            return -EEXIST;
    }

    conn = sol_vector_append(&g->conns);
    SOL_NULL_CHECK(conn, -errno);

    conn->src = src;
    conn->src_port = src_port;
    conn->src_port_idx = src_port_idx;
    conn->dst = dst;
    conn->dst_port = dst_port;
    conn->dst_port_idx = dst_port_idx;
    conn->position = position;
    return i;
}

int
sol_fbp_graph_add_exported_in_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice port, int port_idx, struct sol_str_slice exported_name,
    struct sol_fbp_position position, struct sol_fbp_exported_port **out_ep)
{
    struct sol_fbp_exported_port *ep;
    uint16_t i;
    int err;

    SOL_VECTOR_FOREACH_IDX (&g->exported_in_ports, ep, i) {
        if (sol_str_slice_eq(ep->exported_name, exported_name)) {
            err = -EEXIST;
            goto end;
        }
        if (ep->node == node && (ep->port_idx == port_idx
            || (ep->port_idx == -1 || port_idx == -1))
            && sol_str_slice_eq(ep->port, port)) {
            err = -EADDRINUSE;
            goto end;
        }
    }

    ep = sol_vector_append(&g->exported_in_ports);
    SOL_NULL_CHECK(ep, -errno);

    ep->node = node;
    ep->port = port;
    ep->port_idx = port_idx;
    ep->exported_name = exported_name;
    ep->position = position;
    err = 0;

end:
    if (out_ep)
        *out_ep = ep;
    return err;
}

int
sol_fbp_graph_add_exported_out_port(struct sol_fbp_graph *g,
    int node, struct sol_str_slice port, int port_idx, struct sol_str_slice exported_name,
    struct sol_fbp_position position, struct sol_fbp_exported_port **out_ep)
{
    struct sol_fbp_exported_port *ep;
    uint16_t i;
    int err;

    SOL_VECTOR_FOREACH_IDX (&g->exported_out_ports, ep, i) {
        if (sol_str_slice_eq(ep->exported_name, exported_name)) {
            err = -EEXIST;
            goto end;
        }
        if (ep->node == node && (ep->port_idx == port_idx
            || (ep->port_idx == -1 || port_idx == -1))
            && sol_str_slice_eq(ep->port, port)) {
            err = -EADDRINUSE;
            goto end;
        }
    }

    ep = sol_vector_append(&g->exported_out_ports);
    SOL_NULL_CHECK(ep, -errno);

    ep->node = node;
    ep->port = port;
    ep->port_idx = port_idx;
    ep->exported_name = exported_name;
    ep->position = position;
    err = 0;

end:
    if (out_ep)
        *out_ep = ep;
    return err;
}

int
sol_fbp_graph_declare(struct sol_fbp_graph *g,
    struct sol_str_slice name, struct sol_str_slice metatype, struct sol_str_slice contents, struct sol_fbp_position position)
{
    struct sol_fbp_declaration *dec;
    uint16_t i;

    if (name.len == 0 || metatype.len == 0 || contents.len == 0)
        return -EINVAL;

    SOL_VECTOR_FOREACH_IDX (&g->declarations, dec, i) {
        if (sol_str_slice_eq(dec->name, name))
            return -EEXIST;
    }

    dec = sol_vector_append(&g->declarations);
    SOL_NULL_CHECK(dec, -errno);

    dec->name = name;
    dec->metatype = metatype;
    dec->contents = contents;
    dec->position = position;
    return i;
}

int
sol_fbp_graph_option(struct sol_fbp_graph *g,
    int node, struct sol_str_slice name, struct sol_str_slice node_opt, struct sol_fbp_position position)
{
    struct sol_fbp_option *opt;
    uint16_t i;

    if (name.len == 0 || node_opt.len == 0)
        return -EINVAL;

    SOL_VECTOR_FOREACH_IDX (&g->options, opt, i) {
        if (sol_str_slice_eq(opt->name, name))
            return -EEXIST;
    }

    opt = sol_vector_append(&g->options);
    SOL_NULL_CHECK(opt, -errno);

    opt->name = name;
    opt->node = node;
    opt->node_option = node_opt;
    opt->position = position;
    return i;
}
