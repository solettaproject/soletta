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
#include "sol-flow-static.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-log.h"

#include "sol-flow/console.h"
#ifdef USE_PWM
#include "sol-flow/pwm.h"
#endif
#include "sol-flow/timer.h"
#include "sol-flow/int.h"

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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_out_disconnect,
};

static struct sol_flow_port_type_in test_port_in = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .disconnect = test_port_in_disconnect,
    .connect = test_port_in_connect,
    .process = test_port_process
};

static struct sol_flow_port_type_in test_port_match_in = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_in_connect,
    .disconnect = test_port_in_disconnect,
    .process = test_port_process,
};

static struct sol_flow_port_type_out test_port_match_out = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_out_disconnect,
};

static struct sol_flow_port_type_in test_port_any_in = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_in_connect,
    .disconnect = test_port_in_disconnect,
    .process = test_port_process,
};

static struct sol_flow_port_type_out test_port_any_out = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION, )
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
test_node_init_type(void)
{
    if (!test_port_in.packet_type) {
        test_port_in.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
        test_port_out.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
        test_port_match_in.packet_type = SOL_FLOW_PACKET_TYPE_BOOLEAN;
        test_port_match_out.packet_type = SOL_FLOW_PACKET_TYPE_BOOLEAN;
        test_port_any_in.packet_type = SOL_FLOW_PACKET_TYPE_ANY;
        test_port_any_out.packet_type = SOL_FLOW_PACKET_TYPE_ANY;
    }
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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )

    /* Forces unaligned size, so the storage need to take that into account. */
    .data_size = sizeof(char),

    .open = test_node_open,
    .close = test_node_close,

    .init_type = test_node_init_type,

    .ports_in_count = SOL_UTIL_ARRAY_SIZE(test_ports_in),
    .ports_out_count = SOL_UTIL_ARRAY_SIZE(test_ports_out),
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

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .exported_out = exported_out,
    };

    return sol_flow_static_new_type(&spec);
}

static void
test_flow_del_type(struct sol_flow_node_type *type)
{
    sol_flow_node_type_del(type);
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

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .exported_out = exported_out,
    };

    return sol_flow_static_new_type(&spec);
}

static void
test_other_flow_del_type(struct sol_flow_node_type *type)
{
    sol_flow_node_type_del(type);
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

    static const struct sol_flow_static_spec spec = {
        SOL_SET_API_VERSION(.api_version = SOL_FLOW_STATIC_API_VERSION, )
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
    };

    type = sol_flow_static_new_type(&spec);
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


#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
DEFINE_TEST(named_options_init_from_strv);

static void
named_options_init_from_strv(void)
{
    struct sol_flow_node_named_options named_opts;
    struct sol_flow_node_named_options_member *m;
    const struct sol_flow_node_type *node_type;
    int r;

    ASSERT(sol_flow_get_node_type("int", SOL_FLOW_NODE_TYPE_INT_ACCUMULATOR, &node_type) == 0);
    {
        const char *strv[] = { "initial_value=1000", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type,
            strv);
        ASSERT(r >= 0);
        ASSERT_INT_EQ(named_opts.count, SOL_UTIL_ARRAY_SIZE(strv) - 1);

        m = named_opts.members;
        ASSERT_STR_EQ(m->name, "initial_value");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_INT);
        ASSERT_INT_EQ(m->i, 1000);

        sol_flow_node_named_options_fini(&named_opts);
    }

    {
        const char *strv[] = { "setup_value=20|60|2", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type,
            strv);
        ASSERT(r >= 0);
        ASSERT_INT_EQ(named_opts.count, SOL_UTIL_ARRAY_SIZE(strv) - 1);

        m = named_opts.members;
        ASSERT_STR_EQ(m->name, "setup_value");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE_SPEC);
        ASSERT_INT_EQ(m->irange_spec.min, 20);
        ASSERT_INT_EQ(m->irange_spec.max, 60);
        ASSERT_INT_EQ(m->irange_spec.step, 2);

        sol_flow_node_named_options_fini(&named_opts);
    }

    {
        const char *strv[] = { "setup_value=min:10|max:200|step:5", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type,
            strv);
        ASSERT(r >= 0);
        ASSERT_INT_EQ(named_opts.count, SOL_UTIL_ARRAY_SIZE(strv) - 1);

        m = named_opts.members;
        ASSERT_STR_EQ(m->name, "setup_value");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE_SPEC);
        ASSERT_INT_EQ(m->irange_spec.min, 10);
        ASSERT_INT_EQ(m->irange_spec.max, 200);
        ASSERT_INT_EQ(m->irange_spec.step, 5);

        sol_flow_node_named_options_fini(&named_opts);
    }

    {
        const char *strv[] = { "this_is_not_a_valid_field=100", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type,
            strv);
        ASSERT(r < 0);
    }

    {
        const char *wrong_formatting_strv[] = { "initial_value = 1000", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type,
            wrong_formatting_strv);
        ASSERT(r < 0);
    }

#ifdef USE_PWM
    ASSERT(sol_flow_get_node_type("pwm", SOL_FLOW_NODE_TYPE_PWM, &node_type) == 0);
    {
        const char *strv[] = { "pin=2 7", "raw=true", "enabled=true", "period=42", "duty_cycle=88", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type, strv);
        ASSERT(r >= 0);
        ASSERT_INT_EQ(named_opts.count, SOL_UTIL_ARRAY_SIZE(strv) - 1);

        m = named_opts.members;
        ASSERT_STR_EQ(m->name, "pin");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_STRING);
        ASSERT_STR_EQ(m->string, "2 7");

        m++;
        ASSERT_STR_EQ(m->name, "raw");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN);
        ASSERT(m->boolean);

        m++;
        ASSERT_STR_EQ(m->name, "enabled");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN);
        ASSERT(m->boolean);

        m++;
        ASSERT_STR_EQ(m->name, "period");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_INT);
        ASSERT_INT_EQ(m->i, 42);

        m++;
        ASSERT_STR_EQ(m->name, "duty_cycle");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_INT);
        ASSERT_INT_EQ(m->i, 88);

        sol_flow_node_named_options_fini(&named_opts);
    }
#endif

    ASSERT(sol_flow_get_node_type("console", SOL_FLOW_NODE_TYPE_CONSOLE, &node_type) == 0);
    {
        const char *strv[] = { "prefix=console prefix:", "suffix=. suffix!", "output_on_stdout=true", NULL };

        r = sol_flow_node_named_options_init_from_strv(&named_opts, node_type, strv);
        ASSERT(r >= 0);
        ASSERT_INT_EQ(named_opts.count, SOL_UTIL_ARRAY_SIZE(strv) - 1);

        m = named_opts.members;
        ASSERT_STR_EQ(m->name, "prefix");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_STRING);
        ASSERT_STR_EQ(m->string, "console prefix:");

        m++;
        ASSERT_STR_EQ(m->name, "suffix");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_STRING);
        ASSERT_STR_EQ(m->string, ". suffix!");

        m++;
        ASSERT_STR_EQ(m->name, "output_on_stdout");
        ASSERT(m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN);
        ASSERT(m->boolean);

        sol_flow_node_named_options_fini(&named_opts);
    }
}
#endif


#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
DEFINE_TEST(node_options_new);

static void
node_options_new(void)
{
    struct sol_flow_node_named_options_member one_option[] = {
        { .name = "interval", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_INT, .i = 1000 },
    };

    struct sol_flow_node_named_options_member multiple_options[] = {
        { .name = "pin", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_STRING, .string = "2 7" },
        { .name = "raw", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN, .boolean = true },
        { .name = "enabled", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN, .boolean = true },
        { .name = "period", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_INT, .i = 42 },
        { .name = "duty_cycle", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_INT, .i = 88 },
    };

    struct sol_flow_node_named_options_member string_options[] = {
        { .name = "prefix", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_STRING, .string = "console prefix:" },
        { .name = "suffix", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_STRING, .string = ". suffix!" },
        { .name = "output_on_stdout", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN, .boolean = true },
    };

    struct sol_flow_node_named_options_member unknown_option[] = {
        { .name = "this_is_not_a_valid_field", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN, .boolean = true },
    };

    struct sol_flow_node_named_options_member wrong_type[] = {
        { .name = "interval", .type = SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN, .boolean = true },
    };

    struct sol_flow_node_type_timer_options *timer_opts;

#ifdef USE_PWM
    struct sol_flow_node_type_pwm_options *pwm_opts;
#endif
    struct sol_flow_node_type_console_options *console_opts;
    struct sol_flow_node_options *opts;
    struct sol_flow_node_named_options named_opts;
    const struct sol_flow_node_type *node_type;
    int r;

    /* One option */
    ASSERT(sol_flow_get_node_type("timer", SOL_FLOW_NODE_TYPE_TIMER, &node_type) == 0);
    named_opts.members = one_option;
    named_opts.count = SOL_UTIL_ARRAY_SIZE(one_option);
    r = sol_flow_node_options_new(node_type, &named_opts, &opts);
    ASSERT(r >= 0);
    timer_opts = (struct sol_flow_node_type_timer_options *)opts;
    ASSERT_INT_EQ(timer_opts->interval, 1000);
    sol_flow_node_options_del(node_type, opts);

    /* Unknown option */
    named_opts.members = unknown_option;
    named_opts.count = SOL_UTIL_ARRAY_SIZE(unknown_option);
    r = sol_flow_node_options_new(node_type, &named_opts, &opts);
    ASSERT(r < 0);

    /* Wrong type */
    named_opts.members = wrong_type;
    named_opts.count = SOL_UTIL_ARRAY_SIZE(wrong_type);
    r = sol_flow_node_options_new(node_type, &named_opts, &opts);
    ASSERT(r < 0);

#ifdef USE_PWM
    /* Multiple options */
    ASSERT(sol_flow_get_node_type("pwm", SOL_FLOW_NODE_TYPE_PWM, &node_type) == 0);
    named_opts.members = multiple_options;
    named_opts.count = SOL_UTIL_ARRAY_SIZE(multiple_options);
    r = sol_flow_node_options_new(node_type, &named_opts, &opts);
    ASSERT(r >= 0);
    pwm_opts = (struct sol_flow_node_type_pwm_options *)opts;
    ASSERT_STR_EQ(pwm_opts->pin, "2 7");
    ASSERT_INT_EQ(pwm_opts->raw, true);
    ASSERT_INT_EQ(pwm_opts->enabled, true);
    ASSERT_INT_EQ(pwm_opts->period, 42);
    ASSERT_INT_EQ(pwm_opts->duty_cycle, 88);
    sol_flow_node_options_del(node_type, opts);
#endif

    /* String options */
    ASSERT(sol_flow_get_node_type("console", SOL_FLOW_NODE_TYPE_CONSOLE, &node_type) == 0);
    named_opts.members = string_options;
    named_opts.count = SOL_UTIL_ARRAY_SIZE(string_options);
    r = sol_flow_node_options_new(node_type, &named_opts, &opts);
    ASSERT(r >= 0);
    console_opts = (struct sol_flow_node_type_console_options *)opts;
    ASSERT(streq(console_opts->prefix, "console prefix:"));
    ASSERT(streq(console_opts->suffix, ". suffix!"));
    ASSERT_INT_EQ(console_opts->output_on_stdout, true);
    sol_flow_node_options_del(node_type, opts);
}
#endif

DEFINE_TEST(need_a_valid_type_to_create_packets);

static void
need_a_valid_type_to_create_packets(void)
{
    struct sol_flow_packet *packet_null, *packet_any;

#ifndef SOL_NO_API_VERSION
    struct sol_flow_packet *packet_invalid_type;

    static const struct sol_flow_packet_type invalid_type = {
        .api_version = 0, /* Invalid API version */
    };
#endif

    packet_null = sol_flow_packet_new(NULL, NULL);
    ASSERT(!packet_null);

    packet_any = sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_ANY, NULL);
    ASSERT(!packet_any);

#ifndef SOL_NO_API_VERSION
    packet_invalid_type = sol_flow_packet_new(&invalid_type, NULL);
    ASSERT(!packet_invalid_type);
#endif
}

DEFINE_TEST(test_find_port);

static void
test_find_port(void)
{
    const struct sol_flow_node_type *node_type;
    uint16_t idx;

    ASSERT(sol_flow_get_node_type("boolean", SOL_FLOW_NODE_TYPE_BOOLEAN_AND, &node_type) == 0);

    idx = sol_flow_node_find_port_out(node_type, "OUT");
    ASSERT_INT_EQ(idx, 0);

    idx = sol_flow_node_find_port_out(node_type, "NON-EXISTENT");
    ASSERT_INT_EQ(idx, UINT16_MAX);

    idx = sol_flow_node_find_port_in(node_type, "IN");
    ASSERT_INT_EQ(idx, UINT16_MAX);

    idx = sol_flow_node_find_port_in(node_type, "OUT[0]");
    ASSERT_INT_EQ(idx, UINT16_MAX);

    idx = sol_flow_node_find_port_in(node_type, "IN[0]");
    ASSERT_INT_EQ(idx, 0);
    idx = sol_flow_node_find_port_in(node_type, "IN[ 0 ]");
    ASSERT_INT_EQ(idx, 0);
    idx = sol_flow_node_find_port_in(node_type, "IN[ 0");
    ASSERT_INT_EQ(idx, UINT16_MAX);
    idx = sol_flow_node_find_port_in(node_type, "IN[");
    ASSERT_INT_EQ(idx, UINT16_MAX);
    idx = sol_flow_node_find_port_in(node_type, "IN[]");
    ASSERT_INT_EQ(idx, UINT16_MAX);
    idx = sol_flow_node_find_port_in(node_type, "IN[X");
    ASSERT_INT_EQ(idx, UINT16_MAX);
    idx = sol_flow_node_find_port_in(node_type, "IN[-123]");
    ASSERT_INT_EQ(idx, UINT16_MAX);
    idx = sol_flow_node_find_port_in(node_type, "IN[1234567]");
    ASSERT_INT_EQ(idx, UINT16_MAX);

    idx = sol_flow_node_find_port_in(node_type, "IN[1]");
    ASSERT_INT_EQ(idx, 1);

    idx = sol_flow_node_find_port_in(node_type, "IN[2]");
    ASSERT_INT_EQ(idx, 2);

    idx = sol_flow_node_find_port_in(node_type, "NON-EXISTENT");
    ASSERT_INT_EQ(idx, UINT16_MAX);
}

TEST_MAIN_WITH_RESET_FUNC(clear_events);
