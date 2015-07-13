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

#include "sol-arena.h"
#include "sol-buffer.h"
#include "sol-fbp.h"
#include "sol-flow-builder.h"
#include "sol-flow-parser.h"
#include "sol-flow-resolver.h"
#include "sol-log.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-vector.h"

#ifdef JAVASCRIPT
#include "sol-flow-js.h"
#endif

#define SOL_FLOW_PARSER_CLIENT_API_CHECK(client, expected, ...)          \
    do {                                                                \
        if ((client)->api_version != (expected)) {                      \
            SOL_WRN("Invalid " # client " %p API version(%lu), "         \
                "expected " # expected "(%lu)",                     \
                (client), (client)->api_version, (expected));       \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

struct sol_flow_parser {
    const struct sol_flow_resolver *resolver;
    const struct sol_flow_parser_client *client;

    struct sol_flow_resolver resolver_with_declares;
    struct sol_ptr_vector builders;

    struct sol_ptr_vector no_builder_types;
};

struct declared_type {
    struct sol_str_slice name;
    const struct sol_flow_node_type *type;
};

/* This state is kept during a single parse run, some of its members
 * are then taken to be stored in the main parser struct if the parser
 * was succesful. */
struct parse_state {
    struct sol_flow_parser *parser;
    struct sol_flow_builder *builder;

    struct sol_str_slice input;
    const char *filename;

    struct sol_fbp_graph graph;

    /* Keep node names indexed by position to be used when making
     * connections in builder API. */
    char **node_names;

    /* Resolver that also considers the types declared during the
     * parse. */
    struct sol_flow_resolver resolver;

    struct sol_vector declared_types;
    struct sol_arena *arena;
};



static int
parse_state_resolve(void *data, const char *id,
    struct sol_flow_node_type const **type,
    char const ***opts_strv)
{
    struct parse_state *state = data;
    struct declared_type *dec_type;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&state->declared_types, dec_type, i) {
        if (sol_str_slice_str_eq(dec_type->name, id)) {
            *type = dec_type->type;
            return 0;
        }
    }
    return sol_flow_resolve(state->parser->resolver, id, type, opts_strv);
}

static void
parse_state_init_resolver(struct parse_state *state)
{
    struct sol_flow_resolver *r = &state->resolver;

    r->api_version = SOL_FLOW_RESOLVER_API_VERSION;
    r->name = "parse-state-resolver-with-declares";
    r->data = state;
    r->resolve = parse_state_resolve;
}

static int
parse_state_init(
    struct parse_state *state,
    struct sol_flow_parser *parser,
    struct sol_str_slice input,
    const char *filename)
{
    int err;

    state->parser = parser;
    state->input = input;
    state->filename = filename;
    state->node_names = NULL;

    err = sol_fbp_graph_init(&state->graph);
    if (err < 0) {
        SOL_ERR("Couldn't initialize memory for parsing input");
        goto fail;
    }

    state->builder = sol_flow_builder_new();
    if (!state->builder) {
        err = -errno;
        goto fail_builder;
    }

    state->arena = sol_arena_new();
    if (!state->arena) {
        err = -errno;
        goto fail_arena;
    }

    parse_state_init_resolver(state);
    sol_flow_builder_set_resolver(state->builder, &state->resolver);

    sol_vector_init(&state->declared_types, sizeof(struct declared_type));

    return 0;

fail_arena:
    sol_flow_builder_del(state->builder);

fail_builder:
    sol_fbp_graph_fini(&state->graph);

fail:
    return err;
}

static struct sol_flow_builder *
parse_state_take_builder(struct parse_state *state)
{
    struct sol_flow_builder *builder;

    builder = state->builder;
    state->builder = NULL;
    return builder;
}

static void
parse_state_fini(struct parse_state *state)
{
    sol_vector_clear(&state->declared_types);
    if (state->builder) {
        sol_flow_builder_del(state->builder);
        state->builder = NULL;
    }

    sol_fbp_graph_fini(&state->graph);
    sol_arena_del(state->arena);
}

SOL_API struct sol_flow_parser *
sol_flow_parser_new(
    const struct sol_flow_parser_client *client,
    const struct sol_flow_resolver *resolver)
{
    struct sol_flow_parser *parser;

    /* We accept NULL client. */
    if (client) {
        SOL_FLOW_PARSER_CLIENT_API_CHECK(
            client, SOL_FLOW_PARSER_CLIENT_API_VERSION, NULL);
    }

    parser = calloc(1, sizeof(struct sol_flow_parser));

    if (!parser)
        return NULL;

    if (!resolver)
        resolver = sol_flow_get_default_resolver();
    parser->resolver = resolver;
    parser->client = client;

    sol_ptr_vector_init(&parser->builders);

    sol_ptr_vector_init(&parser->no_builder_types);

    return parser;
}

SOL_API int
sol_flow_parser_del(struct sol_flow_parser *parser)
{
    struct sol_flow_builder *b;
    struct sol_flow_node_type *t;
    uint16_t i;

    SOL_NULL_CHECK(parser, -EBADF);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&parser->builders, b, i)
        sol_flow_builder_del(b);
    sol_ptr_vector_clear(&parser->builders);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&parser->no_builder_types, t, i)
        sol_flow_node_type_del(t);
    sol_ptr_vector_clear(&parser->no_builder_types);

    free(parser);
    return 0;
}

static void
unescape_str(char *orig_str)
{
    char *curr_char = orig_str;
    char *peek_char = orig_str;
    const char unescaped_char_table[] = {
        ['a'] = '\a',
        ['b'] = '\b',
        ['f'] = '\f',
        ['n'] = '\n',
        ['r'] = '\r',
        ['t'] = '\t',
        ['v'] = '\v',
        ['"'] = '"',
        ['\''] = '\'',
        ['\\'] = '\\',
    };

    peek_char++;
    while (*peek_char != '"') {
        if (*peek_char == '\\') {
            peek_char += 1;
            *(curr_char++) = unescaped_char_table[(int)*(peek_char++)];
        } else {
            *(curr_char++) = *(peek_char++);
        }
    }

    *curr_char = '\0';
}

static int
get_options_array(struct sol_fbp_node *node, char ***opts_array)
{
    struct sol_fbp_meta *m;
    unsigned int i, count, pos;
    char **tmp_array, **opts_it;

    count = node->meta.len;
    if (count == 0)
        return 0;

    /* Extra slot for NULL sentinel at the end. */
    tmp_array = calloc(count + 1, sizeof(char *));
    if (!tmp_array)
        return -ENOMEM;

    pos = 0;
    SOL_VECTOR_FOREACH_IDX (&node->meta, m, i) {
        char *entry;
        if (m->value.len == 0)
            continue;

        if (asprintf(&entry, "%.*s=%.*s", SOL_STR_SLICE_PRINT(m->key),
            SOL_STR_SLICE_PRINT(m->value)) < 0)
            goto fail;

        if (entry[m->key.len + 1] == '"') {
            unescape_str(entry + m->key.len + 1);
        }

        tmp_array[pos] = entry;
        pos++;
    }

    *opts_array = tmp_array;
    return 0;

fail:
    for (opts_it = tmp_array; *opts_it != NULL; opts_it++)
        free(*opts_it);
    free(tmp_array);
    return -ENOMEM;
}

static void
del_options_array(char **opts_array)
{
    char **opts_it;

    if (!opts_array)
        return;
    for (opts_it = opts_array; *opts_it != NULL; opts_it++)
        free(*opts_it);
    free(opts_array);
}

static char *
build_node(
    struct parse_state *state,
    struct sol_fbp_node *node)
{
    char **opts_array = NULL;
    char *component, *name = NULL;
    int err;

    component = strndup(node->component.data, node->component.len);
    if (!component) {
        return NULL;
    }

    name = sol_arena_strndup(state->arena, node->name.data, node->name.len);
    if (!name) {
        err = -errno;
        goto end;
    }

    err = get_options_array(node, &opts_array);
    if (err < 0) {
        sol_fbp_log_print(state->filename, node->position.line, node->position.column,
            "Couldn't get options for node '%s'", name);
        err = -errno;
        goto end;
    }

    err = sol_flow_builder_add_node_by_type(state->builder, name, component, (const char *const *)opts_array);
    if (err < 0) {
        if (err == -ENOENT) {
            sol_fbp_log_print(state->filename, node->position.line, node->position.column,
                "Couldn't resolve type name '%s'", component);
        } else if (err == -EINVAL) {
            sol_fbp_log_print(state->filename, node->position.line, node->position.column,
                "Couldn't build options for node '%s'", name);
        }
    }

    del_options_array(opts_array);

end:
    free(component);
    if (err < 0) {
        name = NULL;
        errno = -err;
    }

    return name;
}

typedef int (*type_creator_func)(
    struct parse_state *,
    struct sol_str_slice,
    struct sol_str_slice,
    const struct sol_flow_node_type **);

static int
create_fbp_type(
    struct parse_state *state,
    struct sol_str_slice name,
    struct sol_str_slice contents,
    const struct sol_flow_node_type **type)
{
    const struct sol_flow_node_type *result;
    const struct sol_flow_parser_client *client = state->parser->client;
    const char *buf, *filename;
    size_t size;
    int err;

    if (!client || !client->read_file)
        return -ENOSYS;

    filename = sol_arena_strdup_slice(state->arena, contents);
    err = client->read_file(client->data, filename, &buf, &size);
    if (err < 0)
        return -EINVAL;

    result = sol_flow_parse_buffer(state->parser, buf, size, filename);
    if (!result)
        return -EINVAL;

    *type = result;
    return 0;
}

#ifdef JAVASCRIPT
static int
create_js_type(
    struct parse_state *state,
    struct sol_str_slice name,
    struct sol_str_slice contents,
    const struct sol_flow_node_type **type)
{
    const struct sol_flow_parser_client *client = state->parser->client;
    const char *buf, *filename;
    struct sol_flow_node_type *result;
    size_t size;
    int err;

    SOL_NULL_CHECK(client, -ENOSYS);
    SOL_NULL_CHECK(client->read_file, -ENOSYS);

    filename = sol_arena_strdup_slice(state->arena, contents);
    err = client->read_file(client->data, filename, &buf, &size);
    if (err < 0)
        return -EINVAL;

    result = sol_flow_js_new_type(buf, size);
    if (!result)
        return -EINVAL;

    if (sol_ptr_vector_append(&state->parser->no_builder_types, result) < 0) {
        sol_flow_node_type_del(result);
        return -ENOMEM;
    }

    *type = result;
    return 0;
}
#endif

static const struct sol_str_table_ptr creator_table[] = {
    SOL_STR_TABLE_PTR_ITEM("fbp", create_fbp_type),
#ifdef JAVASCRIPT
    SOL_STR_TABLE_PTR_ITEM("js", create_js_type),
#endif
    { }
};

static int
parse_declarations(struct parse_state *state)
{
    struct sol_fbp_declaration *dec;
    struct declared_type *dec_type;
    uint16_t i;

    if (state->graph.declarations.len == 0)
        return 0;


    SOL_VECTOR_FOREACH_IDX (&state->graph.declarations, dec, i) {
        int err;
        const struct sol_flow_node_type *type = NULL;
        type_creator_func creator = sol_str_table_ptr_lookup_fallback(
            creator_table, dec->kind, NULL);

        if (!creator) {
            SOL_ERR("Couldn't handle declaration '%.*s' of kind '%.*s'",
                SOL_STR_SLICE_PRINT(dec->name),
                SOL_STR_SLICE_PRINT(dec->kind));
            return -EBADF;
        }

        err = creator(state, dec->name, dec->contents, &type);
        if (err < 0) {
            SOL_ERR("Failed when creating type '%.*s' of kind '%.*s'",
                SOL_STR_SLICE_PRINT(dec->name),
                SOL_STR_SLICE_PRINT(dec->kind));
            return -EINVAL;
        }

        dec_type = sol_vector_append(&state->declared_types);
        if (!dec_type)
            return -ENOMEM;
        dec_type->name = dec->name;
        dec_type->type = type;
    }

    return 0;
}

static struct sol_flow_node_type *
build_flow(struct parse_state *state)
{
    struct sol_fbp_error *fbp_error;
    struct sol_fbp_graph *graph = &state->graph;
    struct sol_fbp_node *n;
    struct sol_fbp_conn *c;
    struct sol_fbp_exported_port *ep;
    struct sol_fbp_option *opt;
    struct sol_flow_node_type *type = NULL;
    struct sol_buffer src_port_buf = SOL_BUFFER_EMPTY, dst_port_buf = SOL_BUFFER_EMPTY;
    struct sol_buffer opt_name_buf = SOL_BUFFER_EMPTY;
    int i, err = 0;

    fbp_error = sol_fbp_parse(state->input, &state->graph);
    if (fbp_error) {
        sol_fbp_log_print(state->filename, fbp_error->position.line, fbp_error->position.column, fbp_error->msg);
        sol_fbp_error_free(fbp_error);
        errno = -err;
        return NULL;
    }

    err = parse_declarations(state);
    if (err < 0)
        goto end;

    state->node_names = calloc(graph->nodes.len, sizeof(char *));
    if (!state->node_names)
        return NULL;

    SOL_VECTOR_FOREACH_IDX (&graph->nodes, n, i) {
        state->node_names[i] = build_node(state, n);
        if (!state->node_names[i]) {
            err = -errno;
            goto end;
        }
    }

    SOL_VECTOR_FOREACH_IDX (&graph->conns, c, i) {
        err = sol_buffer_copy_slice(&src_port_buf, c->src_port);
        if (err < 0)
            goto end;

        err = sol_buffer_copy_slice(&dst_port_buf, c->dst_port);
        if (err < 0)
            goto end;

        err = sol_flow_builder_connect(state->builder,
            state->node_names[c->src], src_port_buf.data, c->src_port_idx,
            state->node_names[c->dst], dst_port_buf.data, c->dst_port_idx);
        if (err < 0) {
            sol_fbp_log_print(state->filename, c->position.line, c->position.column,
                "Couldn't connect '%s %s -> %s %s'",
                state->node_names[c->src], src_port_buf.data,
                state->node_names[c->dst], dst_port_buf.data);
            goto end;
        }
    }

    SOL_VECTOR_FOREACH_IDX (&graph->exported_in_ports, ep, i) {
        char *exported_name;

        err = sol_buffer_copy_slice(&dst_port_buf, ep->port);
        if (err < 0)
            goto end;

        exported_name = sol_arena_strdup_slice(
            state->arena, ep->exported_name);
        if (!exported_name) {
            err = -errno;
            goto end;
        }

        sol_flow_builder_export_in_port(
            state->builder, state->node_names[ep->node],
            dst_port_buf.data, ep->port_idx, exported_name);
    }

    SOL_VECTOR_FOREACH_IDX (&graph->exported_out_ports, ep, i) {
        char *exported_name;

        err = sol_buffer_copy_slice(&src_port_buf, ep->port);
        if (err < 0)
            goto end;

        exported_name = sol_arena_strdup_slice(
            state->arena, ep->exported_name);
        if (!exported_name) {
            err = -errno;
            goto end;
        }

        sol_flow_builder_export_out_port(
            state->builder, state->node_names[ep->node],
            src_port_buf.data, ep->port_idx, exported_name);
    }

    SOL_VECTOR_FOREACH_IDX (&graph->options, opt, i) {
        char *exported_name;

        err = sol_buffer_copy_slice(&opt_name_buf, opt->node_option);
        if (err < 0)
            goto end;

        exported_name = sol_arena_strdup_slice(state->arena, opt->name);
        if (!exported_name) {
            err = -errno;
            goto end;
        }

        sol_flow_builder_export_option(
            state->builder, state->node_names[opt->node],
            opt_name_buf.data, exported_name);
    }

    type = sol_flow_builder_get_node_type(state->builder);
    if (!type)
        err = -ECANCELED;

end:
    free(state->node_names);

    sol_buffer_fini(&src_port_buf);
    sol_buffer_fini(&dst_port_buf);
    sol_buffer_fini(&opt_name_buf);
    if (err < 0)
        errno = -err;
    return type;
}

SOL_API struct sol_flow_node_type *
sol_flow_parse_buffer(
    struct sol_flow_parser *parser,
    const char *buf,
    size_t len,
    const char *filename)
{
    struct parse_state state;
    struct sol_flow_node_type *type = NULL;
    int err;

    struct sol_str_slice input = { .data = buf, .len = len };

    SOL_NULL_CHECK(buf, NULL);
    SOL_INT_CHECK(len, == 0, NULL);

    err = parse_state_init(&state, parser, input, filename);
    if (err < 0)
        return NULL;

    type = build_flow(&state);
    if (!type) {
        err = -errno;
        goto end;
    }

    err = sol_ptr_vector_append(&parser->builders,
        parse_state_take_builder(&state));
    if (err < 0) {
        type = NULL;
        goto end;
    }

end:
    parse_state_fini(&state);
    if (err < 0)
        errno = -err;
    return type;
}

SOL_API struct sol_flow_node_type *
sol_flow_parse_string(
    struct sol_flow_parser *parser,
    const char *cstr,
    const char *filename)
{
    return sol_flow_parse_buffer(parser, cstr, strlen(cstr), filename);
}
