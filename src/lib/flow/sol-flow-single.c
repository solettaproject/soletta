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

#include "sol-flow-internal.h"
#include "sol-flow-single.h"
#include "sol-mainloop.h"

struct port_conn {
    const void *port_type;
    uint16_t idx;
    uint16_t references;
};

struct pending {
    struct sol_flow_packet *packet;
    uint16_t port_idx;
};

struct sol_flow_single_data {
    struct sol_vector connected_ports_in;
    struct sol_vector connected_ports_out;
    struct {
        struct sol_vector packets;
        struct sol_timeout *timeout;
    } pending;
    void (*process)(void *user_data, struct sol_flow_node *node, uint16_t port, const struct sol_flow_packet *packet);
    const void *user_data;
    struct sol_flow_node child; /* keep last, type->data_size = sizeof(sol_flow_single_data) + child->type->data_size */
};

struct sol_flow_single_type {
    struct sol_flow_node_container_type base;
    const struct sol_flow_node_type *child_type;

    /* This type was created for a single node, so when the node goes
     * down, the type will be finalized. */
    bool owned_by_node;
};

static int sol_flow_single_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);
#define SOL_FLOW_SINGLE_TYPE_CHECK(type, ...) \
    do { \
        if (!(type)) { \
            SOL_WRN("" # type " == NULL"); \
            return __VA_ARGS__; \
        } \
        if (((type)->flags & SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER) == 0 \
            || ((type)->open != sol_flow_single_open)) { \
            SOL_WRN("" # type " isn't a single flow type!"); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_SINGLE_CHECK(node, ...) \
    do { \
        const struct sol_flow_node_type *_sol_flow_node_type_local = sol_flow_node_get_type(node); \
        SOL_FLOW_SINGLE_TYPE_CHECK(_sol_flow_node_type_local, __VA_ARGS__); \
    } while (0)

static struct port_conn *
find_port_conn(const struct sol_vector *vector, uint16_t port_idx)
{
    const struct port_conn *pc;
    uint16_t itr;

    SOL_VECTOR_FOREACH_IDX (vector, pc, itr) {
        if (pc->idx == port_idx)
            return (struct port_conn *)pc;
    }

    return NULL;
}

static int32_t
port_connect(struct sol_vector *vector, uint16_t port_idx, const void *port_type)
{
    struct port_conn *pc;

    pc = find_port_conn(vector, port_idx);
    if (!pc) {
        pc = sol_vector_append(vector);
        SOL_NULL_CHECK(pc, -ENOMEM);
        pc->idx = port_idx;
        pc->port_type = port_type;
    }

    SOL_INT_CHECK(pc->references, == UINT16_MAX, -EOVERFLOW);
    pc->references++;
    return pc->references;
}

static int32_t
port_disconnect(struct sol_vector *vector, uint16_t port_idx, const void **ret_port_type)
{
    struct port_conn *pc;

    if (ret_port_type)
        *ret_port_type = NULL;

    pc = find_port_conn(vector, port_idx);
    SOL_NULL_CHECK(pc, -ENOENT);

    if (ret_port_type)
        *ret_port_type = pc->port_type;

    pc->references--;
    if (pc->references > 0)
        return pc->references;

    return sol_vector_del_element(vector, pc);
}

static int32_t
sol_flow_single_connect_port_in_internal(struct sol_flow_node *node, uint16_t port_idx)
{
    const struct sol_flow_node_type *type = node->type;
    struct sol_flow_single_data *mdata = (void *)node->data;
    const struct sol_flow_port_type_in *port_type;
    int32_t r;

    port_type = sol_flow_node_type_get_port_in(type, port_idx);
    SOL_NULL_CHECK_MSG(port_type, -ENOENT,
        "type %p has no input port #%" PRIu16, type, port_idx);

    r = port_connect(&mdata->connected_ports_in, port_idx, port_type);
    if (r != 1)
        return r;

    if (port_type->connect) {
        r = port_type->connect(&mdata->child, mdata->child.data, port_idx, 0);
        if (r < 0) {
            SOL_WRN("failed to connect to internal node %p (type %p) input port #%" PRIu16 ": %s",
                &mdata->child, mdata->child.type, port_idx, sol_util_strerrora(-r));
            port_disconnect(&mdata->connected_ports_in, port_idx, NULL);
            return r;
        }
    }

    return 1;
}

static int32_t
sol_flow_single_disconnect_port_in_internal(struct sol_flow_node *node, uint16_t port_idx)
{
    struct sol_flow_single_data *mdata = (void *)node->data;
    const struct sol_flow_port_type_in *port_type;
    int32_t r;

    r = port_disconnect(&mdata->connected_ports_in, port_idx, (const void **)&port_type);
    if (r != 0)
        return r;

    if (!port_type->disconnect)
        return 0;

    port_type->disconnect(&mdata->child, mdata->child.data, port_idx, 0);
    return 0;
}

static int32_t
sol_flow_single_connect_port_out_internal(struct sol_flow_node *node, uint16_t port_idx)
{
    const struct sol_flow_node_type *type = node->type;
    struct sol_flow_single_data *mdata = (void *)node->data;
    const struct sol_flow_port_type_out *port_type;
    int32_t r;

    port_type = sol_flow_node_type_get_port_out(type, port_idx);
    SOL_NULL_CHECK_MSG(port_type, -ENOENT,
        "type %p has no output port #%" PRIu16, type, port_idx);

    r = port_connect(&mdata->connected_ports_out, port_idx, port_type);
    if (r != 1)
        return r;

    if (port_type->connect) {
        r = port_type->connect(&mdata->child, mdata->child.data, port_idx, 0);
        if (r < 0) {
            SOL_WRN("failed to connect to internal node %p (type %p) output port #%" PRIu16 ": %s",
                &mdata->child, mdata->child.type, port_idx, sol_util_strerrora(-r));
            port_disconnect(&mdata->connected_ports_out, port_idx, NULL);
            return r;
        }
    }

    return 1;
}

static int32_t
sol_flow_single_disconnect_port_out_internal(struct sol_flow_node *node, uint16_t port_idx)
{
    struct sol_flow_single_data *mdata = (void *)node->data;
    const struct sol_flow_port_type_out *port_type;
    int32_t r;

    r = port_disconnect(&mdata->connected_ports_out, port_idx, (const void **)&port_type);
    if (r != 0)
        return r;

    if (!port_type->disconnect)
        return 0;

    port_type->disconnect(&mdata->child, mdata->child.data, port_idx, 0);
    return 0;
}

static const struct sol_flow_port_type_in *
sol_flow_single_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct sol_flow_single_type *single_type = (const struct sol_flow_single_type *)type;

    return sol_flow_node_type_get_port_in(single_type->child_type, port);
}

static const struct sol_flow_port_type_out *
sol_flow_single_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct sol_flow_single_type *single_type = (const struct sol_flow_single_type *)type;

    return sol_flow_node_type_get_port_out(single_type->child_type, port);
}

static int
sol_flow_single_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_single_type *single_type;
    const struct sol_flow_single_options *opts;
    struct sol_flow_single_data *mdata = data;
    const uint16_t *itr;
    int32_t r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_SINGLE_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_single_options *)options;

    single_type = (const struct sol_flow_single_type *)node->type;
    mdata->process = opts->process;
    mdata->user_data = opts->user_data;

    sol_vector_init(&mdata->connected_ports_in, sizeof(struct port_conn));
    sol_vector_init(&mdata->connected_ports_out, sizeof(struct port_conn));
    sol_vector_init(&mdata->pending.packets, sizeof(struct pending));

    /* first pass will register what we want to connect without doing
     * actual connect() calls since the node wasn't created yet (needs
     * sol_flow_node_init() below.
     *
     * we need this since on node open it may produce some packets and
     * we'd drop them since they were not connected (as in the array).
     */
    if (opts->connected_ports_in) {
        for (itr = opts->connected_ports_in; *itr != UINT16_MAX; itr++) {
            const struct sol_flow_port_type_in *port_type;
            struct port_conn *pc;

            port_type = node->type->get_port_in(node->type, *itr);
            r = -ENOENT;
            SOL_NULL_CHECK_MSG_GOTO(port_type, error,
                "type %p has no input port #%" PRIu16, node->type, *itr);

            pc = find_port_conn(&mdata->connected_ports_in, *itr);
            if (pc)
                continue;

            pc = sol_vector_append(&mdata->connected_ports_in);
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(pc, error);
            pc->idx = *itr;
            pc->port_type = port_type;
            pc->references = 0;
        }
    }

    if (opts->connected_ports_out) {
        for (itr = opts->connected_ports_out; *itr != UINT16_MAX; itr++) {
            const struct sol_flow_port_type_out *port_type;
            struct port_conn *pc;

            port_type = node->type->get_port_out(node->type, *itr);
            r = -ENOENT;
            SOL_NULL_CHECK_MSG_GOTO(port_type, error,
                "type %p has no output port #%" PRIu16, node->type, *itr);

            pc = find_port_conn(&mdata->connected_ports_out, *itr);
            if (pc)
                continue;

            pc = sol_vector_append(&mdata->connected_ports_out);
            r = -ENOMEM;
            SOL_NULL_CHECK_GOTO(pc, error);
            pc->idx = *itr;
            pc->port_type = port_type;
            pc->references = 0;
        }
    }

    r = sol_flow_node_init(&mdata->child, node, "child",
        single_type->child_type, opts->options);
    SOL_INT_CHECK(r, < 0, r);

    if (opts->connected_ports_in) {
        for (itr = opts->connected_ports_in; *itr != UINT16_MAX; itr++) {
            r = sol_flow_single_connect_port_in_internal(node, *itr);
            SOL_INT_CHECK_GOTO(r, < 0, failed_ports_in);
        }
    }

    if (opts->connected_ports_out) {
        for (itr = opts->connected_ports_out; *itr != UINT16_MAX; itr++) {
            r = sol_flow_single_connect_port_out_internal(node, *itr);
            SOL_INT_CHECK_GOTO(r, < 0, failed_ports_out);
        }
    }

    return 0;

failed_ports_out:
    for (; itr >= opts->connected_ports_out; itr--)
        sol_flow_single_disconnect_port_out_internal(node, *itr);

    if (opts->connected_ports_in) {
        for (itr = opts->connected_ports_in; *itr != UINT16_MAX; itr++) {
        }
        itr--;
    }

failed_ports_in:
    for (; itr >= opts->connected_ports_in; itr--)
        sol_flow_single_disconnect_port_in_internal(node, *itr);

    return r;

error:
    sol_vector_clear(&mdata->connected_ports_in);
    sol_vector_clear(&mdata->connected_ports_out);
    return r;
}

static void
sol_flow_single_close(struct sol_flow_node *node, void *data)
{
    struct sol_flow_single_type *single_type = (struct sol_flow_single_type *)node->type;
    struct sol_flow_single_data *mdata = data;
    const struct port_conn *pc;
    struct pending *pending;
    uint16_t idx;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&mdata->connected_ports_in, pc, idx) {
        const struct sol_flow_port_type_in *port_type = pc->port_type;

        if (!port_type->disconnect)
            continue;
        port_type->disconnect(&mdata->child, mdata->child.data, pc->idx, 0);
    }
    sol_vector_clear(&mdata->connected_ports_in);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&mdata->connected_ports_out, pc, idx) {
        const struct sol_flow_port_type_out *port_type = pc->port_type;

        if (!port_type->disconnect)
            continue;
        port_type->disconnect(&mdata->child, mdata->child.data, pc->idx, 0);
    }
    sol_vector_clear(&mdata->connected_ports_out);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&mdata->pending.packets, pending, idx) {
        sol_flow_packet_del(pending->packet);
    }
    sol_vector_clear(&mdata->pending.packets);

    if (mdata->pending.timeout)
        sol_timeout_del(mdata->pending.timeout);

    sol_flow_node_fini(&mdata->child);

    if (single_type->owned_by_node)
        sol_flow_node_type_del(&single_type->base.base);
}

static void
sol_flow_single_dispose_type(struct sol_flow_node_type *type)
{
    free(type);
}

static bool
sol_flow_single_send_do(void *data)
{
    struct sol_flow_node *node = data;
    struct sol_flow_single_data *mdata = (void *)node->data;
    struct sol_vector *v = &mdata->pending.packets;
    struct pending *pending;
    uint16_t idx, last_idx;

    /* do not dispatch newly added packets immediately, otherwise it
     * may get into infinite recursion loop
     */
    last_idx = v->len;

    SOL_VECTOR_FOREACH_IDX (v, pending, idx) {
        struct sol_flow_packet *packet;
        uint16_t port_idx;

        if (idx == last_idx)
            break;

        packet = pending->packet;
        port_idx = pending->port_idx;
        mdata->process((void *)mdata->user_data, node, port_idx, packet);
        sol_flow_packet_del(packet);
    }

    if (v->len == last_idx) {
        sol_vector_clear(v);
        mdata->pending.timeout = NULL;
        return false;
    }

    for (idx = 0; idx < last_idx; idx++)
        sol_vector_del(v, 0);
    return true;
}

static int
sol_flow_single_send(struct sol_flow_node *container, struct sol_flow_node *source_node, uint16_t source_out_port_idx, struct sol_flow_packet *packet)
{
    struct sol_flow_single_data *mdata = (void *)container->data;
    const struct port_conn *pc;
    struct pending *pending;

    if (!mdata->process) {
        const struct sol_flow_packet_type *packet_type;

        packet_type = sol_flow_packet_get_type(packet);
        SOL_DBG("drop packet %p (%s) from single-node %p port #%" PRIu16 ": no process() callback provided.",
            packet, packet_type->name, container, source_out_port_idx);
        goto drop;
    }

    pc = find_port_conn(&mdata->connected_ports_out, source_out_port_idx);
    if (!pc) {
        const struct sol_flow_packet_type *packet_type;

        packet_type = sol_flow_packet_get_type(packet);
        SOL_DBG("drop packet %p (%s) from single-node %p port #%" PRIu16 ": not connected.",
            packet, packet_type->name, container, source_out_port_idx);
        goto drop;
    }

    pending = sol_vector_append(&mdata->pending.packets);
    SOL_NULL_CHECK_GOTO(pending, drop);
    pending->packet = packet;
    pending->port_idx = source_out_port_idx;

    if (!mdata->pending.timeout) {
        mdata->pending.timeout = sol_timeout_add(0, sol_flow_single_send_do, container);
        SOL_NULL_CHECK_GOTO(mdata->pending.timeout, pop_and_drop);
    }

    return 0;

pop_and_drop:
    sol_vector_del_last(&mdata->pending.packets);

drop:
    sol_flow_packet_del(packet);
    return 0;
}

static int
sol_flow_single_process(struct sol_flow_node *container, uint16_t source_in_port_idx, struct sol_flow_packet *packet)
{
    struct sol_flow_single_data *mdata = (void *)container->data;
    const struct port_conn *pc;
    const struct sol_flow_port_type_in *port_type;

    pc = find_port_conn(&mdata->connected_ports_in, source_in_port_idx);
    if (!pc) {
        const struct sol_flow_packet_type *packet_type;

        packet_type = sol_flow_packet_get_type(packet);
        SOL_DBG("drop packet %p (%s) to single-node %p port %" PRIu16 ": not connected.",
            packet, packet_type->name, container, source_in_port_idx);
        goto end;
    }

    port_type = pc->port_type;
    if (!port_type->process) {
        const struct sol_flow_packet_type *packet_type;

        packet_type = sol_flow_packet_get_type(packet);
        SOL_DBG("drop packet %p (%s) to single-node %p port %" PRIu16 ": port doesn't provide process()",
            packet, packet_type->name, container, source_in_port_idx);
        goto end;
    }

    port_type->process(&mdata->child, mdata->child.data, source_in_port_idx, 0, packet);

end:
    sol_flow_packet_del(packet);
    return 0;
}

SOL_API struct sol_flow_node *
sol_flow_single_new(const char *id, const struct sol_flow_node_type *base_type, const struct sol_flow_node_options *options, const uint16_t *connected_ports_in, const uint16_t *connected_ports_out, void (*process)(void *user_data, struct sol_flow_node *node, uint16_t port, const struct sol_flow_packet *packet), const void *user_data)
{
    struct sol_flow_single_type *single_type;
    struct sol_flow_node_options empty_opts = {
#ifndef SOL_NO_API_VERSION
        .api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION,
        .sub_api = 0,
#endif
    };
    struct sol_flow_single_options child_opts = SOL_FLOW_SINGLE_OPTIONS_DEFAULTS(
        .options = options,
        .process = process,
        .user_data = user_data,
        .connected_ports_in = connected_ports_in,
        .connected_ports_out = connected_ports_out,
        );

    errno = -EINVAL;
    SOL_NULL_CHECK(base_type, NULL);
    SOL_FLOW_NODE_TYPE_API_CHECK(base_type, SOL_FLOW_NODE_TYPE_API_VERSION, NULL);

    single_type = (struct sol_flow_single_type *)sol_flow_single_new_type(base_type);
    SOL_NULL_CHECK(single_type, NULL);

    single_type->owned_by_node = true;

    if (!child_opts.options)
        child_opts.options = single_type->child_type->default_options;
    if (!child_opts.options)
        child_opts.options = &empty_opts;

    errno = 0;
    return sol_flow_node_new(NULL, id, &single_type->base.base, &child_opts.base);
}

SOL_API struct sol_flow_node_type *
sol_flow_single_new_type(const struct sol_flow_node_type *base_type)
{
    struct sol_flow_single_type *single_type;
    static const struct sol_flow_single_options default_options = SOL_FLOW_SINGLE_OPTIONS_DEFAULTS();

    errno = -EINVAL;
    SOL_NULL_CHECK(base_type, NULL);
    SOL_FLOW_NODE_TYPE_API_CHECK(base_type, SOL_FLOW_NODE_TYPE_API_VERSION, NULL);

    errno = 0;
    single_type = calloc(1, sizeof(*single_type));
    SOL_NULL_CHECK(single_type, NULL);

    SOL_SET_API_VERSION(single_type->base.base.api_version = SOL_FLOW_NODE_TYPE_API_VERSION);

    single_type->base.base.data_size = base_type->data_size + sizeof(struct sol_flow_single_data);
    single_type->base.base.options_size = sizeof(struct sol_flow_single_options);
    single_type->base.base.flags = base_type->flags | SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER;
    single_type->base.base.default_options = &default_options;
    single_type->base.base.ports_in_count = base_type->ports_in_count;
    single_type->base.base.ports_out_count = base_type->ports_out_count;
    single_type->base.base.get_port_in = sol_flow_single_get_port_in;
    single_type->base.base.get_port_out = sol_flow_single_get_port_out;
    single_type->base.base.open = sol_flow_single_open;
    single_type->base.base.close = sol_flow_single_close;
    single_type->base.base.dispose_type = sol_flow_single_dispose_type;
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    single_type->base.base.description = base_type->description;
#endif
    single_type->base.send = sol_flow_single_send;
    single_type->base.process = sol_flow_single_process;
    single_type->child_type = base_type;

    return &single_type->base.base;
}

SOL_API int32_t
sol_flow_single_connect_port_in(struct sol_flow_node *node, uint16_t port_idx)
{
    SOL_FLOW_SINGLE_CHECK(node, -EINVAL);
    return sol_flow_single_connect_port_in_internal(node, port_idx);
}

SOL_API int32_t
sol_flow_single_disconnect_port_in(struct sol_flow_node *node, uint16_t port_idx)
{
    SOL_FLOW_SINGLE_CHECK(node, -EINVAL);
    return sol_flow_single_disconnect_port_in_internal(node, port_idx);
}

SOL_API int32_t
sol_flow_single_connect_port_out(struct sol_flow_node *node, uint16_t port_idx)
{
    SOL_FLOW_SINGLE_CHECK(node, -EINVAL);
    return sol_flow_single_connect_port_out_internal(node, port_idx);
}

SOL_API int32_t
sol_flow_single_disconnect_port_out(struct sol_flow_node *node, uint16_t port_idx)
{
    SOL_FLOW_SINGLE_CHECK(node, -EINVAL);
    return sol_flow_single_disconnect_port_out_internal(node, port_idx);
}

SOL_API struct sol_flow_node *
sol_flow_single_get_child(const struct sol_flow_node *node)
{
    struct sol_flow_single_data *mdata;

    SOL_FLOW_SINGLE_CHECK(node, NULL);

    mdata = sol_flow_node_get_private_data(node);
    return &mdata->child;
}

SOL_API const struct sol_flow_node_type *
sol_flow_single_type_get_child_type(const struct sol_flow_node_type *single_type)
{
    SOL_FLOW_SINGLE_TYPE_CHECK(single_type, NULL);
    return ((const struct sol_flow_single_type *)single_type)->child_type;
}
