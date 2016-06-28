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

#include "sol-flow.h"
#include "sol-flow-parser.h"
#include "sol-flow-builder.h"
#include "sol-flow-resolver.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
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
test_connect_port_in(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    add_event(node, EVENT_PORT_CONNECT);
    return 0;
}

static int
test_connect_port_out(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_connect_port_out,
    .disconnect = test_port_disconnect,
};

static struct sol_flow_port_type_in test_port_in = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_connect_port_in,
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
test_node_init_type(void)
{
    if (!test_port_in.packet_type) {
        test_port_in.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
        test_port_out.packet_type = SOL_FLOW_PACKET_TYPE_EMPTY;
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

struct test_node_options {
    struct sol_flow_node_options base;
    bool opt;
};

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
    .options = &((const struct sol_flow_node_options_description){
            .data_size = sizeof(struct test_node_options),
            SOL_SET_API_VERSION(.sub_api = 1, )
            .required = false,
            .members = (const struct sol_flow_node_options_member_description[]){
                {
                    .name = "opt",
                    .description = "An optional option",
                    .data_type = "boolean",
                    .required = false,
                    .offset = offsetof(struct test_node_options, opt),
                    .size = sizeof(bool),
                    .defvalue.b = true,
                },
                {}
            }
        })
};

static const struct test_node_options default_opts = {
    .base = {
#ifndef SOL_NO_API_VERSION
        .api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION,
        .sub_api = 1,
#endif
    },
    .opt = true,
};

static const struct sol_flow_node_type test_node_type = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )

    .options_size = sizeof(struct test_node_options),
    .default_options = &default_opts,

    .init_type = test_node_init_type,

    .ports_in_count = sol_util_array_size(test_ports_in),
    .ports_out_count = sol_util_array_size(test_ports_out),
    .get_port_in = test_node_get_port_in,
    .get_port_out = test_node_get_port_out,

    .description = &test_node_description,
};

DEFINE_TEST(parse_with_string);

static void
parse_with_string(void)
{
    struct sol_flow_node *flow;
    struct sol_flow_parser *parser;
    unsigned int i;

    static const char *tests[] = {
        "node_alone(boolean/not)",
        "a(boolean/not) OUT -> IN b(boolean/not)",
        "a(boolean/not) OUT -> IN b(boolean/not) OUT -> IN c(boolean/not)",
    };

    parser = sol_flow_parser_new(NULL, NULL);
    ASSERT(parser);

    for (i = 0; i < sol_util_array_size(tests); i++) {
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
    unsigned int i;

    static const char *tests[] = {
        "a",
        "a(boolean/not) OUT in b(boolean/not)",
        "-> -> ->",

        "a(boolean/not) OUT -> IN b(node-type-that-doesnt-exist)",
        "a(boolean/not) PORT_THAT_DOESNT-exist -> IN b(boolean/not)",
    };

    parser = sol_flow_parser_new(NULL, NULL);
    ASSERT(parser);

    for (i = 0; i < sol_util_array_size(tests); i++) {
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
    struct sol_flow_node_named_options *named_opts)
{
    *type = &test_node_type;
    *named_opts = (struct sol_flow_node_named_options){};
    return 0;
}

static const struct sol_flow_resolver test_resolver = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_RESOLVER_API_VERSION, )
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

    static const char input[] =
        "OUTPORT=a.OUT1:OUTPUT_PORT\n"
        "INPORT=b.IN1:INPUT_PORT\n"
        "a(whatever) OUT1 -> IN1 b(whatever)";

    parser = sol_flow_parser_new(NULL, &test_resolver);
    ASSERT(parser);

    type = sol_flow_parse_string(parser, input, NULL);
    ASSERT(type);

    ASSERT_INT_EQ(type->ports_in_count, 1);
    ASSERT_INT_EQ(type->ports_out_count, 1);

    ASSERT(type->description);

    ASSERT(streq(type->description->ports_in[0]->name, "INPUT_PORT"));
    ASSERT(streq(type->description->ports_out[0]->name, "OUTPUT_PORT"));

    sol_flow_parser_del(parser);
}


DEFINE_TEST(declare_fbp);

static int
declare_fbp_read_file(void *data, const char *name, struct sol_buffer *buf)
{
    if (streq(name, "add.fbp")) {
        sol_buffer_init_flags(buf,
            (void *)"INPORT=add.OPERAND[1]:IN, OUTPORT=add.OUT:OUT, _(constant/int:value=1) OUT "
            "-> OPERAND[0] add(int/addition)",
            sizeof("INPORT=add.OPERAND[1]:IN, OUTPORT=add.OUT:OUT, _(constant/int:value=1) OUT "
                "-> OPERAND[0] add(int/addition)") - 1, SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        buf->used = buf->capacity;
        return 0;
    }

    if (streq(name, "sub.fbp")) {
        sol_buffer_init_flags(buf,
            (void *)"INPORT=sub.SUBTRAHEND:IN, OUTPORT=sub.OUT:OUT, _(constant/int:value=1) OUT -> "
            "MINUEND sub(int/subtraction)",
            sizeof("INPORT=sub.SUBTRAHEND:IN, OUTPORT=sub.OUT:OUT, _(constant/int:value=1) OUT -> "
            "MINUEND sub(int/subtraction)") - 1, SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        buf->used = buf->capacity;
        return 0;
    }

    return -1;
}

struct sol_flow_parser_client declare_fbp_client = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PARSER_CLIENT_API_VERSION, )
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

DEFINE_TEST(exported_options);

static void
exported_options(void)
{
    struct sol_flow_parser *parser;
    struct sol_flow_node_type *type;
    const struct sol_flow_node_options_member_description *myopt, *myotheropt, *opt;

    static const char input[] =
        "OPTION=a.opt:myopt\n"
        "OPTION=b.opt:myotheropt\n"
        "a(whatever) OUT1 -> IN1 b(whatever:opt=false)";

    parser = sol_flow_parser_new(NULL, &test_resolver);
    ASSERT(parser);

    type = sol_flow_parse_string(parser, input, NULL);
    ASSERT(type);

    ASSERT(type->description);
    ASSERT(type->description->options);
    ASSERT(type->description->options->members);

    myopt = &type->description->options->members[0];
    ASSERT(myopt);

    ASSERT(streq(myopt->name, "myopt"));

    opt = &test_node_description.options->members[0];

    ASSERT(streq(myopt->data_type, opt->data_type));
    ASSERT(myopt->required == opt->required);
    ASSERT(myopt->size == opt->size);
    ASSERT(myopt->defvalue.b == opt->defvalue.b);

    myotheropt = &type->description->options->members[1];
    ASSERT(myotheropt);

    ASSERT(streq(myotheropt->name, "myotheropt"));
    ASSERT(myotheropt->defvalue.b == false);

    sol_flow_parser_del(parser);
}

TEST_MAIN_WITH_RESET_FUNC(clear_events);
