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
#include <errno.h>
#include <stdio.h>

#include "sol-fbp-internal-log.h"
#include "sol-fbp-internal-scanner.h"
#include "sol-fbp.h"
#include "sol-util.h"

struct sol_fbp_parser {
    struct sol_fbp_scanner scanner;

    /* Stores both the current and the pending token, so we can peek
    * at the next token without having to calculate it everytime. */
    struct sol_fbp_token current_token;
    struct sol_fbp_token pending_token;

    char *error_msg;
    struct sol_fbp_position error_pos;

    struct sol_fbp_graph *graph;
};

static struct sol_fbp_position
get_token_position(struct sol_fbp_token *t)
{
    return (struct sol_fbp_position) {
               .line = t->line,
               .column = t->column
    };
}

static enum sol_fbp_token_type
next_token(struct sol_fbp_parser *p)
{
    enum sol_fbp_token_type type = p->pending_token.type;

    if (type != SOL_FBP_TOKEN_NONE) {
        p->current_token = p->pending_token;
        p->pending_token.type = SOL_FBP_TOKEN_NONE;
        return type;
    }

    sol_fbp_scan_token(&p->scanner);
    p->current_token = p->scanner.token;

    return p->current_token.type;
}

static enum sol_fbp_token_type
peek_token(struct sol_fbp_parser *p)
{
    enum sol_fbp_token_type type = p->pending_token.type;
    struct sol_fbp_token old;

    if (type != SOL_FBP_TOKEN_NONE)
        return type;

    old = p->current_token;
    type = next_token(p);
    p->pending_token = p->current_token;
    p->current_token = old;

    return type;
}

static struct sol_str_slice
get_token_slice(struct sol_fbp_parser *p)
{
    struct sol_str_slice s;

    s.len = p->current_token.end - p->current_token.start;
    s.data = p->current_token.start;
    return s;
}

static bool
set_parse_error(struct sol_fbp_parser *p, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (vasprintf(&p->error_msg, fmt, ap) == -1)
        SOL_WRN("Unable to parse error message %s", sol_util_strerrora(errno));
    va_end(ap);

    if (p->error_pos.line == 0) {
        p->error_pos.line = p->current_token.line;
        p->error_pos.column = p->current_token.column;
    }
    return false;
}

static bool
parse_exported_port(struct sol_fbp_parser *p,
    struct sol_str_slice *node, struct sol_str_slice *port, int *port_idx,
    struct sol_str_slice *exported_port)
{
    if (next_token(p) != SOL_FBP_TOKEN_EQUAL)
        return set_parse_error(p, "Expected '=' after exported port keyword");

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected node identifier in export port statement");

    *node = get_token_slice(p);

    if (next_token(p) != SOL_FBP_TOKEN_DOT)
        return set_parse_error(p, "Expected '.' after node identifier in export port statement");

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected port identifier in export port statement");

    *port = get_token_slice(p);

    if (peek_token(p) == SOL_FBP_TOKEN_BRACKET_OPEN) {
        struct sol_str_slice idx;

        /* consume '[' */
        next_token(p);

        if (next_token(p) != SOL_FBP_TOKEN_INTEGER)
            return set_parse_error(p, "Expected integer number for port index");

        idx = get_token_slice(p);

        if (next_token(p) != SOL_FBP_TOKEN_BRACKET_CLOSE) {
            return set_parse_error(p, "Expected ']' after port index");
        }

        sol_str_slice_to_int(idx, port_idx);
    }

    if (next_token(p) != SOL_FBP_TOKEN_COLON)
        return set_parse_error(p, "Expected ':' after port identifier in export port statement");

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected exported port identifier");

    *exported_port = get_token_slice(p);

    return true;
}

static bool
handle_node_error(struct sol_fbp_parser *p, struct sol_str_slice *name, struct sol_fbp_position *position, int error, struct sol_fbp_node *err_node)
{
    p->error_pos = *position;

    if (error == -EINVAL)
        return set_parse_error(p, "Anonymous node type must be defined. e.g. '_(nodetype)'");

    if (error == -EEXIST) {
        return set_parse_error(p, "Node '%.*s' already declared with type '%.*s' at %d:%d",
            SOL_STR_SLICE_PRINT(*name), SOL_STR_SLICE_PRINT(err_node->component),
            err_node->position.line, err_node->position.column);
    }

    return set_parse_error(p, "Couldn't create node '%.*s': %s", (int)name->len, name->data, sol_util_strerrora(error));
}

static bool
handle_conn_error(struct sol_fbp_parser *p,
    int src, struct sol_str_slice *src_port_name, int src_port_idx,
    int dst, struct sol_str_slice *dst_port_name, int dst_port_idx,
    struct sol_fbp_position *position, int error)
{
    struct sol_fbp_conn *c;
    uint16_t i;

    p->error_pos = *position;

    if (error == -EEXIST) {
        SOL_VECTOR_FOREACH_IDX (&p->graph->conns, c, i) {
            if (c->src != src || c->dst != dst)
                continue;

            if (c->src_port_idx != src_port_idx || c->dst_port_idx != dst_port_idx)
                continue;

            if (!sol_str_slice_eq(c->src_port, *src_port_name) || !sol_str_slice_eq(c->dst_port, *dst_port_name))
                continue;

            return set_parse_error(p, "Connection '%.*s[%d] -> %.*s[%d]' already declared at %d:%d",
                (int)src_port_name->len, src_port_name->data, src_port_idx, (int)dst_port_name->len, dst_port_name->data, dst_port_idx,
                c->position.line, c->position.column);
        }
    }

    return set_parse_error(p, "Couldn't add connection '%.*s -> %.*s': %s",
        (int)src_port_name->len, src_port_name->data, (int)dst_port_name->len, dst_port_name->data, sol_util_strerrora(error));
}

static bool
handle_meta_error(struct sol_fbp_parser *p, int node, struct sol_str_slice *key, struct sol_fbp_position *position, int error)
{
    struct sol_fbp_node *n;
    struct sol_fbp_meta *m;
    uint16_t i;

    p->error_pos = *position;

    n = sol_vector_get(&p->graph->nodes, node);
    SOL_NULL_CHECK(n, -EINVAL);

    if (error == -EEXIST) {
        SOL_VECTOR_FOREACH_IDX (&n->meta, m, i) {
            if (sol_str_slice_eq(m->key, *key)) {
                return set_parse_error(p, "Node '%.*s' option '%.*s' already declared at %d:%d",
                    (int)n->name.len, n->name.data, (int)key->len, key->data, m->position.line, m->position.column);
            }
        }
    }

    return set_parse_error(p, "Couldn't add option '%.s*': %s", (int)key->len, key->data, sol_util_strerrora(error));
}

static bool
parse_inport_stmt(struct sol_fbp_parser *p)
{
    struct sol_str_slice node, port, exported_port, empty = SOL_STR_SLICE_EMPTY;
    struct sol_fbp_position node_position;
    struct sol_fbp_node *err_node;
    struct sol_fbp_exported_port *ep;
    int node_idx, port_idx = -1;
    int err;

    assert(next_token(p) == SOL_FBP_TOKEN_INPORT_KEYWORD);
    if (!parse_exported_port(p, &node, &port, &port_idx, &exported_port))
        return false;

    node_position = get_token_position(&p->current_token);

    node_idx = sol_fbp_graph_add_node(p->graph, node, empty, node_position, &err_node);
    if (node_idx < 0)
        return handle_node_error(p, &node, &node_position, node_idx, err_node);

    sol_fbp_graph_add_in_port(p->graph, node_idx, port, get_token_position(&p->current_token));

    err = sol_fbp_graph_add_exported_in_port(p->graph, node_idx, port, port_idx, exported_port, get_token_position(&p->current_token), &ep);
    if (err == -EEXIST) {
        return set_parse_error(p,
            "Exported input port with name '%.*s' already declared in %d:%d",
            SOL_STR_SLICE_PRINT(exported_port), ep->position.line, ep->position.column);
    }
    if (err == -EADDRINUSE) {
        return set_parse_error(p,
            "Node '%.*s' and input port '%.*s' already exported as '%.*s' declared in %d:%d",
            SOL_STR_SLICE_PRINT(node), SOL_STR_SLICE_PRINT(port),
            SOL_STR_SLICE_PRINT(ep->exported_name),
            ep->position.line, ep->position.column);
    }

    return true;
}

static bool
parse_outport_stmt(struct sol_fbp_parser *p)
{
    struct sol_str_slice node, port, exported_port, empty = SOL_STR_SLICE_EMPTY;
    struct sol_fbp_position node_position;
    struct sol_fbp_node *err_node;
    struct sol_fbp_exported_port *ep;
    int node_idx, port_idx = -1;
    int err;

    assert(next_token(p) == SOL_FBP_TOKEN_OUTPORT_KEYWORD);
    if (!parse_exported_port(p, &node, &port, &port_idx, &exported_port))
        return false;

    node_position = get_token_position(&p->current_token);

    node_idx = sol_fbp_graph_add_node(p->graph, node, empty, node_position, &err_node);
    if (node_idx < 0)
        return handle_node_error(p, &node, &node_position, node_idx, err_node);

    sol_fbp_graph_add_out_port(p->graph, node_idx, port, get_token_position(&p->current_token));

    err = sol_fbp_graph_add_exported_out_port(p->graph, node_idx, port, port_idx, exported_port, get_token_position(&p->current_token), &ep);
    if (err == -EEXIST) {
        return set_parse_error(p,
            "Exported output port with name '%.*s' already declared in %d:%d",
            SOL_STR_SLICE_PRINT(exported_port), ep->position.line, ep->position.column);
    }
    if (err == -EADDRINUSE) {
        return set_parse_error(p,
            "Node '%.*s' and output port '%.*s' already exported as '%.*s' declared in %d:%d",
            SOL_STR_SLICE_PRINT(node), SOL_STR_SLICE_PRINT(port),
            SOL_STR_SLICE_PRINT(ep->exported_name),
            ep->position.line, ep->position.column);
    }

    return true;
}

static bool
parse_declare_stmt(struct sol_fbp_parser *p)
{
    struct sol_str_slice name, kind, contents;
    struct sol_fbp_position pos;
    int err;

    assert(next_token(p) == SOL_FBP_TOKEN_DECLARE_KEYWORD);

    if (next_token(p) != SOL_FBP_TOKEN_EQUAL)
        return set_parse_error(p, "Expected '=' after DECLARE keyword");

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected name in declaration statement");

    name = get_token_slice(p);
    pos = get_token_position(&p->current_token);

    if (next_token(p) != SOL_FBP_TOKEN_COLON)
        return set_parse_error(p, "Expected ':' after name in declaration statement");

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected kind name in declaration statement");

    kind = get_token_slice(p);

    if (next_token(p) != SOL_FBP_TOKEN_COLON)
        return set_parse_error(p, "Expected ':' after kind name in declaration statement");

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected declaration contents");

    contents = get_token_slice(p);

    err = sol_fbp_graph_declare(p->graph, name, kind, contents, pos);
    if (err == -EEXIST) {
        p->error_pos = pos;
        return set_parse_error(p, "Type '%.*s' already declared", SOL_STR_SLICE_PRINT(name));
    } else if (err == -EINVAL) {
        p->error_pos = pos;
        return set_parse_error(p, "Type '%.*s' with invalid values", SOL_STR_SLICE_PRINT(name));
    }

    return true;
}

static bool
parse_meta(struct sol_fbp_parser *p, int node)
{
    struct sol_str_slice key, value;
    struct sol_fbp_position key_position;
    bool first = true;
    int r;

    if (peek_token(p) != SOL_FBP_TOKEN_COLON)
        return true;

    /* Consume ':'. */
    next_token(p);

    while (peek_token(p) != SOL_FBP_TOKEN_PAREN_CLOSE) {
        if (!first && next_token(p) != SOL_FBP_TOKEN_COMMA)
            return set_parse_error(p, "Expected ',' after key-pair meta information. e.g. '(nodetype:key1:val2,keyN:valN)'");

        if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
            return set_parse_error(p, "Expected key for node meta information. e.g. '(nodetype:key1:val2,keyN:valN)'");

        key = get_token_slice(p);
        key_position = get_token_position(&p->current_token);

        if (peek_token(p) == SOL_FBP_TOKEN_EQUAL) {
            char curr = next_token(p); /* Consume '='. */
            curr = next_token(p);

            if (curr != SOL_FBP_TOKEN_IDENTIFIER && curr != SOL_FBP_TOKEN_STRING)
                return set_parse_error(p, "Expected value for node meta information. e.g. '(nodetype:key1:val2,keyN:valN)'");

            value = get_token_slice(p);
        } else {
            value = (struct sol_str_slice)SOL_STR_SLICE_EMPTY;
        }

        r = sol_fbp_graph_add_meta(p->graph, node, key, value, key_position);
        if (r < 0)
            return handle_meta_error(p, node, &key, &key_position, r);

        first = false;
    }

    return true;
}

static bool
parse_node(struct sol_fbp_parser *p, int *node)
{
    struct sol_str_slice name = SOL_STR_SLICE_EMPTY, component = SOL_STR_SLICE_EMPTY;
    struct sol_fbp_node *err_node;
    struct sol_fbp_position node_position;
    int n;

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected node identifier. e.g. 'node(nodetype)'");

    name = get_token_slice(p);
    node_position = get_token_position(&p->current_token);
    if (peek_token(p) != SOL_FBP_TOKEN_PAREN_OPEN) {
        n = sol_fbp_graph_add_node(p->graph, name, component, node_position, &err_node);
        if (n < 0)
            return handle_node_error(p, &name, &node_position, n, err_node);
        goto end;
    }

    /* Consume '('. */
    next_token(p);

    if (peek_token(p) == SOL_FBP_TOKEN_PAREN_CLOSE) {
        next_token(p); /* Consume ')'. */
        n = sol_fbp_graph_add_node(p->graph, name, component, node_position, &err_node);
        if (n < 0)
            return handle_node_error(p, &name, &node_position, n, err_node);
        goto end;
    }

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER)
        return set_parse_error(p, "Expected node type after '('. e.g. 'node(nodetype)'");

    component = get_token_slice(p);
    n = sol_fbp_graph_add_node(p->graph, name, component, node_position, &err_node);
    if (n < 0)
        return handle_node_error(p, &name, &node_position, n, err_node);

    if (!parse_meta(p, n))
        return false;

    if (next_token(p) != SOL_FBP_TOKEN_PAREN_CLOSE)
        return set_parse_error(p, "Expected ')' after node type. e.g. 'node(nodetype)'");

end:
    *node = n;
    return true;
}

static bool
parse_port(struct sol_fbp_parser *p, struct sol_str_slice *name, int *port_idx)
{
    struct sol_str_slice idx;

    if (next_token(p) != SOL_FBP_TOKEN_IDENTIFIER) {
        return set_parse_error(p, "Expected port identifier."
            " e.g. 'node(nodetype) OUTPUT_PORT_NAME -> INPUT_PORT_NAME node2(nodetype2)'");
    }

    *name = get_token_slice(p);

    if (peek_token(p) != SOL_FBP_TOKEN_BRACKET_OPEN)
        return true;

    next_token(p);

    if (next_token(p) != SOL_FBP_TOKEN_INTEGER) {
        return set_parse_error(p, "Expected integer number for port index");
    }

    idx = get_token_slice(p);

    if (next_token(p) != SOL_FBP_TOKEN_BRACKET_CLOSE) {
        return set_parse_error(p, "Expected ']' after port index");
    }

    sol_str_slice_to_int(idx, port_idx);

    return true;
}

static bool
parse_conn_stmt(struct sol_fbp_parser *p)
{
    enum sol_fbp_token_type t;
    int src, dst;
    struct sol_str_slice src_port_name, dst_port_name;
    struct sol_fbp_position conn_position, in_port_position;
    int r;

    if (!parse_node(p, &src))
        return false;

    t = peek_token(p);
    if (t != SOL_FBP_TOKEN_IDENTIFIER && t != SOL_FBP_TOKEN_STMT_SEPARATOR && t != SOL_FBP_TOKEN_EOF) {
        return set_parse_error(p, "Expected node and port while defining a connection."
            " e.g. 'node(nodetype) OUTPUT_PORT_NAME -> INPUT_PORT_NAME node2(nodetype2)'");
    }

    while (peek_token(p) == SOL_FBP_TOKEN_IDENTIFIER) {
        int src_port_idx = -1, dst_port_idx = -1;
        if (!parse_port(p, &src_port_name, &src_port_idx))
            return false;

        conn_position = get_token_position(&p->current_token);

        sol_fbp_graph_add_out_port(p->graph, src, src_port_name, get_token_position(&p->current_token));

        t = next_token(p);
        if (t != SOL_FBP_TOKEN_ARROW) {
            if (t == SOL_FBP_TOKEN_PAREN_OPEN) {
                return set_parse_error(p, "Expected node and port while defining a connection."
                    " e.g. 'node(nodetype) OUTPUT_PORT_NAME -> INPUT_PORT_NAME node2(nodetype2)'");
            }
            return set_parse_error(p, "Expected '->' between connection statement."
                " e.g. 'node(nodetype) OUTPUT_PORT_NAME -> INPUT_PORT_NAME node2(nodetype2)'");
        }

        if (peek_token(p) != SOL_FBP_TOKEN_IDENTIFIER) {
            return set_parse_error(p, "Expected node and port while defining a connection."
                " e.g. 'node(nodetype) OUTPUT_PORT_NAME -> INPUT_PORT_NAME node2(nodetype2)'");
        }

        if (!parse_port(p, &dst_port_name, &dst_port_idx))
            return false;

        in_port_position = get_token_position(&p->current_token);

        if (peek_token(p) != SOL_FBP_TOKEN_IDENTIFIER) {
            return set_parse_error(p, "Expected node and port while defining a connection."
                " e.g. 'node(nodetype) OUTPUT_PORT_NAME -> INPUT_PORT_NAME node2(nodetype2)'");
        }

        if (!parse_node(p, &dst))
            return false;

        sol_fbp_graph_add_in_port(p->graph, dst, dst_port_name, in_port_position);

        r = sol_fbp_graph_add_conn(p->graph, src, src_port_name, src_port_idx, dst, dst_port_name, dst_port_idx, conn_position);
        if (r < 0)
            return handle_conn_error(p, src, &src_port_name, src_port_idx, dst, &dst_port_name, dst_port_idx, &conn_position, r);

        /* When parsing a chain of connections, the destination node
         * from previous will be the source node of the next. */
        src = dst;
    }

    return true;
}

static bool
parse_stmt(struct sol_fbp_parser *p)
{
    switch (peek_token(p)) {
    case SOL_FBP_TOKEN_INPORT_KEYWORD:
        return parse_inport_stmt(p);

    case SOL_FBP_TOKEN_OUTPORT_KEYWORD:
        return parse_outport_stmt(p);

    case SOL_FBP_TOKEN_DECLARE_KEYWORD:
        return parse_declare_stmt(p);

    case SOL_FBP_TOKEN_IDENTIFIER:
        return parse_conn_stmt(p);

    case SOL_FBP_TOKEN_STMT_SEPARATOR:
        next_token(p);
        return true;

    case SOL_FBP_TOKEN_EOF:
        return false;

    case SOL_FBP_TOKEN_ARROW:
        /* Ensure error is at the token that couldn't be parsed. */
        next_token(p);
        return set_parse_error(p, "Arrow symbol must appear between two port names");

    default:
        /* Ensure error is at the token that couldn't be parsed. */
        next_token(p);
        return set_parse_error(p, "Couldn't parse statement.");
    }
}

static void
parse_stmt_list(struct sol_fbp_parser *p)
{
    while (parse_stmt(p)) ;

    if (!p->error_msg && peek_token(p) != SOL_FBP_TOKEN_EOF)
        set_parse_error(p, "Invalid trailing after statements.");
}

static bool
verify_graph(struct sol_fbp_parser *p)
{
    struct sol_fbp_node *n;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&p->graph->nodes, n, i) {
        if (n->component.len == 0) {
            p->error_pos = n->position;
            return set_parse_error(p, "Node '%.*s' doesn't have a type, Node type must be defined. e.g. 'node(nodetype'",
                (int)n->name.len, n->name.data);
        }
    }

    return true;
}

void
sol_fbp_log_print(const char *file, unsigned int line, unsigned int column, const char *format, ...)
{
    char *msg;
    va_list ap;

    va_start(ap, format);
    if (vasprintf(&msg, format, ap) == -1) {
        SOL_WRN("Unable to parse error message %s", strerror(errno));
        va_end(ap);
        return;
    }
    va_end(ap);

    fprintf(stderr, "%s%c%u:%u %s\n", file ? file : "", file ? ':' : '\0', line, column, msg);

    free(msg);
}

void
sol_fbp_error_free(struct sol_fbp_error *e)
{
    SOL_NULL_CHECK(e);
    free(e->msg);
    free(e);
}

struct sol_fbp_error *
sol_fbp_parse(struct sol_str_slice input, struct sol_fbp_graph *g)
{
    struct sol_fbp_parser p = { };
    struct sol_fbp_error *e;

    sol_fbp_init_log_domain();

    sol_fbp_scanner_init(&p.scanner, input);
    p.current_token.type = SOL_FBP_TOKEN_NONE;
    p.pending_token = p.current_token;
    p.error_msg = NULL;
    p.graph = g;

    parse_stmt_list(&p);

    if (!p.error_msg && verify_graph(&p))
        return NULL;

    e = calloc(1, sizeof(struct sol_fbp_error));
    if (!e) {
        free(p.error_msg);
        return NULL;
    }

    e->msg = p.error_msg;
    e->position = p.error_pos;

    return e;
}
