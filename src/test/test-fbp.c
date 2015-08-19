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

#include "sol-fbp.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util.h"

#include "test.h"

static struct sol_fbp_node *
find_node(struct sol_fbp_graph *g, const char *name)
{
    struct sol_str_slice name_slice;
    struct sol_fbp_node *n;
    uint16_t i;

    name_slice = sol_str_slice_from_str(name);

    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        if (sol_str_slice_eq(n->name, name_slice))
            return n;
    }

    return NULL;
}

static struct sol_fbp_meta *
find_meta(struct sol_fbp_node *n, const char *key)
{
    struct sol_str_slice key_slice;
    struct sol_fbp_meta *m;
    uint16_t i;

    key_slice = sol_str_slice_from_str(key);

    SOL_VECTOR_FOREACH_IDX (&n->meta, m, i) {
        if (sol_str_slice_eq(m->key, key_slice))
            return m;
    }

    return NULL;
}

static struct sol_fbp_port *
find_in_port(struct sol_fbp_node *n, const char *name)
{
    struct sol_str_slice name_slice;
    struct sol_fbp_port *p;
    uint16_t i;

    name_slice = sol_str_slice_from_str(name);

    SOL_VECTOR_FOREACH_IDX (&n->in_ports, p, i) {
        if (sol_str_slice_eq(p->name, name_slice))
            return p;
    }

    return NULL;
}

static struct sol_fbp_port *
find_out_port(struct sol_fbp_node *n, const char *name)
{
    struct sol_str_slice name_slice;
    struct sol_fbp_port *p;
    uint16_t i;

    name_slice = sol_str_slice_from_str(name);

    SOL_VECTOR_FOREACH_IDX (&n->out_ports, p, i) {
        if (sol_str_slice_eq(p->name, name_slice))
            return p;
    }

    return NULL;
}

/* Argument order following the FBP format order (node port port node)
 * to make easy inspect tests. */
static struct sol_fbp_conn *
find_conn(struct sol_fbp_graph *g,
    const char *src, const char *src_port,
    const char *dst_port, const char *dst)
{
    struct sol_fbp_conn *conn;
    struct sol_fbp_node *n, *src_node = NULL, *dst_node = NULL;
    struct sol_fbp_port *p;
    struct sol_str_slice src_slice, src_port_slice, dst_slice, dst_port_slice;
    int src_idx = -1, src_port_idx = -1, dst_idx = -1, dst_port_idx = -1;
    uint16_t i;

    src_slice = sol_str_slice_from_str(src);
    src_port_slice = sol_str_slice_from_str(src_port);
    dst_slice = sol_str_slice_from_str(dst);
    dst_port_slice = sol_str_slice_from_str(dst_port);

    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        if (sol_str_slice_eq(n->name, src_slice)) {
            src_idx = i;
            src_node = n;
        } else if (sol_str_slice_eq(n->name, dst_slice)) {
            dst_idx = i;
            dst_node = n;
        }
    }

    if (src_idx < 0 || dst_idx < 0)
        return NULL;

    SOL_VECTOR_FOREACH_IDX (&src_node->out_ports, p, i) {
        if (sol_str_slice_eq(p->name, src_port_slice)) {
            src_port_idx = i;
            break;
        }
    }

    if (src_port_idx < 0)
        return NULL;

    SOL_VECTOR_FOREACH_IDX (&dst_node->in_ports, p, i) {
        if (sol_str_slice_eq(p->name, dst_port_slice)) {
            dst_port_idx = i;
            break;
        }
    }

    if (dst_port_idx < 0)
        return NULL;

    SOL_VECTOR_FOREACH_IDX (&g->conns, conn, i) {
        if (conn->src == src_idx && conn->dst == dst_idx
            && sol_str_slice_eq(conn->src_port, src_port_slice)
            && sol_str_slice_eq(conn->dst_port, dst_port_slice))
            return conn;
    }

    return NULL;
}

static struct sol_fbp_exported_port *
find_exported_in_port(struct sol_fbp_graph *g,
    const char *node, const char *port, const char *exported_name)
{
    struct sol_fbp_exported_port *ep;
    struct sol_fbp_node *n;
    struct sol_str_slice node_slice, port_slice, exported_name_slice;
    int node_idx = -1;
    uint16_t i;

    node_slice = sol_str_slice_from_str(node);
    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        if (sol_str_slice_eq(n->name, node_slice)) {
            node_idx = i;
            break;
        }
    }
    if (node_idx < 0)
        return NULL;

    port_slice = sol_str_slice_from_str(port);
    exported_name_slice = sol_str_slice_from_str(exported_name);

    SOL_VECTOR_FOREACH_IDX (&g->exported_in_ports, ep, i) {
        if (ep->node == node_idx && sol_str_slice_eq(ep->port, port_slice)
            && sol_str_slice_eq(ep->exported_name, exported_name_slice))
            return ep;
    }

    return NULL;
}

static struct sol_fbp_exported_port *
find_exported_out_port(struct sol_fbp_graph *g,
    const char *node, const char *port, const char *exported_name)
{
    struct sol_fbp_exported_port *ep;
    struct sol_fbp_node *n;
    struct sol_str_slice node_slice, port_slice, exported_name_slice;
    int node_idx = -1;
    uint16_t i;

    node_slice = sol_str_slice_from_str(node);
    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        if (sol_str_slice_eq(n->name, node_slice)) {
            node_idx = i;
            break;
        }
    }
    if (node_idx < 0)
        return NULL;

    port_slice = sol_str_slice_from_str(port);
    exported_name_slice = sol_str_slice_from_str(exported_name);

    SOL_VECTOR_FOREACH_IDX (&g->exported_out_ports, ep, i) {
        if (ep->node == node_idx && sol_str_slice_eq(ep->port, port_slice)
            && sol_str_slice_eq(ep->exported_name, exported_name_slice))
            return ep;
    }

    return NULL;
}

static bool
is_component_eq(struct sol_fbp_node *n, const char *name)
{
    return sol_str_slice_eq(n->component, sol_str_slice_from_str(name));
}

static bool
contains_meta(struct sol_fbp_node *n, const char *key, const char *value)
{
    struct sol_str_slice key_slice, value_slice;
    struct sol_fbp_meta *m;
    uint16_t i;

    key_slice = sol_str_slice_from_str(key);
    value_slice = sol_str_slice_from_str(value);

    SOL_VECTOR_FOREACH_IDX (&n->meta, m, i) {
        if (sol_str_slice_eq(m->key, key_slice)
            && sol_str_slice_eq(m->value, value_slice))
            return true;
    }

    return false;
}

/* Test cases. */

static const struct sol_str_slice input_simple = SOL_STR_SLICE_LITERAL(
    "a(T) OUT -> IN b(T)");

static void
check_simple(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *a, *b;

    ASSERT_INT_EQ(g->nodes.len, 2);

    a = find_node(g, "a");
    ASSERT(a);

    b = find_node(g, "b");
    ASSERT(b);

    ASSERT_INT_EQ(a->in_ports.len, 0);
    ASSERT_INT_EQ(a->out_ports.len, 1);
    ASSERT_INT_EQ(b->in_ports.len, 1);
    ASSERT_INT_EQ(b->out_ports.len, 0);

    ASSERT(find_out_port(a, "OUT"));
    ASSERT(find_in_port(b, "IN"));

    ASSERT_INT_EQ(g->conns.len, 1);

    ASSERT(find_conn(g, "a", "OUT", "IN", "b"));
}


static const struct sol_str_slice input_chained = SOL_STR_SLICE_LITERAL(
    "a(T) OUT -> IN b(T) OUT -> IN c(T)");

static void
check_chained(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *a, *b, *c;

    ASSERT_INT_EQ(g->nodes.len, 3);

    a = find_node(g, "a");
    ASSERT(a);

    b = find_node(g, "b");
    ASSERT(b);

    c = find_node(g, "c");
    ASSERT(c);

    ASSERT_INT_EQ(a->in_ports.len, 0);
    ASSERT_INT_EQ(a->out_ports.len, 1);
    ASSERT_INT_EQ(b->in_ports.len, 1);
    ASSERT_INT_EQ(b->out_ports.len, 1);
    ASSERT_INT_EQ(c->in_ports.len, 1);
    ASSERT_INT_EQ(c->out_ports.len, 0);

    ASSERT(find_out_port(a, "OUT"));
    ASSERT(find_in_port(b, "IN"));
    ASSERT(find_out_port(b, "OUT"));
    ASSERT(find_in_port(c, "IN"));

    ASSERT_INT_EQ(g->conns.len, 2);
    ASSERT(find_conn(g, "a", "OUT", "IN", "b"));
    ASSERT(find_conn(g, "b", "OUT", "IN", "c"));
}


static const struct sol_str_slice input_multi_stmts = SOL_STR_SLICE_LITERAL(
    "a(T) OUT1 -> IN1 b(T), a OUT2 -> IN2 b \n a OUT3 -> IN3 b");

static void
check_multi_stmts(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *a, *b;

    ASSERT_INT_EQ(g->nodes.len, 2);

    a = find_node(g, "a");
    ASSERT(a);

    b = find_node(g, "b");
    ASSERT(b);

    ASSERT_INT_EQ(a->in_ports.len, 0);
    ASSERT_INT_EQ(a->out_ports.len, 3);
    ASSERT_INT_EQ(b->in_ports.len, 3);
    ASSERT_INT_EQ(b->out_ports.len, 0);

    ASSERT(find_out_port(a, "OUT1"));
    ASSERT(find_out_port(a, "OUT2"));
    ASSERT(find_out_port(a, "OUT3"));
    ASSERT(find_in_port(b, "IN1"));
    ASSERT(find_in_port(b, "IN2"));
    ASSERT(find_in_port(b, "IN3"));

    ASSERT_INT_EQ(g->conns.len, 3);
    ASSERT(find_conn(g, "a", "OUT1", "IN1", "b"));
    ASSERT(find_conn(g, "a", "OUT2", "IN2", "b"));
    ASSERT(find_conn(g, "a", "OUT3", "IN3", "b"));
}


static const struct sol_str_slice input_inport_stmt = SOL_STR_SLICE_LITERAL(
    "INPORT=Read.IN:FILENAME\n Read(ReadFile) OUT -> IN Display(Output)");

static void
check_inport_stmt(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *read, *display;

    ASSERT_INT_EQ(g->nodes.len, 2);

    read = find_node(g, "Read");
    ASSERT(read);

    display = find_node(g, "Display");
    ASSERT(display);

    ASSERT_INT_EQ(g->exported_in_ports.len, 1);
    ASSERT(find_exported_in_port(g, "Read", "IN", "FILENAME"));
}


static const struct sol_str_slice input_outport_stmt = SOL_STR_SLICE_LITERAL(
    "Counter(T), OUTPORT=Counter.OUT:OUT");

static void
check_outport_stmt(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *counter;

    ASSERT_INT_EQ(g->nodes.len, 1);

    counter = find_node(g, "Counter");
    ASSERT(counter);

    ASSERT_INT_EQ(g->exported_out_ports.len, 1);
    ASSERT(find_exported_out_port(g, "Counter", "OUT", "OUT"));
}


static const struct sol_str_slice input_component = SOL_STR_SLICE_LITERAL(
    "INPORT=Read.IN:FILENAME, Read(ReadFile) OUT -> IN Display(Output)");

static void
check_component(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *read, *display;

    ASSERT_INT_EQ(g->nodes.len, 2);

    read = find_node(g, "Read");
    ASSERT(read);
    ASSERT(is_component_eq(read, "ReadFile"));

    display = find_node(g, "Display");
    ASSERT(display);
    ASSERT(is_component_eq(display, "Output"));
}


static const struct sol_str_slice input_meta = SOL_STR_SLICE_LITERAL(
    "MyTimer(Timer:interval=400) OUT -> IN Led(Super/LED:color=blue,brightness=100)");

static void
check_meta(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *my_timer, *led;

    ASSERT_INT_EQ(g->nodes.len, 2);

    my_timer = find_node(g, "MyTimer");
    ASSERT(my_timer);
    ASSERT(is_component_eq(my_timer, "Timer"));
    ASSERT(contains_meta(my_timer, "interval", "400"));

    led = find_node(g, "Led");
    ASSERT(led);
    ASSERT(is_component_eq(led, "Super/LED"));
    ASSERT(contains_meta(led, "color", "blue"));
    ASSERT(contains_meta(led, "brightness", "100"));
}


static const struct sol_str_slice input_meta_key_only = SOL_STR_SLICE_LITERAL(
    "Read(Reader:main) OUT -> IN Split(SplitStr:main), Split() OUT -> IN Count(Counter:main)");

static void
check_meta_key_only(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *read, *split, *count;

    ASSERT_INT_EQ(g->nodes.len, 3);

    read = find_node(g, "Read");
    ASSERT(read);
    ASSERT(contains_meta(read, "main", ""));

    split = find_node(g, "Split");
    ASSERT(split);
    ASSERT(contains_meta(split, "main", ""));

    count = find_node(g, "Count");
    ASSERT(count);
    ASSERT(contains_meta(count, "main", ""));
}

static const struct sol_str_slice input_suboptions = SOL_STR_SLICE_LITERAL(
    "MyTimer(Timer:interval=800|200|1000|2) OUT -> IN Led(Super/LED:color=r:125|g:0|b:255,brightness=100)");

static void
check_suboptions(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *my_timer, *led;

    ASSERT_INT_EQ(g->nodes.len, 2);

    my_timer = find_node(g, "MyTimer");
    ASSERT(my_timer);
    ASSERT(is_component_eq(my_timer, "Timer"));
    ASSERT(contains_meta(my_timer, "interval", "800|200|1000|2"));

    led = find_node(g, "Led");
    ASSERT(led);
    ASSERT(is_component_eq(led, "Super/LED"));
    ASSERT(contains_meta(led, "color", "r:125|g:0|b:255"));
    ASSERT(contains_meta(led, "brightness", "100"));
}

struct parse_test_entry {
    const struct sol_str_slice *input;
    void (*func)(struct sol_fbp_graph *);
};

static const struct sol_str_slice input_predeclare_nodes = SOL_STR_SLICE_LITERAL(
    "MyTimer(Timer:interval=800|200|1000|2)\n"
    "Led(Super/LED:color=r:125|g:0|b:255,brightness=100)\n"
    "MyTimer OUT -> IN Led");

static void
check_predeclare_nodes(struct sol_fbp_graph *g)
{
    check_suboptions(g);
}


static const struct sol_str_slice input_node_alone = SOL_STR_SLICE_LITERAL(
    "node_alone(Type)\n");

static void
check_node_alone(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *node_alone;

    ASSERT_INT_EQ(g->nodes.len, 1);

    node_alone = find_node(g, "node_alone");
    ASSERT(node_alone);
    ASSERT(is_component_eq(node_alone, "Type"));
}

#define ASSERT_POSITION(_pos, _line, _col)                              \
    do {                                                                \
        if ((_pos).line != (_line) || (_pos).column != (_col)) {        \
            fprintf(stderr, "%s:%d: %s: Wrong position for: `" #_pos    \
                "', expected (%u, %u) but got (%u, %u).\n",         \
                __FILE__, __LINE__, __PRETTY_FUNCTION__,            \
                _line, _col, (_pos).line, (_pos).column             \
                );                                                      \
            exit(1);                                                    \
        }                                                               \
    } while (0)

static const struct sol_str_slice input_column_position = SOL_STR_SLICE_LITERAL(
    "Timer(Timer:interval=400) OUT -> IN ConverterToBool(Converter/IntegerToBoolean:threshold=10) OUT -> IN Led(Super/LED:color=blue,brightness=100)");
/*       ^1       ^10       ^20       ^30       ^40       ^50       ^60       ^70       ^80       ^90       ^100      ^110      ^120      ^130      ^140 */

static void
check_column_position(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *timer, *converter, *led;
    struct sol_fbp_port *timer_out, *converter_in, *converter_out, *led_in;
    struct sol_fbp_conn *conn_timer_to_converter, *conn_converter_to_led;
    struct sol_fbp_meta *timer_interval, *converter_threshold, *led_color, *led_brightness;

    ASSERT_INT_EQ(g->nodes.len, 3);

    timer = find_node(g, "Timer");
    ASSERT(timer);
    ASSERT_POSITION(timer->position, 1, 1);

    timer_interval = find_meta(timer, "interval");
    ASSERT(timer_interval);
    ASSERT_POSITION(timer_interval->position, 1, 13);

    timer_out = find_out_port(timer, "OUT");
    ASSERT(timer_out);
    ASSERT_POSITION(timer_out->position, 1, 27);

    converter = find_node(g, "ConverterToBool");
    ASSERT(converter);
    ASSERT_POSITION(converter->position, 1, 37);

    converter_threshold = find_meta(converter, "threshold");
    ASSERT(converter_threshold);
    ASSERT_POSITION(converter_threshold->position, 1, 80);

    converter_in = find_in_port(converter, "IN");
    ASSERT(converter_in);
    ASSERT_POSITION(converter_in->position, 1, 34);

    converter_out = find_out_port(converter, "OUT");
    ASSERT(converter_out);
    ASSERT_POSITION(converter_out->position, 1, 94);

    conn_timer_to_converter = find_conn(g, "Timer", "OUT", "IN", "ConverterToBool");
    ASSERT(conn_timer_to_converter);
    ASSERT_POSITION(conn_timer_to_converter->position, 1, 27);

    led = find_node(g, "Led");
    ASSERT(led);
    ASSERT_POSITION(led->position, 1, 104);

    led_in = find_in_port(led, "IN");
    ASSERT(led_in);
    ASSERT_POSITION(led_in->position, 1, 101);

    led_color = find_meta(led, "color");
    ASSERT(led_color);
    ASSERT_POSITION(led_color->position, 1, 118);

    led_brightness = find_meta(led, "brightness");
    ASSERT(led_brightness);
    ASSERT_POSITION(led_brightness->position, 1, 129);

    conn_converter_to_led = find_conn(g, "ConverterToBool", "OUT", "IN", "Led");
    ASSERT(conn_converter_to_led);
    ASSERT_POSITION(conn_converter_to_led->position, 1, 94);
}

static const struct sol_str_slice input_line_position = SOL_STR_SLICE_LITERAL(
    "One(One)\n\nTwo(Two:val=123) OUT -> IN Three(Three)\n\n#commentary\n\n\nFour(Four)\n\n\n\n\nFive(Five)\nSix(Six)");
/*       ^1          ^3                                         ^5               ^8                  ^13         ^14      */

static void
check_line_position(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *one, *two, *three, *four, *five, *six;
    struct sol_fbp_port *two_out, *three_in;
    struct sol_fbp_conn *conn_two_to_three;
    struct sol_fbp_meta *two_val;

    ASSERT_INT_EQ(g->nodes.len, 6);

    one = find_node(g, "One");
    ASSERT(one);
    ASSERT_POSITION(one->position, 1, 1);

    two = find_node(g, "Two");
    ASSERT(two);
    ASSERT_POSITION(two->position, 3, 1);

    two_val = find_meta(two, "val");
    ASSERT(two_val);
    ASSERT_POSITION(two_val->position, 3, 9);

    two_out = find_out_port(two, "OUT");
    ASSERT(two_out);
    ASSERT_POSITION(two_out->position, 3, 18);

    three = find_node(g, "Three");
    ASSERT(three);
    ASSERT_POSITION(three->position, 3, 28);

    three_in = find_in_port(three, "IN");
    ASSERT(three_in);
    ASSERT_POSITION(three_in->position, 3, 25);

    conn_two_to_three = find_conn(g, "Two", "OUT", "IN", "Three");
    ASSERT(conn_two_to_three);
    ASSERT_POSITION(conn_two_to_three->position, 3, 18);

    four = find_node(g, "Four");
    ASSERT(four);
    ASSERT_POSITION(four->position, 8, 1);

    five = find_node(g, "Five");
    ASSERT(five);
    ASSERT_POSITION(five->position, 13, 1);

    six = find_node(g, "Six");
    ASSERT(six);
    ASSERT_POSITION(six->position, 14, 1);
}

static const struct sol_str_slice input_anonymous_nodes = SOL_STR_SLICE_LITERAL(
    "_(constant/boolean:value=true) OUT -> IN _(converter/boolean-to-string) OUT -> IN _(console)");
/*   ^1                                       ^42                                      ^83 */

static void
check_anonymous_nodes(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *one /* little indian */, *two /* little indians */, *three /* little indians */;

    ASSERT_INT_EQ(g->nodes.len, 3);

    one = find_node(g, "#anon:1:1");
    ASSERT(one);
    ASSERT_POSITION(one->position, 1, 1);

    two = find_node(g, "#anon:1:42");
    ASSERT(two);
    ASSERT_POSITION(two->position, 1, 42);

    three = find_node(g, "#anon:1:83");
    ASSERT(three);
    ASSERT_POSITION(three->position, 1, 83);
}


static const struct sol_str_slice input_declare_stmt = SOL_STR_SLICE_LITERAL(
    "DECLARE=MyType:fbp:MyType.fbp, node(MyType)");

static void
check_declare_stmt(struct sol_fbp_graph *g)
{
    struct sol_fbp_node *node;
    struct sol_fbp_declaration *dec;

    ASSERT_INT_EQ(g->nodes.len, 1);

    node = find_node(g, "node");
    ASSERT(node);
    ASSERT(is_component_eq(node, "MyType"));

    ASSERT_INT_EQ(g->declarations.len, 1);

    dec = sol_vector_get(&g->declarations, 0);
    ASSERT(dec);

    ASSERT(sol_str_slice_str_eq(dec->name, "MyType"));
    ASSERT(sol_str_slice_str_eq(dec->metatype, "fbp"));
    ASSERT(sol_str_slice_str_eq(dec->contents, "MyType.fbp"));
}


#define PARSE_TEST(NAME) { &input_ ## NAME, check_ ## NAME }

static struct parse_test_entry parse_tests[] = {
    PARSE_TEST(simple),
    PARSE_TEST(chained),
    PARSE_TEST(multi_stmts),
    PARSE_TEST(inport_stmt),
    PARSE_TEST(outport_stmt),
    PARSE_TEST(component),
    PARSE_TEST(meta),
    PARSE_TEST(meta_key_only),
    PARSE_TEST(suboptions),
    PARSE_TEST(predeclare_nodes),
    PARSE_TEST(node_alone),
    PARSE_TEST(column_position),
    PARSE_TEST(line_position),
    PARSE_TEST(anonymous_nodes),
    PARSE_TEST(declare_stmt),
};

DEFINE_TEST(run_parse_tests);

static void
run_parse_tests(void)
{
    struct sol_fbp_error *fbp_error;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(parse_tests); i++) {
        struct sol_fbp_graph g;
        int r;

        r = sol_fbp_graph_init(&g);
        ASSERT_INT_EQ(r, 0);

        fbp_error = sol_fbp_parse(*parse_tests[i].input, &g);
        if (fbp_error) {
            sol_fbp_log_print(NULL, fbp_error->position.line, fbp_error->position.column, fbp_error->msg);
            sol_fbp_error_free(fbp_error);
            SOL_ERR("Failed to parse string '%.*s'.", SOL_STR_SLICE_PRINT(*parse_tests[i].input));
            ASSERT(false);
        }

        parse_tests[i].func(&g);

        r = sol_fbp_graph_fini(&g);
        ASSERT_INT_EQ(r, 0);
    }
}


TEST_MAIN();
