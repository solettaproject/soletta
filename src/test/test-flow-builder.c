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

#include <errno.h>

#include "sol-flow.h"
#include "sol-flow-builder.h"
#include "sol-flow-resolver.h"
#include "sol-flow-static.h"
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
    int count = 0;
    uint16_t i;

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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION, )
    .packet_type = NULL, /* placeholder for SOL_FLOW_PACKET_TYPE_EMTPY */
    .connect = test_port_out_connect,
    .disconnect = test_port_disconnect,
};

static struct sol_flow_port_type_in test_port_in = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )
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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )

    .init_type = test_node_init_type,

    .ports_in_count = ARRAY_SIZE(test_ports_in),
    .ports_out_count = ARRAY_SIZE(test_ports_out),
    .get_port_in = test_node_get_port_in,
    .get_port_out = test_node_get_port_out,
    .options_size = sizeof(struct sol_flow_node_options),

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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )
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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )
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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )
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
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )
    .description = &test_wrong_in2_node_description,
};

DEFINE_TEST(connect_two_nodes);

static void
connect_two_nodes(void)
{
    struct sol_flow_node *flow, *node_out, *node_in;
    struct sol_flow_node_type *node_type;
    struct sol_flow_builder *builder;

    builder = sol_flow_builder_new();

    sol_flow_builder_add_node(builder, "node1", &test_node_type, NULL);
    sol_flow_builder_add_node(builder, "node2", &test_node_type, NULL);

    sol_flow_builder_connect(builder, "node1", "OUT1", -1, "node2", "IN2", -1);
    sol_flow_builder_connect(builder, "node1", "OUT2", -1, "node2", "IN1", -1);
    sol_flow_builder_connect(builder, "node2", "OUT1", -1, "node1", "IN1", -1);
    sol_flow_builder_connect(builder, "node2", "OUT2", -1, "node1", "IN1", -1);

    node_type = sol_flow_builder_get_node_type(builder);
    sol_flow_builder_del(builder);

    flow = sol_flow_node_new(NULL, "simple_and", node_type, NULL);
    ASSERT(flow);

    node_out = sol_flow_static_get_node(flow, 0);
    node_in = sol_flow_static_get_node(flow, 1);

    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_CONNECT, 4);
    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_DISCONNECT, 0);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_CONNECT, 4);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_DISCONNECT, 0);

    sol_flow_node_del(flow);
    sol_flow_node_type_del(node_type);

    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_CONNECT, 4);
    ASSERT_EVENT_COUNT(node_out, EVENT_PORT_DISCONNECT, 4);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_CONNECT, 4);
    ASSERT_EVENT_COUNT(node_in, EVENT_PORT_DISCONNECT, 4);
}

DEFINE_TEST(connections_nodes_are_ordered);

static void
connections_nodes_are_ordered(void)
{
    struct sol_flow_node_type *node_type;
    struct sol_flow_builder *builder;
    struct sol_flow_node *flow;

    builder = sol_flow_builder_new();

    sol_flow_builder_add_node(builder, "node1", &test_node_type, NULL);
    sol_flow_builder_add_node(builder, "node2", &test_node_type, NULL);

    /* nodes out of order */
    sol_flow_builder_connect(builder, "node2", "OUT1", -1, "node1", "IN1", -1);
    sol_flow_builder_connect(builder, "node1", "OUT1", -1, "node2", "IN1", -1);

    node_type = sol_flow_builder_get_node_type(builder);
    sol_flow_builder_del(builder);

    /* if connections are out of order flow won't be created */
    flow = sol_flow_node_new(NULL, "simple_and", node_type, NULL);

    ASSERT(flow);

    sol_flow_node_del(flow);
    sol_flow_node_type_del(node_type);
}

DEFINE_TEST(connections_ports_are_ordered);

static void
connections_ports_are_ordered(void)
{
    struct sol_flow_node_type *node_type;
    struct sol_flow_builder *builder;
    struct sol_flow_node *flow;

    builder = sol_flow_builder_new();

    sol_flow_builder_add_node(builder, "node1", &test_node_type, NULL);
    sol_flow_builder_add_node(builder, "node2", &test_node_type, NULL);

    /* ports out of order */
    sol_flow_builder_connect(builder, "node1", "OUT2", -1, "node2", "IN1", -1);
    sol_flow_builder_connect(builder, "node1", "OUT1", -1, "node2", "IN2", -1);

    node_type = sol_flow_builder_get_node_type(builder);
    sol_flow_builder_del(builder);

    /* if connections are out of order flow won't be created */
    flow = sol_flow_node_new(NULL, "simple_and", node_type, NULL);
    ASSERT(flow);

    sol_flow_node_del(flow);
    sol_flow_node_type_del(node_type);
}

DEFINE_TEST(nodes_must_have_unique_names);

static void
nodes_must_have_unique_names(void)
{
    struct sol_flow_builder *builder;
    int ret;

    builder = sol_flow_builder_new();

    sol_flow_builder_add_node(builder, "node1", &test_node_type, NULL);
    ret = sol_flow_builder_add_node(builder, "node1",
        &test_wrong_out_node_type, NULL);
    ASSERT(ret);

    ret = sol_flow_builder_add_node(builder, NULL, &test_node_type, NULL);
    ASSERT(ret);

    sol_flow_builder_del(builder);
}

DEFINE_TEST(node_ports_must_have_unique_names);

static void
node_ports_must_have_unique_names(void)
{
    struct sol_flow_builder *builder;
    int ret;

    builder = sol_flow_builder_new();

    ret = sol_flow_builder_add_node(builder, "node",
        &test_wrong_out_node_type, NULL);
    ASSERT(ret);

    ret = sol_flow_builder_add_node(builder, "node",
        &test_wrong_out2_node_type, NULL);
    ASSERT(ret);

    ret = sol_flow_builder_add_node(builder, "node",
        &test_wrong_in_node_type, NULL);
    ASSERT(ret);

    ret = sol_flow_builder_add_node(builder, "node",
        &test_wrong_in2_node_type, NULL);
    ASSERT(ret);

    sol_flow_builder_del(builder);
}

DEFINE_TEST(ports_can_be_exported);

static void
ports_can_be_exported(void)
{
    struct sol_flow_builder *builder;
    struct sol_flow_node_type *type;
    int ret;

    const char *in_name = "EXPORTED_IN";
    const char *out_name = "EXPORTED_OUT";

    builder = sol_flow_builder_new();

    sol_flow_builder_add_node(builder, "node", &test_node_type, NULL);
    sol_flow_builder_add_node(builder, "other", &test_node_type, NULL);
    sol_flow_builder_connect(builder, "node", "OUT2", -1, "other", "IN2", -1);

    ret = sol_flow_builder_export_in_port(builder, "node", "IN1", -1, in_name);
    ASSERT(ret >= 0);

    ret = sol_flow_builder_export_out_port(builder, "other", "OUT2", -1, out_name);
    ASSERT(ret >= 0);

    type = sol_flow_builder_get_node_type(builder);
    ASSERT(type);

    ASSERT_INT_EQ(type->ports_in_count, 1);
    ASSERT_INT_EQ(type->ports_out_count, 1);

    ASSERT(type->description);

    ASSERT(streq(type->description->ports_in[0]->name, in_name));
    ASSERT(streq(type->description->ports_out[0]->name, out_name));

    sol_flow_node_type_del(type);
    sol_flow_builder_del(builder);
}


static int
custom_resolve(void *data, const char *id, struct sol_flow_node_type const **type,
    struct sol_flow_node_named_options *named_opts)
{
    if (streq(id, "custom_test_type")) {
        *type = &test_node_type;
        *named_opts = (struct sol_flow_node_named_options){};
        return 0;
    }

    return sol_flow_resolve(NULL, id, type, named_opts);
}

static const struct sol_flow_resolver custom_resolver = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_RESOLVER_API_VERSION, )
    .name = "custom_resolver",
    .resolve = custom_resolve,
};

DEFINE_TEST(add_node_by_type);

static void
add_node_by_type(void)
{
    static const char *const bad_opts[] = {
        "value=5", NULL
    };
    static const char *const good_opts[] = {
        "value=true", NULL
    };
    static const char *const string_opts[] = {
        "value=\"something\"", NULL
    };

    static const struct {
        const char *type;
        bool can_build;
        const char *const *opts;
    } inputs[] = {
        { "boolean/and", true, NULL },
        { "custom_test_type", true, NULL },
        { "custom_type_that_doesnt_exist", false, NULL },

        /* timer have options but with no required members. */
        { "timer", true, NULL },

        /* constant/boolean have options with required members, so would
         * fail because we don't specify it. */
        { "constant/boolean", false, NULL },

        { "constant/boolean", false, bad_opts },
        { "constant/boolean", true, good_opts },

        { "constant/string", true, string_opts },
    };

    struct sol_flow_builder *builder;
    unsigned int i;

    builder = sol_flow_builder_new();
    sol_flow_builder_set_resolver(builder, &custom_resolver);

    for (i = 0; i < ARRAY_SIZE(inputs); i++) {
        int err;
        char *name;
        bool built;

        asprintf(&name, "node%d", i);
        ASSERT(name);

        err = sol_flow_builder_add_node_by_type(builder, name, inputs[i].type, inputs[i].opts);
        built = err >= 0;

        if (built != inputs[i].can_build) {
            SOL_ERR("Unexpected result when building input=%d of type='%s', expected %s but got %s",
                i, inputs[i].type, inputs[i].can_build ? "build" : "failure", built ? "build" : "failure");
            FAIL();
        }

        free(name);
    }

    sol_flow_builder_del(builder);
}

DEFINE_TEST(add_type_descriptions);

static void
add_type_descriptions(void)
{
    struct sol_flow_builder *builder;
    struct sol_flow_node_type *type;
    int ret;

    builder = sol_flow_builder_new();
    ASSERT(builder);

    sol_flow_builder_add_node_by_type(builder, "node", "boolean/and", NULL);

    ret = sol_flow_builder_set_type_description(builder, "MyName", "MyCategory", "MyDescription", "MyAuthor", "MyUrl", "MyLicense", "MyVersion");
    ASSERT(ret == 0);

    type = sol_flow_builder_get_node_type(builder);
    ASSERT(type);
    sol_flow_builder_del(builder);

    ASSERT(streq(type->description->name, "MyName"));
    ASSERT(streq(type->description->category, "MyCategory"));
    ASSERT(streq(type->description->description, "MyDescription"));
    ASSERT(streq(type->description->author, "MyAuthor"));
    ASSERT(streq(type->description->url, "MyUrl"));
    ASSERT(streq(type->description->license, "MyLicense"));
    ASSERT(streq(type->description->version, "MyVersion"));
    ASSERT(streq(type->description->symbol, "SOL_FLOW_NODE_TYPE_BUILDER_MYNAME"));
    ASSERT(streq(type->description->options_symbol, "sol_flow_node_type_builder_myname_options"));

    sol_flow_node_type_del(type);
}

TEST_MAIN_WITH_RESET_FUNC(clear_events);
