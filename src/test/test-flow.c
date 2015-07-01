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

#include "sol-flow.h"
#include "sol-flow-node-types.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "test.h"

enum event_type {
    EVENT_NONE,
    EVENT_NODE_OPEN,
    EVENT_NODE_CLOSE,
    EVENT_PORT_IN_CONNECT,
    EVENT_PORT_IN_DISCONNECT,
    EVENT_PORT_OUT_CONNECT,
    EVENT_PORT_OUT_DISCONNECT,
    EVENT_PORT_PROCESS,
};

struct test_event {
    struct sol_flow_node *node;
    enum event_type type;
    uint16_t id;
};

static bool test_initial_data = false;
static struct sol_vector test_events = SOL_VECTOR_INIT(struct test_event);

static void
add_event(struct sol_flow_node *node, enum event_type type, uint16_t id)
{
    struct test_event *ev;

    ev = sol_vector_append(&test_events);
    ev->node = node;
    ev->type = type;
    ev->id = id;
}

static bool
quit_loop(void *data)
{
    sol_quit();
    return false;
}

static int
count_events(struct sol_flow_node *node, enum event_type type, uint16_t id)
{
    struct test_event *ev;
    int count = 0;
    uint16_t i;

    /* static flow will use idlers to send packets, so we need to process
     * the main loop each time */
    /* TODO: Get rid of this timeout */
    sol_timeout_add(1, quit_loop, NULL);
    sol_run();

    SOL_VECTOR_FOREACH_IDX (&test_events, ev, i) {
        if (node && ev->node != node)
            continue;
        if (type != EVENT_NONE && ev->type != type)
            continue;
        if (id != UINT16_MAX && ev->id != id)
            continue;
        count++;
    }

    return count;
}

static void
clear_events(void)
{
    sol_vector_clear(&test_events);
}

static int
test_node_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    add_event(node, EVENT_NODE_OPEN, 0);
    if (test_initial_data) {
        return sol_flow_send_empty_packet(node, 0);
    }
    return 0;
}

static void
test_node_close(struct sol_flow_node *node, void *data)
{
    add_event(node, EVENT_NODE_CLOSE, 0);
}

static int
test_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_IN_CONNECT, conn_id);
    return 0;
}

static int
test_port_in_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_IN_DISCONNECT, conn_id);
    return 0;
}

static int
test_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_OUT_CONNECT, conn_id);
    return 0;
}

static int
test_port_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_OUT_DISCONNECT, conn_id);
    return 0;
}

static int
test_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    add_event(node, EVENT_PORT_PROCESS, conn_id);
    return 0;
}

static struct sol_flow_port_type_out test_port_out = {
    .api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_out_disconnect,
};

static struct sol_flow_port_type_in test_port_in = {
    .api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .disconnect = test_port_in_disconnect,
    .connect = test_port_in_connect,
    .process = test_port_process
};

static struct sol_flow_port_type_in test_port_match_in = {
    .api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_in_connect,
    .disconnect = test_port_in_disconnect,
    .process = test_port_process,
};

static struct sol_flow_port_type_out test_port_match_out = {
    .api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_out_disconnect,
};

static struct sol_flow_port_type_in test_port_any_in = {
    .api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_in_connect,
    .disconnect = test_port_in_disconnect,
    .process = test_port_process,
};

static struct sol_flow_port_type_out test_port_any_out = {
    .api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_out_disconnect,
};

static const struct sol_flow_port_type_in *test_ports_in[] = {
    &test_port_in,
    &test_port_in,
    &test_port_match_in,
    &test_port_any_in,
};

static const struct sol_flow_port_type_out *test_ports_out[] = {
    &test_port_out,
    &test_port_out,
    &test_port_match_out,
    &test_port_any_out,
};

static void
test_node_get_ports_counts(const struct sol_flow_node_type *type, uint16_t *ports_in_count, uint16_t *ports_out_count)
{
    if (!test_port_in.packet_type) {
        test_port_in.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
        test_port_out.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
        test_port_match_in.packet_type = SOL_FLOW_PACKET_TYPE_BOOLEAN;
        test_port_match_out.packet_type = SOL_FLOW_PACKET_TYPE_BOOLEAN;
        test_port_any_in.packet_type = SOL_FLOW_PACKET_TYPE_ANY;
        test_port_any_out.packet_type = SOL_FLOW_PACKET_TYPE_ANY;
    }

    if (ports_in_count)
        *ports_in_count = ARRAY_SIZE(test_ports_in);
    if (ports_out_count)
        *ports_out_count = ARRAY_SIZE(test_ports_out);
}

static const struct sol_flow_port_type_in *
test_node_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    return test_ports_in[port];
}

static const struct sol_flow_port_type_out *
test_node_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    return test_ports_out[port];
}

static const struct sol_flow_node_type test_node_type = {
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,

    /* Forces unaligned size, so the storage need to take that into account. */
    .data_size = sizeof(char),

    .open = test_node_open,
    .close = test_node_close,

    .get_ports_counts = test_node_get_ports_counts,
    .get_port_in = test_node_get_port_in,
    .get_port_out = test_node_get_port_out,
};

static struct sol_flow_node_type *
test_flow_new_type(void)
{
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { .type = &test_node_type },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, 0, .dst = 1, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_in[] = {
        { 1, 0 },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 1, 0 },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    return sol_flow_static_new_type(nodes, conns, exported_in, exported_out, NULL);
}

static void
test_flow_del_type(struct sol_flow_node_type *type)
{
    sol_flow_static_del_type(type);
}

#define ASSERT_EVENT_COUNT(node, event, count) \
    ASSERT_INT_EQ(count_events(node, event, UINT16_MAX), count);

#define ASSERT_EVENT_WITH_ID_COUNT(node, event, id, count)   \
    ASSERT_INT_EQ(count_events(node, event, id), count);


DEFINE_TEST(node_is_opened_and_closed);

static void
node_is_opened_and_closed(void)
{
    struct sol_flow_node *flow, *node, *node_in;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "just a node" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, conns);
    node = sol_flow_static_get_node(flow, 0);
    node_in = sol_flow_static_get_node(flow, 1);

    ASSERT_EVENT_COUNT(node, EVENT_NODE_OPEN, 1);
    ASSERT_EVENT_COUNT(node, EVENT_NODE_CLOSE, 0);
    ASSERT_EVENT_COUNT(node_in, EVENT_NODE_OPEN, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_NODE_CLOSE, 0);

    sol_flow_node_del(flow);
    ASSERT_EVENT_COUNT(node, EVENT_NODE_CLOSE, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_NODE_CLOSE, 1);
}


DEFINE_TEST(connect_two_nodes);

static void
connect_two_nodes(void)
{
    struct sol_flow_node *flow, *node_out, *node_in;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, conns);
    node_out = sol_flow_static_get_node(flow, 0);
    node_in = sol_flow_static_get_node(flow, 1);

    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_OUT_CONNECT, 1);
    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_OUT_DISCONNECT, 0);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_IN_CONNECT, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_IN_DISCONNECT, 0);

    sol_flow_node_del(flow);

    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_OUT_CONNECT, 1);
    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_OUT_DISCONNECT, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_IN_CONNECT, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_IN_DISCONNECT, 1);
}

DEFINE_TEST(send_packets);

static void
send_packets(void)
{
    struct sol_flow_node *flow, *node_out, *node_in;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    int i;

    flow = sol_flow_static_new(NULL, nodes, conns);
    node_out = sol_flow_static_get_node(flow, 0);
    node_in = sol_flow_static_get_node(flow, 1);

    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, 0);

    for (i = 1; i < 10; i++) {
        sol_flow_send_empty_packet(node_out, 0);
        ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, i);
    }

    sol_flow_node_del(flow);
}


DEFINE_TEST(send_packets_multiple_out_connections);

static void
send_packets_multiple_out_connections(void)
{
    struct sol_flow_node *flow, *node_out, *node_in1, *node_in2;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in 1" },
        [2] = { .type = &test_node_type, .name = "node in 2" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        { .src = 0, .src_port = 0, .dst = 2, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    int i;

    flow = sol_flow_static_new(NULL, nodes, conns);
    node_out = sol_flow_static_get_node(flow, 0);
    node_in1 = sol_flow_static_get_node(flow, 1);
    node_in2 = sol_flow_static_get_node(flow, 2);

    ASSERT_EVENT_COUNT(node_in1, EVENT_PORT_PROCESS, 0);
    ASSERT_EVENT_COUNT(node_in2, EVENT_PORT_PROCESS, 0);

    for (i = 1; i < 10; i++) {
        sol_flow_send_empty_packet(node_out, 0);
        ASSERT_EVENT_COUNT(node_in1, EVENT_PORT_PROCESS, i);
        ASSERT_EVENT_COUNT(node_in2, EVENT_PORT_PROCESS, i);
    }

    sol_flow_node_del(flow);
}


DEFINE_TEST(send_packets_in_different_nodes);

static void
send_packets_in_different_nodes(void)
{
    struct sol_flow_node *flow, *node_out1, *node_out2, *node_in1, *node_in2;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out 1" },
        [1] = { .type = &test_node_type, .name = "node out 2" },
        [2] = { .type = &test_node_type, .name = "node in 1" },
        [3] = { .type = &test_node_type, .name = "node in 2" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 2, .dst_port = 0 },
        { .src = 0, .src_port = 0, .dst = 3, .dst_port = 0 },
        { .src = 1, .src_port = 0, .dst = 2, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, conns);
    node_out1 = sol_flow_static_get_node(flow, 0);
    node_out2 = sol_flow_static_get_node(flow, 1);
    node_in1 = sol_flow_static_get_node(flow, 2);
    node_in2 = sol_flow_static_get_node(flow, 3);

    ASSERT_EVENT_COUNT(node_in1, EVENT_PORT_PROCESS, 0);
    ASSERT_EVENT_COUNT(node_in2, EVENT_PORT_PROCESS, 0);

    sol_flow_send_empty_packet(node_out1, 0);
    ASSERT_EVENT_COUNT(node_in1, EVENT_PORT_PROCESS, 1);
    ASSERT_EVENT_COUNT(node_in2, EVENT_PORT_PROCESS, 1);

    sol_flow_send_empty_packet(node_out2, 0);
    ASSERT_EVENT_COUNT(node_in1, EVENT_PORT_PROCESS, 2);
    ASSERT_EVENT_COUNT(node_in2, EVENT_PORT_PROCESS, 1);

    sol_flow_node_del(flow);
}


DEFINE_TEST(connections_specs_must_be_ordered);

static void
connections_specs_must_be_ordered(void)
{
    struct sol_flow_node *flow;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 1, .src_port = 0, .dst = 0, .dst_port = 0 },
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, conns);
    ASSERT(!flow);
}


DEFINE_TEST(connections_specs_are_verified);

static void
connections_specs_are_verified(void)
{
    struct sol_flow_node *flow;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns_invalid_src[] = {
        { .src = 1234, .src_port = 0, .dst = 0, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns_invalid_dst[] = {
        { .src = 0, .src_port = 0, .dst = 1234, .dst_port = 0 },
        { .src = 1, .src_port = 0, .dst = 0, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns_invalid_src_port[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        { .src = 1, .src_port = 1234, .dst = 0, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns_invalid_dst_port[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        { .src = 1, .src_port = 0, .dst = 0, .dst_port = 1234 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, conns_invalid_src);
    ASSERT(!flow);

    flow = sol_flow_static_new(NULL, nodes, conns_invalid_dst);
    ASSERT(!flow);

    flow = sol_flow_static_new(NULL, nodes, conns_invalid_src_port);
    ASSERT(!flow);

    flow = sol_flow_static_new(NULL, nodes, conns_invalid_dst_port);
    ASSERT(!flow);
}


DEFINE_TEST(multiple_conns_to_the_same_in_port_have_different_conn_ids);

static void
multiple_conns_to_the_same_in_port_have_different_conn_ids(void)
{
    struct sol_flow_node *flow, *first_out, *second_out, *node_in;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "first node out" },
        [1] = { .type = &test_node_type, .name = "second node out" },
        [2] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { 0, 0, 2, 0 },
        { 0, 1, 2, 0 },
        { 1, 0, 2, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, conns);
    node_in = sol_flow_static_get_node(flow, 2);
    first_out = sol_flow_static_get_node(flow, 0);
    second_out = sol_flow_static_get_node(flow, 1);

    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_IN_CONNECT, 3);

    /* Connection IDs are sequential. */
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_IN_CONNECT, 0, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_IN_CONNECT, 1, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_IN_CONNECT, 2, 1);

    /* Connection IDs are local for each port. All out ports have conn with id 0. */
    ASSERT_EVENT_WITH_ID_COUNT(first_out, EVENT_PORT_OUT_CONNECT, 0, 2);
    ASSERT_EVENT_WITH_ID_COUNT(second_out, EVENT_PORT_OUT_CONNECT, 0, 1);

    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, 0);

    sol_flow_send_empty_packet(first_out, 0);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 0, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 1, 0);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 2, 0);

    sol_flow_send_empty_packet(first_out, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 0, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 1, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 2, 0);

    sol_flow_send_empty_packet(second_out, 0);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 0, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 1, 1);
    ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 2, 1);

    sol_flow_node_del(flow);
}


DEFINE_TEST(create_multiple_nodes_from_same_flow);

static void
create_multiple_nodes_from_same_flow(void)
{
    struct sol_flow_node *node1, *node2;
    struct sol_flow_node_type *type;

    type = test_flow_new_type();

    node1 = sol_flow_node_new(NULL, NULL, type, NULL);
    ASSERT(node1);
    ASSERT_EVENT_COUNT(NULL, EVENT_NODE_OPEN, 2);
    ASSERT_EVENT_COUNT(NULL, EVENT_NODE_CLOSE, 0);

    node2 = sol_flow_node_new(NULL, NULL, type, NULL);
    ASSERT(node2);
    ASSERT_EVENT_COUNT(NULL, EVENT_NODE_OPEN, 4);
    ASSERT_EVENT_COUNT(NULL, EVENT_NODE_CLOSE, 0);

    sol_flow_node_del(node1);
    sol_flow_node_del(node2);

    ASSERT_EVENT_COUNT(NULL, EVENT_NODE_CLOSE, 4);

    test_flow_del_type(type);
}


DEFINE_TEST(connect_callback_is_called_for_exported_in_port);

static void
connect_callback_is_called_for_exported_in_port(void)
{
    struct sol_flow_node_type *type;
    struct sol_flow_node *toplevel, *test_flow, *child_node_in;

    static struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, 0, .dst = 1, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    type = test_flow_new_type();
    nodes[1].type = type;

    toplevel = sol_flow_static_new(NULL, nodes, conns);
    test_flow = sol_flow_static_get_node(toplevel, 1);
    child_node_in = sol_flow_static_get_node(test_flow, 1);

    ASSERT_EVENT_COUNT(child_node_in, EVENT_PORT_IN_CONNECT, 2);
    ASSERT_EVENT_COUNT(child_node_in, EVENT_PORT_IN_DISCONNECT, 0);

    /* Test flow have internal connection in exported port, so conn_id
     * from the outside is 1. */
    ASSERT_EVENT_WITH_ID_COUNT(child_node_in, EVENT_PORT_IN_CONNECT, 0, 1);
    ASSERT_EVENT_WITH_ID_COUNT(child_node_in, EVENT_PORT_IN_CONNECT, 1, 1);

    sol_flow_node_del(toplevel);

    ASSERT_EVENT_COUNT(child_node_in, EVENT_PORT_IN_CONNECT, 2);
    ASSERT_EVENT_COUNT(child_node_in, EVENT_PORT_IN_DISCONNECT, 2);
    ASSERT_EVENT_WITH_ID_COUNT(child_node_in, EVENT_PORT_IN_DISCONNECT, 0, 1);
    ASSERT_EVENT_WITH_ID_COUNT(child_node_in, EVENT_PORT_IN_DISCONNECT, 1, 1);

    test_flow_del_type(type);
}


DEFINE_TEST(connect_callback_is_called_for_exported_out_port);

static void
connect_callback_is_called_for_exported_out_port(void)
{
    struct sol_flow_node_type *type;
    struct sol_flow_node *toplevel, *test_flow, *child_node_out;

    static struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 1, 0, .dst = 0, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    type = test_flow_new_type();
    nodes[1].type = type;

    toplevel = sol_flow_static_new(NULL, nodes, conns);
    test_flow = sol_flow_static_get_node(toplevel, 1);
    child_node_out = sol_flow_static_get_node(test_flow, 1);

    ASSERT_EVENT_COUNT(child_node_out, EVENT_PORT_OUT_CONNECT, 1);
    ASSERT_EVENT_COUNT(child_node_out, EVENT_PORT_OUT_DISCONNECT, 0);

    sol_flow_node_del(toplevel);

    ASSERT_EVENT_COUNT(child_node_out, EVENT_PORT_OUT_CONNECT, 1);
    ASSERT_EVENT_COUNT(child_node_out, EVENT_PORT_OUT_DISCONNECT, 1);

    test_flow_del_type(type);
}

static struct sol_flow_node_type *
test_other_flow_new_type(void)
{
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { .type = &test_node_type },
        [2] = { .type = &test_node_type },
        [3] = { .type = &test_node_type },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    /* Produce different number of connections for input and output ports. */
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 1, 0, .dst = 3, 0 },
        { .src = 2, 0, .dst = 2, 0 },
        { .src = 2, 0, .dst = 3, 0 },
        { .src = 3, 0, .dst = 1, 0 },
        { .src = 3, 0, .dst = 2, 0 },
        { .src = 3, 0, .dst = 3, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_in[] = {
        { 0, 0 },
        { 1, 0 },
        { 2, 0 },
        { 3, 0 },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_out[] = {
        { 0, 0 },
        { 1, 0 },
        { 2, 0 },
        { 3, 0 },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    return sol_flow_static_new_type(nodes, conns, exported_in, exported_out, NULL);
}

static void
test_other_flow_del_type(struct sol_flow_node_type *type)
{
    sol_flow_static_del_type(type);
}


DEFINE_TEST(conn_ids_are_mapped_when_reaching_exported_ports);

static void
conn_ids_are_mapped_when_reaching_exported_ports(void)
{
    struct sol_flow_node_type *type;
    struct sol_flow_node *toplevel, *other_flow, *child_node;
    int i, j;

    static struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    /* Two connections for each exported port (both in and out). */
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, 0, .dst = 1, 0 },
        { .src = 0, 0, .dst = 1, 1 },
        { .src = 0, 0, .dst = 1, 2 },
        { .src = 0, 0, .dst = 1, 3 },
        { .src = 0, 1, .dst = 1, 0 },
        { .src = 0, 1, .dst = 1, 1 },
        { .src = 0, 1, .dst = 1, 2 },
        { .src = 0, 1, .dst = 1, 3 },

        { .src = 1, 0, .dst = 0, 0 },
        { .src = 1, 0, .dst = 0, 1 },
        { .src = 1, 1, .dst = 0, 0 },
        { .src = 1, 1, .dst = 0, 1 },
        { .src = 1, 2, .dst = 0, 0 },
        { .src = 1, 2, .dst = 0, 1 },
        { .src = 1, 3, .dst = 0, 0 },
        { .src = 1, 3, .dst = 0, 1 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    type = test_other_flow_new_type();
    nodes[1].type = type;

    toplevel = sol_flow_static_new(NULL, nodes, conns);
    other_flow = sol_flow_static_get_node(toplevel, 1);

    for (i = 0; i < 4; i++) {
        int total_conns;
        child_node = sol_flow_static_get_node(other_flow, i);

        /* Each node 'i' will have 'i' connections (both out and in)
         * plus the ones from its parent flow. */
        total_conns = i + 2;

        ASSERT_EVENT_COUNT(child_node, EVENT_PORT_OUT_CONNECT, total_conns);
        ASSERT_EVENT_COUNT(child_node, EVENT_PORT_IN_CONNECT, total_conns);

        /* Each connection have its own id. Ids from connections
         * inside the flow will not conflict with ids from connections
         * from the outside. */
        for (j = 0; j < total_conns; j++) {
            ASSERT_EVENT_WITH_ID_COUNT(child_node, EVENT_PORT_OUT_CONNECT, j, 1);
            ASSERT_EVENT_WITH_ID_COUNT(child_node, EVENT_PORT_IN_CONNECT, j, 1);
        }
    }

    sol_flow_node_del(toplevel);

    test_other_flow_del_type(type);
}


DEFINE_TEST(send_packet_to_exported_in_port);

static void
send_packet_to_exported_in_port(void)
{
    struct sol_flow_node_type *type;
    struct sol_flow_node *toplevel, *node_out, *test_flow, *child_node_in;

    static struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, 0, .dst = 1, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    int i;

    type = test_flow_new_type();
    nodes[1].type = type;

    toplevel = sol_flow_static_new(NULL, nodes, conns);
    node_out = sol_flow_static_get_node(toplevel, 0);
    test_flow = sol_flow_static_get_node(toplevel, 1);
    child_node_in = sol_flow_static_get_node(test_flow, 1);

    ASSERT_EVENT_COUNT(child_node_in, EVENT_PORT_PROCESS, 0);

    for (i = 1; i < 10; i++) {
        sol_flow_send_empty_packet(node_out, 0);
        ASSERT_EVENT_COUNT(child_node_in, EVENT_PORT_PROCESS, i);

        /* Test flow have internal connection in exported port, so
         * conn_id from packets from the outside is 1. */
        ASSERT_EVENT_WITH_ID_COUNT(child_node_in, EVENT_PORT_PROCESS, 0, 0);
        ASSERT_EVENT_WITH_ID_COUNT(child_node_in, EVENT_PORT_PROCESS, 1, i);
    }

    sol_flow_node_del(toplevel);
    test_flow_del_type(type);
}


DEFINE_TEST(send_packet_to_multiple_flows);

static void
send_packet_to_multiple_flows(void)
{
    struct sol_flow_node_type *type;
    struct sol_flow_node *toplevel, *node_out;
    static struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { NULL },
        [2] = { NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        { .src = 0, .src_port = 0, .dst = 2, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    int i;

    type = test_flow_new_type();
    nodes[1].type = type;
    nodes[2].type = type;

    toplevel = sol_flow_static_new(NULL, nodes, conns);
    node_out = sol_flow_static_get_node(toplevel, 0);

    ASSERT_EVENT_COUNT(NULL, EVENT_PORT_PROCESS, 0);

    for (i = 1; i < 10; i++) {
        sol_flow_send_empty_packet(node_out, 0);
        ASSERT_EVENT_COUNT(NULL, EVENT_PORT_PROCESS, 2 * i);
    }

    sol_flow_node_del(toplevel);
    test_flow_del_type(type);
}


DEFINE_TEST(send_packet_to_exported_out_port);

static void
send_packet_to_exported_out_port(void)
{
    struct sol_flow_node_type *type;
    struct sol_flow_node *toplevel, *node_in, *node_out, *test_flow, *child_node_out;

    static struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { .type = &test_node_type },
        [2] = { NULL },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 1, 0, .dst = 0, 0 },
        { .src = 2, 0, .dst = 0, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    int i;

    type = test_flow_new_type();
    nodes[2].type = type;

    toplevel = sol_flow_static_new(NULL, nodes, conns);
    node_in = sol_flow_static_get_node(toplevel, 0);
    node_out = sol_flow_static_get_node(toplevel, 1);
    test_flow = sol_flow_static_get_node(toplevel, 2);
    child_node_out = sol_flow_static_get_node(test_flow, 1);

    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, 0);

    /* Send to a non exported port doesn't have any effect on outside. */
    sol_flow_send_empty_packet(child_node_out, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, 0);

    /* Send to an exported port. */
    for (i = 1; i < 10; i++) {
        sol_flow_send_empty_packet(child_node_out, 0);
        ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, i);
        ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 0, 0);
        ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 1, i);
    }

    /* Send to an exported node from a sibling node. This is here to check conn_id is sane. */
    for (i = 1; i < 10; i++) {
        sol_flow_send_empty_packet(node_out, 0);
        ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, 9 + i);
        ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 0, i);
        ASSERT_EVENT_WITH_ID_COUNT(node_in, EVENT_PORT_PROCESS, 1, 9);
    }

    sol_flow_node_del(toplevel);
    test_flow_del_type(type);
}


DEFINE_TEST(exported_specs_must_be_ordered);

static void
exported_specs_must_be_ordered(void)
{
    struct sol_flow_node_type *type;

    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type },
        [1] = { .type = &test_node_type },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        { .src = 1, .src_port = 0, .dst = 0, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_port_spec exported_in[] = {
        { 1, 0 },
        { 0, 0 },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };

    type = sol_flow_static_new_type(nodes, conns, exported_in, NULL, NULL);
    ASSERT(!type);
}


DEFINE_TEST(initial_packet);

static void
initial_packet(void)
{
    struct sol_flow_node *flow, *node_in;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    test_initial_data = true;
    flow = sol_flow_static_new(NULL, nodes, conns);
    node_in = sol_flow_static_get_node(flow, 1);

    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_IN_CONNECT, 1);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_PROCESS, 1);
    test_initial_data = false;

    sol_flow_node_del(flow);
}


DEFINE_TEST(connect_two_nodes_match_packet_types);

static void
connect_two_nodes_match_packet_types(void)
{
    struct sol_flow_node *flow;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec empty_to_boolean_conns[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 2 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec boolean_to_empty_conns[] = {
        { .src = 0, .src_port = 2, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec boolean_to_any_conns[] = {
        { .src = 0, .src_port = 2, .dst = 1, .dst_port = 3 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec any_to_empty_conns[] = {
        { .src = 0, .src_port = 3, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec any_to_any_conns[] = {
        { .src = 0, .src_port = 3, .dst = 1, .dst_port = 3 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    flow = sol_flow_static_new(NULL, nodes, empty_to_boolean_conns);
    ASSERT(!flow);

    flow = sol_flow_static_new(NULL, nodes, boolean_to_empty_conns);
    ASSERT(!flow);

    flow = sol_flow_static_new(NULL, nodes, boolean_to_any_conns);
    ASSERT(flow);
    sol_flow_node_del(flow);

    flow = sol_flow_static_new(NULL, nodes, any_to_empty_conns);
    ASSERT(flow);
    sol_flow_node_del(flow);

    flow = sol_flow_static_new(NULL, nodes, any_to_any_conns);
    ASSERT(flow);
    sol_flow_node_del(flow);
}


DEFINE_TEST(send_packets_match_packet_types);

static void
send_packets_match_packet_types(void)
{
    struct sol_flow_node *flow, *node_out;
    static const struct sol_flow_static_node_spec nodes[] = {
        [0] = { .type = &test_node_type, .name = "node out" },
        [1] = { .type = &test_node_type, .name = "node in" },
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns1[] = {
        { .src = 0, .src_port = 0, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    static const struct sol_flow_static_conn_spec conns2[] = {
        { .src = 0, .src_port = 3, .dst = 1, .dst_port = 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };
    int r;

    /* Sending BOOLEAN packet to EMPTY port */
    flow = sol_flow_static_new(NULL, nodes, conns1);
    node_out = sol_flow_static_get_node(flow, 0);
    r = sol_flow_send_boolean_packet(node_out, 0, true);
    ASSERT(r < 0);
    sol_flow_node_del(flow);

    /* Sending EMPTY packet to ANY port */
    flow = sol_flow_static_new(NULL, nodes, conns2);
    node_out = sol_flow_static_get_node(flow, 0);
    r = sol_flow_send_empty_packet(node_out, 0);
    ASSERT(r == 0);
    sol_flow_node_del(flow);
}


DEFINE_TEST(node_options_from_strv);

static void
node_options_from_strv(void)
{
    const char *timer_strv[2] = { "interval=1000", NULL };
    const char *timer_irange_strv[2] = { "interval=50|20|60|2", NULL };
    const char *timer_irange_different_format_strv[2] = { "interval=val:100|min:10|max:200|step:5", NULL };
#ifdef HARDWARE_PWM
    const char *pwm_strv[6] = { "chip=2", "pin=7", "enabled=true", "period=42", "duty_cycle=88", NULL };
#endif
    const char *console_strv[4] = { "prefix=console prefix:", "suffix=. suffix!", "output_on_stdout=true", NULL };
    const char *timer_unknown_field_strv[2] = { "this_is_not_a_valid_field=100", NULL };
    const char *timer_wrongly_formatted_strv[2] = { "interval = 1000", NULL };

    struct sol_flow_node_type_timer_options *timer_opts;
#ifdef HARDWARE_PWM
    struct sol_flow_node_type_pwm_options *pwm_opts;
#endif
    struct sol_flow_node_type_console_options *console_opts;
    struct sol_flow_node_options *opts;

    /* One option */
    timer_opts = (struct sol_flow_node_type_timer_options *)
                 sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_TIMER, timer_strv);
    ASSERT(timer_opts);
    ASSERT_INT_EQ(timer_opts->interval.val, 1000);
    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_TIMER, (struct sol_flow_node_options *)timer_opts);

#ifdef HARDWARE_PWM
    /* Multiple options */
    pwm_opts = (struct sol_flow_node_type_pwm_options *)
               sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_PWM, pwm_strv);
    ASSERT(pwm_opts);
    ASSERT_INT_EQ(pwm_opts->chip.val, 2);
    ASSERT_INT_EQ(pwm_opts->pin.val, 7);
    ASSERT_INT_EQ(pwm_opts->enabled, true);
    ASSERT_INT_EQ(pwm_opts->period.val, 42);
    ASSERT_INT_EQ(pwm_opts->duty_cycle.val, 88);
    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_PWM, (struct sol_flow_node_options *)pwm_opts);
#endif

    /* String options */
    console_opts = (struct sol_flow_node_type_console_options *)
                   sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_CONSOLE, console_strv);
    ASSERT(console_opts);
    ASSERT(streq(console_opts->prefix, "console prefix:"));
    ASSERT(streq(console_opts->suffix, ". suffix!"));
    ASSERT_INT_EQ(console_opts->output_on_stdout, true);
    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_CONSOLE, (struct sol_flow_node_options *)console_opts);

    /* Irange options */
    timer_opts = (struct sol_flow_node_type_timer_options *)
                 sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_TIMER, timer_irange_strv);
    ASSERT(timer_opts);
    ASSERT_INT_EQ(timer_opts->interval.val, 50);
    ASSERT_INT_EQ(timer_opts->interval.step, 2);
    ASSERT_INT_EQ(timer_opts->interval.min, 20);
    ASSERT_INT_EQ(timer_opts->interval.max, 60);
    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_TIMER, (struct sol_flow_node_options *)timer_opts);

    /* Irange different format options */
    timer_opts = (struct sol_flow_node_type_timer_options *)
                 sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_TIMER, timer_irange_different_format_strv);
    ASSERT(timer_opts);
    ASSERT_INT_EQ(timer_opts->interval.val, 100);
    ASSERT_INT_EQ(timer_opts->interval.step, 5);
    ASSERT_INT_EQ(timer_opts->interval.min, 10);
    ASSERT_INT_EQ(timer_opts->interval.max, 200);
    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_TIMER, (struct sol_flow_node_options *)timer_opts);

    /* Unknown field options */
    opts = sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_TIMER, timer_unknown_field_strv);
    ASSERT(!opts);

    /* Wrongly formatted options */
    opts = sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_TIMER, timer_wrongly_formatted_strv);
    ASSERT(!opts);
}


DEFINE_TEST(merge_options);

static void
merge_options(void)
{
    struct sol_flow_node_type_console_options *opts;
    int err;

    const char *original_strv[] = {
        "prefix=original_prefix",
        "suffix=original_suffix",
        "output_on_stdout=true",
        NULL
    };

    const char *to_merge_strv[] = {
        "prefix=merged_prefix",
        "output_on_stdout=false",
        NULL
    };

    opts = (struct sol_flow_node_type_console_options *)
           sol_flow_node_options_new_from_strv(SOL_FLOW_NODE_TYPE_CONSOLE, original_strv);
    ASSERT(opts);

    ASSERT(streq(opts->prefix, "original_prefix"));
    ASSERT(streq(opts->suffix, "original_suffix"));
    ASSERT(opts->output_on_stdout);

    err = sol_flow_node_options_merge_from_strv(SOL_FLOW_NODE_TYPE_CONSOLE, &opts->base, to_merge_strv);
    ASSERT(err >= 0);

    ASSERT(streq(opts->prefix, "merged_prefix"));
    ASSERT(streq(opts->suffix, "original_suffix"));
    ASSERT(!opts->output_on_stdout);

    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_CONSOLE, &opts->base);
}


DEFINE_TEST(copy_options);

static void
copy_options(void)
{
    struct sol_flow_node_type_console_options opts = {}, *copied_opts;
    char prefix[] = { 'A', 'B', 'C', '\0' };

    opts.base.api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION;
    opts.base.sub_api = SOL_FLOW_NODE_TYPE_CONSOLE_OPTIONS_API_VERSION;
    opts.prefix = prefix;
    opts.output_on_stdout = true;
    opts.flush = false;

    copied_opts = (struct sol_flow_node_type_console_options *)
                  sol_flow_node_options_copy(SOL_FLOW_NODE_TYPE_CONSOLE, &opts.base);
    ASSERT(copied_opts);

    /* Will touch some of the values after the copy. */
    prefix[0] = 'X';
    prefix[1] = '\0';
    opts.output_on_stdout = false;

    ASSERT(streq(copied_opts->prefix, "ABC"));
    ASSERT(copied_opts->output_on_stdout);
    ASSERT(!copied_opts->flush);

    sol_flow_node_options_del(SOL_FLOW_NODE_TYPE_CONSOLE, &copied_opts->base);
}


DEFINE_TEST(need_a_valid_type_to_create_packets);

static void
need_a_valid_type_to_create_packets(void)
{
    struct sol_flow_packet *packet_null, *packet_any, *packet_invalid_type;

    static const struct sol_flow_packet_type invalid_type = {
        .api_version = 0, /* Invalid API version */
    };

    packet_null = sol_flow_packet_new(NULL, NULL);
    ASSERT(!packet_null);

    packet_any = sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_ANY, NULL);
    ASSERT(!packet_any);

    packet_invalid_type = sol_flow_packet_new(&invalid_type, NULL);
    ASSERT(!packet_invalid_type);
}


TEST_MAIN_WITH_RESET_FUNC(clear_events);
