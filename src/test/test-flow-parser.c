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
#include "sol-flow-parser.h"
#include "sol-flow-builder.h"
#include "sol-flow-resolver.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "test.h"

#define ASSERT_EVENT_COUNT(node, event, count) \
    ASSERT_INT_EQ(count_events(node, event), count);

enum event_type {
    EVENT_NONE,
    EVENT_PORT_CONNECT,
    EVENT_PORT_DISCONNECT,
};

struct test_event {
    struct sol_flow_node *node;
    enum event_type type;
};

static struct sol_vector test_events = SOL_VECTOR_INIT(struct test_event);

static void
add_event(struct sol_flow_node *node, enum event_type type)
{
    struct test_event *ev;

    ev = sol_vector_append(&test_events);
    ev->node = node;
    ev->type = type;
}

static int
count_events(struct sol_flow_node *node, enum event_type type)
{
    struct test_event *ev;
    int i, count = 0;

    SOL_VECTOR_FOREACH_IDX (&test_events, ev, i) {
        if (node && ev->node != node)
            continue;
        if (type != EVENT_NONE && ev->type != type)
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
test_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_CONNECT);
    return 0;
}

static int
test_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_CONNECT);
    return 0;
}

static int
test_port_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_DISCONNECT);
    return 0;
}

static struct sol_flow_port_type_out test_port_out = {
    .api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_disconnect,
};

static struct sol_flow_port_type_in test_port_in = {
    .api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION,
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_in_connect,
    .disconnect = test_port_disconnect,
};

static const struct sol_flow_port_type_in *test_ports_in[] = {
    &test_port_in,
    &test_port_in,
};

static const struct sol_flow_port_type_out *test_ports_out[] = {
    &test_port_out,
    &test_port_out,
};

static void
test_node_get_ports_counts(const struct sol_flow_node_type *type, uint16_t *ports_in_count, uint16_t *ports_out_count)
{
    if (!test_port_in.packet_type) {
        test_port_in.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
        test_port_out.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
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

static const struct sol_flow_node_type_description test_node_description = {
    .ports_in = (const struct sol_flow_port_description *const []){
        &((const struct sol_flow_port_description){
              .name = "IN1",
          }),
        &((const struct sol_flow_port_description){
              .name = "IN2",
          }),
        NULL
    },
    .ports_out = (const struct sol_flow_port_description *const []){
        &((const struct sol_flow_port_description){
              .name = "OUT1",
          }),
        &((const struct sol_flow_port_description){
              .name = "OUT2",
          }),
        NULL
    },
};

static const struct sol_flow_node_type test_node_type = {
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,

    .get_ports_counts = test_node_get_ports_counts,
    .get_port_in = test_node_get_port_in,
    .get_port_out = test_node_get_port_out,

    .description = &test_node_description,
};

static const struct sol_flow_node_type_description test_wrong_out_node_description = {
    .ports_out = (const struct sol_flow_port_description *const []){
        &((const struct sol_flow_port_description){
              .name = "OUT",
          }),
        &((const struct sol_flow_port_description){
              .name = "OUT",
          }),
        NULL
    },
};

static const struct sol_flow_node_type test_wrong_out_node_type = {
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
    .description = &test_wrong_out_node_description,
};

static const struct sol_flow_node_type_description test_wrong_out2_node_description = {
    .ports_out = (const struct sol_flow_port_description *const []){
        &((const struct sol_flow_port_description){
          }),
        &((const struct sol_flow_port_description){
              .name = "OUT",
          }),
        NULL
    },
};

static const struct sol_flow_node_type test_wrong_out2_node_type = {
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
    .description = &test_wrong_out2_node_description,
};

static const struct sol_flow_node_type_description test_wrong_in_node_description = {
    .ports_in = (const struct sol_flow_port_description *const []){
        &((const struct sol_flow_port_description){
              .name = "IN",
          }),
        &((const struct sol_flow_port_description){
              .name = "IN",
          }),
        NULL
    },
};

static const struct sol_flow_node_type test_wrong_in_node_type = {
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
    .description = &test_wrong_in_node_description,
};

static const struct sol_flow_node_type_description test_wrong_in2_node_description = {
    .ports_in = (const struct sol_flow_port_description *const []){
        &((const struct sol_flow_port_description){
              .name = "IN",
          }),
        &((const struct sol_flow_port_description){
          }),
        NULL
    },
};

static const struct sol_flow_node_type test_wrong_in2_node_type = {
    .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
    .description = &test_wrong_in2_node_description,
};

DEFINE_TEST(parse_with_string);

static void
parse_with_string(void)
{
    struct sol_flow_node *flow;
    struct sol_flow_parser *parser;
    const struct sol_flow_resolver *builtins_resolver;
    unsigned int i;

    static const char *tests[] = {
        "node_alone(boolean/not)",
        "a(boolean/not) OUT -> IN b(boolean/not)",
        "a(boolean/not) OUT -> IN b(boolean/not) OUT -> IN c(boolean/not)",
    };

    builtins_resolver = sol_flow_get_builtins_resolver();
    ASSERT(builtins_resolver);

    parser = sol_flow_parser_new(NULL, builtins_resolver);
    ASSERT(parser);

    for (i = 0; i < ARRAY_SIZE(tests); i++) {
        const char *input = tests[i];
        struct sol_flow_node_type *type;

        type = sol_flow_parse_string(parser, input, NULL);
        if (!type) {
            SOL_WRN("Failed to create type using parser with string '%s'", input);
            ASSERT(false);
        }

        flow = sol_flow_node_new(NULL, "test", type, NULL);
        if (!flow) {
            SOL_WRN("Failed to create node from type parsed from string '%s'", input);
            ASSERT(false);
        }

        sol_flow_node_del(flow);
    }

    sol_flow_parser_del(parser);
}

DEFINE_TEST(parse_and_fail_with_invalid_string);

static void
parse_and_fail_with_invalid_string(void)
{
    struct sol_flow_parser *parser;
    const struct sol_flow_resolver *builtins_resolver;
    unsigned int i;

    static const char *tests[] = {
        "a",
        "a(boolean/not) OUT in b(boolean/not)",
        "-> -> ->",

        "a(boolean/not) OUT -> IN b(node-type-that-doesnt-exist)",
        "a(boolean/not) PORT_THAT_DOESNT-exist -> IN b(boolean/not)",
    };

    builtins_resolver = sol_flow_get_builtins_resolver();
    ASSERT(builtins_resolver);

    parser = sol_flow_parser_new(NULL, builtins_resolver);
    ASSERT(parser);

    for (i = 0; i < ARRAY_SIZE(tests); i++) {
        const char *input = tests[i];
        struct sol_flow_node_type *type;

        type = sol_flow_parse_string(parser, input, NULL);
        if (type) {
            SOL_WRN("Failed. Expected string '%s' to fail parse, but it did not.", input);
            ASSERT(false);
        }
    }

    sol_flow_parser_del(parser);
}

static int
test_resolve(void *data, const char *id,
    struct sol_flow_node_type const **type,
    char const ***opts_strv)
{
    *type = &test_node_type;
    *opts_strv = NULL;
    return 0;
}

static const struct sol_flow_resolver test_resolver = {
    .api_version = SOL_FLOW_RESOLVER_API_VERSION,
    .name = "test_resolver",
    .resolve = test_resolve,
};

DEFINE_TEST(parse_with_string_and_resolver);

static void
parse_with_string_and_resolver(void)
{
    struct sol_flow_node *flow;
    struct sol_flow_parser *parser;
    struct sol_flow_node_type *type;

    static const char input[] = "a(whatever) OUT1 -> IN1 b(whatever)";

    parser = sol_flow_parser_new(NULL, &test_resolver);
    ASSERT(parser);

    type = sol_flow_parse_string(parser, input, NULL);
    ASSERT(type);

    flow = sol_flow_node_new(NULL, "test", type, NULL);
    ASSERT(flow);
    ASSERT_EVENT_COUNT(NULL, EVENT_PORT_CONNECT, 2);
    ASSERT_EVENT_COUNT(NULL, EVENT_PORT_DISCONNECT, 0);

    sol_flow_node_del(flow);
    ASSERT_EVENT_COUNT(NULL, EVENT_PORT_CONNECT, 2);
    ASSERT_EVENT_COUNT(NULL, EVENT_PORT_DISCONNECT, 2);

    sol_flow_parser_del(parser);
}


DEFINE_TEST(exported_ports);

static void
exported_ports(void)
{
    struct sol_flow_parser *parser;
    struct sol_flow_node_type *type;
    uint16_t count_in = 0, count_out = 0;

    static const char input[] =
        "OUTPORT=a.OUT1:OUTPUT_PORT\n"
        "INPORT=b.IN1:INPUT_PORT\n"
        "a(whatever) OUT1 -> IN1 b(whatever)";

    parser = sol_flow_parser_new(NULL, &test_resolver);
    ASSERT(parser);

    type = sol_flow_parse_string(parser, input, NULL);
    ASSERT(type);

    type->get_ports_counts(type, &count_in, &count_out);
    ASSERT_INT_EQ(count_in, 1);
    ASSERT_INT_EQ(count_out, 1);

    ASSERT(type->description);

    ASSERT(streq(type->description->ports_in[0]->name, "INPUT_PORT"));
    ASSERT(streq(type->description->ports_out[0]->name, "OUTPUT_PORT"));

    sol_flow_parser_del(parser);
}


DEFINE_TEST(declare_fbp);

static int
declare_fbp_read_file(void *data, const char *name, const char **buf, size_t *size)
{
    if (streq(name, "add.fbp")) {
        *buf = "INPORT=add.IN1:IN, OUTPORT=add.OUT:OUT, _(constant/int:value=1) OUT -> IN0 add(int/addition)";
        *size = strlen(*buf);
        return 0;
    }

    if (streq(name, "sub.fbp")) {
        *buf = "INPORT=sub.IN1:IN, OUTPORT=sub.OUT:OUT, _(constant/int:value=1) OUT -> IN0 sub(int/subtraction)";
        *size = strlen(*buf);
        return 0;
    }

    return -1;
}

struct sol_flow_parser_client declare_fbp_client = {
    .api_version = SOL_FLOW_PARSER_CLIENT_API_VERSION,
    .read_file = declare_fbp_read_file,
};

static void
declare_fbp(void)
{
    struct sol_flow_parser *parser;
    struct sol_flow_node_type *type;

    static const char input[] =
        "DECLARE=Add:fbp:add.fbp\n"
        "DECLARE=Sub:fbp:sub.fbp\n"
        "a(Add) OUT -> IN b(Sub)";

    parser = sol_flow_parser_new(&declare_fbp_client, NULL);
    ASSERT(parser);

    type = sol_flow_parse_string(parser, input, NULL);
    ASSERT(type);

    sol_flow_parser_del(parser);
}

TEST_MAIN_WITH_RESET_FUNC(clear_events);
