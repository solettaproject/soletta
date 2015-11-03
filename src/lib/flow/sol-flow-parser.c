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
#include "sol-flow-internal.h"
#include "sol-flow-metatype.h"
#include "sol-flow-parser.h"
#include "sol-flow-resolver.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"
#include "sol-flow-composed.h"

#include "sol-flow-metatype-builtins-gen.h"

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

    const struct sol_flow_resolver *builtins_resolver;
    struct sol_flow_resolver resolver_with_declares;

    /* Types produced by the parser are owned by it, to ensure that no
     * type get it's dependencies deleted too early. */
    struct sol_ptr_vector types;
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
    struct sol_flow_node_named_options *named_opts)
{
    struct parse_state *state = data;
    struct declared_type *dec_type;
    uint16_t i;
    int err;

    SOL_VECTOR_FOREACH_IDX (&state->declared_types, dec_type, i) {
        if (sol_str_slice_str_eq(dec_type->name, id)) {
            *type = dec_type->type;
            *named_opts = (struct sol_flow_node_named_options){};
            return 0;
        }
    }

    err = sol_flow_resolve(state->parser->builtins_resolver, id, type, named_opts);
    if (err >= 0)
        return 0;

    return sol_flow_resolve(state->parser->resolver, id, type, named_opts);
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

static void
parse_state_fini(struct parse_state *state)
{
    sol_vector_clear(&state->declared_types);
    sol_flow_builder_del(state->builder);
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
    parser->builtins_resolver = sol_flow_get_builtins_resolver();
    parser->resolver = resolver;
    parser->client = client;

    sol_ptr_vector_init(&parser->types);

    return parser;
}

SOL_API int
sol_flow_parser_del(struct sol_flow_parser *parser)
{
    struct sol_flow_node_type *type;
    uint16_t i;

    SOL_NULL_CHECK(parser, -EBADF);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&parser->types, type, i) {
        sol_flow_node_type_del(type);
    }
    sol_ptr_vector_clear(&parser->types);

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
append_node_options(
    struct parse_state *state,
    struct sol_fbp_node *node,
    const struct sol_flow_node_type *type,
    struct sol_flow_node_named_options *named_opts)
{
    struct sol_buffer key = SOL_BUFFER_INIT_EMPTY, value = SOL_BUFFER_INIT_EMPTY;
    struct sol_flow_node_named_options result;
    struct sol_flow_node_named_options_member *m;
    struct sol_fbp_meta *meta;
    uint16_t i, count;
    bool failed = false;
    int r;

    if (node->meta.len == 0)
        return 0;

    if (!type->description || !type->description->options
        || !type->description->options->members) {
        return -ENOTSUP;
    }

    count = named_opts->count + node->meta.len;
    result.members = calloc(count, sizeof(struct sol_flow_node_named_options_member));
    if (!result.members)
        return -errno;

    result.count = named_opts->count;
    m = result.members + result.count;

    SOL_VECTOR_FOREACH_IDX (&node->meta, meta, i) {
        const struct sol_flow_node_options_member_description *mdesc;
        bool found = false, is_string_value;

        if (meta->key.len == 0) {
            sol_fbp_log_print(state->filename, meta->position.line, meta->position.column,
                "Invalid option name '%.*s' of node '%.*s'",
                SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(node->name));
            failed = true;
            continue;
        }

        if (meta->value.len == 0) {
            sol_fbp_log_print(state->filename, meta->position.line, meta->position.column,
                "Non-string empty value for option name '%.*s' of node '%.*s'",
                SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(node->name));
            failed = true;
            continue;
        }

        r = sol_buffer_set_slice(&key, meta->key);
        if (r < 0)
            goto end;

        mdesc = type->description->options->members;
        for (; mdesc->name != NULL; mdesc++) {
            if (streq(mdesc->name, key.data)) {
                found = true;
                break;
            }
        }

        if (!found) {
            sol_fbp_log_print(state->filename, meta->position.line, meta->position.column,
                "Unknown option name '%.*s' of node '%.*s'",
                SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(node->name));
            failed = true;
            continue;
        }

        r = sol_buffer_set_slice(&value, meta->value);
        if (r < 0)
            goto end;

        is_string_value = ((char *)value.data)[0] == '"';

        m->type = sol_flow_node_options_member_type_from_string(mdesc->data_type);
        if (m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN) {
            sol_fbp_log_print(state->filename, meta->position.line, meta->position.column,
                "Unknown type (%u) of option name '%.*s' of node '%.*s'",
                m->type, SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(node->name));
            failed = true;
            continue;
        } else if (m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_STRING) {
            /* We currently don't enforce strings to be quoted. */
            if (is_string_value)
                unescape_str(value.data);
        } else if (is_string_value) {
            sol_fbp_log_print(state->filename, meta->position.line, meta->position.column,
                "Option '%.*s' (%s) of node '%.*s' must not be quoted since it's not a string",
                SOL_STR_SLICE_PRINT(meta->key), sol_flow_node_options_member_type_to_string(m->type),
                SOL_STR_SLICE_PRINT(node->name));
            failed = true;
            continue;
        }

        m->name = mdesc->name;

        r = sol_flow_node_named_options_parse_member(m, value.data, mdesc);
        if (r < 0) {
            sol_fbp_log_print(state->filename, meta->position.line, meta->position.column,
                "Couldn't parse value '%.*s' for option '%.*s' (%s) of node '%.*s'",
                SOL_STR_SLICE_PRINT(meta->value), SOL_STR_SLICE_PRINT(meta->key),
                sol_flow_node_options_member_type_to_string(m->type), SOL_STR_SLICE_PRINT(node->name));
            failed = true;
            continue;
        }

        m++;
        result.count++;
    }

    if (!failed) {
        if (named_opts->count)
            memcpy(result.members, named_opts->members,
                named_opts->count * sizeof(struct sol_flow_node_named_options_member));
        *named_opts = result;
        r = 0;
    } else {
        sol_flow_node_named_options_fini(&result);
        r = -EINVAL;
    }

end:
    sol_buffer_fini(&key);
    sol_buffer_fini(&value);

    if (r == -ENOMEM) {
        sol_fbp_log_print(state->filename, node->position.line, node->position.column,
            "Out of memory when processing node '%.*s'",
            SOL_STR_SLICE_PRINT(node->name));
    }

    return r;
}

static char *
build_node(
    struct parse_state *state,
    struct sol_fbp_node *node)
{
    const struct sol_flow_node_type *type = NULL;
    struct sol_flow_node_options *opts = NULL;
    struct sol_flow_node_named_options named_opts = {};
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

    err = sol_flow_resolve(&state->resolver, component, &type, &named_opts);
    if (err < 0) {
        sol_fbp_log_print(state->filename, node->position.line, node->position.column,
            "Couldn't resolve type '%s' of node '%s'", component, name);
        goto end;
    }

    err = append_node_options(state, node, type, &named_opts);
    if (err < 0) {
        if (err == -ENOTSUP) {
            sol_fbp_log_print(state->filename, node->position.line, node->position.column,
                "Type name '%s' of node '%s' doesn't contain type description for options",
                component, name);
        }
        goto end;
    }

    err = sol_flow_node_options_new(type, &named_opts, &opts);
    if (err < 0) {
        sol_fbp_log_print(state->filename, node->position.line, node->position.column,
            "Couldn't build options for node '%s'", name);
        goto end;
    }

    err = sol_flow_builder_add_node_taking_options(state->builder, name, type, opts);
    if (err < 0) {
        sol_fbp_log_print(state->filename, node->position.line, node->position.column,
            "Couldn't add node '%s'", name);
        goto end;
    }

end:
    sol_flow_node_named_options_fini(&named_opts);

    free(component);
    if (err < 0) {
        name = NULL;
        errno = -err;
    }

    return name;
}

struct metatype_context_internal {
    struct sol_flow_metatype_context base;
    struct sol_flow_parser *parser;
};

static int
create_fbp_type(
    const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type)
{
    struct metatype_context_internal *internal_ctx =
        (struct metatype_context_internal *)ctx;

    struct sol_flow_node_type *result;
    const char *buf, *filename;
    size_t size;
    int err;

    filename = strndupa(ctx->contents.data, ctx->contents.len);
    err = ctx->read_file(ctx, filename, &buf, &size);
    if (err < 0)
        return -EINVAL;

    /* Because its reusing the same parser, there's no need to pass
     * ownership using store_type(), the parser already have it. */
    result = sol_flow_parse_buffer(internal_ctx->parser, buf, size, filename);
    if (!result)
        return -EINVAL;

    *type = result;
    return 0;
}

static int
metatype_read_file(
    const struct sol_flow_metatype_context *ctx,
    const char *name, const char **buf, size_t *size)
{
    const struct metatype_context_internal *internal_ctx =
        (const struct metatype_context_internal *)ctx;
    const struct sol_flow_parser_client *client = internal_ctx->parser->client;

    if (!client || !client->read_file)
        return -ENOSYS;
    return client->read_file(client->data, name, buf, size);
}

static int
metatype_store_type(
    const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type *type)
{
    const struct metatype_context_internal *internal_ctx =
        (const struct metatype_context_internal *)ctx;

    return sol_ptr_vector_append(&internal_ctx->parser->types, type);
}

#if (SOL_FLOW_METATYPE_BUILTINS_COUNT > 0)
static const struct sol_flow_metatype *
get_metatype_by_name(const struct sol_str_slice name)
{
    const struct sol_flow_metatype *metatype, *const *itr;
    int i;

    for (i = 0, itr = SOL_FLOW_METATYPE_BUILTINS_ALL;
        i < SOL_FLOW_METATYPE_BUILTINS_COUNT;
        i++, itr++) {
        metatype = *itr;
        if (sol_str_slice_str_eq(name, metatype->name))
            return metatype;
    }
    return NULL;
}
#endif

static sol_flow_metatype_create_type_func
get_create_type_func(const struct sol_str_slice name)
{
    if (sol_str_slice_str_eq(name, "fbp"))
        return create_fbp_type;

    if (sol_str_slice_str_eq(name, "composed-split"))
        return create_composed_splitter_type;

    if (sol_str_slice_str_eq(name, "composed-new"))
        return create_composed_constructor_type;

#if (SOL_FLOW_METATYPE_BUILTINS_COUNT > 0)
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_metatype_by_name(name);
        if (metatype)
            return metatype->create_type;
    }
#endif

#ifdef ENABLE_DYNAMIC_MODULES
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_dynamic_metatype(name);
        if (metatype)
            return metatype->create_type;
    }
#endif
    return NULL;
}

SOL_API sol_flow_metatype_generate_code_func
sol_flow_metatype_get_generate_code_start_func(const struct sol_str_slice name)
{
    if (sol_str_slice_str_eq(name, "composed-split"))
        return composed_metatype_splitter_generate_code_start;
    if (sol_str_slice_str_eq(name, "composed-new"))
        return composed_metatype_constructor_generate_code_start;
#if (SOL_FLOW_METATYPE_BUILTINS_COUNT > 0)
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_metatype_by_name(name);
        if (metatype)
            return metatype->generate_type_start;
    }
#endif
#ifdef ENABLE_DYNAMIC_MODULES
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_dynamic_metatype(name);
        if (metatype)
            return metatype->generate_type_start;
    }
#endif
    return NULL;
}

SOL_API sol_flow_metatype_generate_code_func
sol_flow_metatype_get_generate_code_type_func(const struct sol_str_slice name)
{
    if (sol_str_slice_str_eq(name, "composed-split"))
        return composed_metatype_splitter_generate_code_type;
    if (sol_str_slice_str_eq(name, "composed-new"))
        return composed_metatype_constructor_generate_code_type;
#if (SOL_FLOW_METATYPE_BUILTINS_COUNT > 0)
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_metatype_by_name(name);
        if (metatype)
            return metatype->generate_type_body;
    }
#endif
#ifdef ENABLE_DYNAMIC_MODULES
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_dynamic_metatype(name);
        if (metatype)
            return metatype->generate_type_body;
    }
#endif
    return NULL;
}

SOL_API sol_flow_metatype_generate_code_func
sol_flow_metatype_get_generate_code_end_func(const struct sol_str_slice name)
{
    if (sol_str_slice_str_eq(name, "composed-split"))
        return composed_metatype_splitter_generate_code_end;
    if (sol_str_slice_str_eq(name, "composed-new"))
        return composed_metatype_constructor_generate_code_end;
#if (SOL_FLOW_METATYPE_BUILTINS_COUNT > 0)
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_metatype_by_name(name);
        if (metatype)
            return metatype->generate_type_end;
    }
#endif
#ifdef ENABLE_DYNAMIC_MODULES
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_dynamic_metatype(name);
        if (metatype)
            return metatype->generate_type_end;
    }
#endif
    return NULL;
}

SOL_API sol_flow_metatype_ports_description_func
sol_flow_metatype_get_ports_description_func(const struct sol_str_slice name)
{
    if (sol_str_slice_str_eq(name, "composed-split"))
        return composed_metatype_splitter_get_ports_description;
    if (sol_str_slice_str_eq(name, "composed-new"))
        return composed_metatype_constructor_get_ports_description;
#if (SOL_FLOW_METATYPE_BUILTINS_COUNT > 0)
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_metatype_by_name(name);
        if (metatype)
            return metatype->ports_description;
    }
#endif
#ifdef ENABLE_DYNAMIC_MODULES
    {
        const struct sol_flow_metatype *metatype;

        metatype = get_dynamic_metatype(name);
        if (metatype)
            return metatype->ports_description;
    }
#endif
    return NULL;
}

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
        struct sol_flow_node_type *type = NULL;
        sol_flow_metatype_create_type_func creator;
        struct metatype_context_internal internal_ctx = {
            .base = {
                .name = dec->name,
                .contents = dec->contents,
                .read_file = metatype_read_file,
                .store_type = metatype_store_type,
            },
            .parser = state->parser,
        };

        creator = get_create_type_func(dec->metatype);
        if (!creator) {
            SOL_ERR("Couldn't handle declaration '%.*s' of metatype '%.*s'",
                SOL_STR_SLICE_PRINT(dec->name),
                SOL_STR_SLICE_PRINT(dec->metatype));
            return -EBADF;
        }

        err = creator(&internal_ctx.base, &type);
        if (err < 0) {
            SOL_ERR("Failed when creating type '%.*s' of metatype '%.*s'",
                SOL_STR_SLICE_PRINT(dec->name),
                SOL_STR_SLICE_PRINT(dec->metatype));
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
    struct sol_buffer src_port_buf = SOL_BUFFER_INIT_EMPTY, dst_port_buf = SOL_BUFFER_INIT_EMPTY;
    struct sol_buffer opt_name_buf = SOL_BUFFER_INIT_EMPTY;
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
        err = sol_buffer_set_slice(&src_port_buf, c->src_port);
        if (err < 0)
            goto end;

        err = sol_buffer_set_slice(&dst_port_buf, c->dst_port);
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

        err = sol_buffer_set_slice(&dst_port_buf, ep->port);
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

        err = sol_buffer_set_slice(&src_port_buf, ep->port);
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

        err = sol_buffer_set_slice(&opt_name_buf, opt->node_option);
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

    err = sol_ptr_vector_append(&parser->types, type);
    if (err < 0) {
        sol_flow_node_type_del(type);
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

struct metatype_context_with_buf {
    struct metatype_context_internal base;

    const char *filename;
    const char *buf;
    size_t len;
};

/* "Fake" implementation of read_file that expose only one file using
 * a buffer. */
static int
metatype_with_buf_read_file(
    const struct sol_flow_metatype_context *ctx,
    const char *name, const char **buf, size_t *size)
{
    const struct metatype_context_with_buf *ctx_with_buf =
        (const struct metatype_context_with_buf *)ctx;

    if (!streq(name, ctx_with_buf->filename))
        return -ENOENT;
    *buf = ctx_with_buf->buf;
    *size = ctx_with_buf->len;
    return 0;
}

SOL_API struct sol_flow_node_type *
sol_flow_parse_buffer_metatype(
    struct sol_flow_parser *parser,
    const char *metatype,
    const char *buf,
    size_t len,
    const char *filename)
{
    struct sol_flow_node_type *type = NULL;
    sol_flow_metatype_create_type_func creator;
    int err;

    struct metatype_context_with_buf ctx_with_buf = {
        .base = {
            .base = {
                .read_file = metatype_with_buf_read_file,
                .store_type = metatype_store_type,
            },
            .parser = parser,
        },
        .filename = filename,
        .buf = buf,
        .len = len,
    };

    SOL_NULL_CHECK(parser, NULL);
    SOL_NULL_CHECK(metatype, NULL);
    SOL_NULL_CHECK(buf, NULL);
    SOL_NULL_CHECK(filename, NULL);
    SOL_INT_CHECK(len, == 0, NULL);

    ctx_with_buf.base.base.name = ctx_with_buf.base.base.contents = sol_str_slice_from_str(filename);

    /* TODO: try to make FBP less special. Implement parse buffer in
     * terms of create function. Probably the call to build_flow
     * should be moved inside the creator function. */
    if (streq(metatype, "fbp"))
        return sol_flow_parse_buffer(parser, buf, len, filename);

    creator = get_create_type_func(sol_str_slice_from_str(metatype));
    if (!creator) {
        SOL_ERR("Failed to find metatype '%s'", metatype);
        errno = -EINVAL;
        return NULL;
    }

    err = creator(&ctx_with_buf.base.base, &type);
    if (err < 0) {
        SOL_ERR("Failed when creating type of metatype '%s'", metatype);
        errno = -err;
        return NULL;
    }

    return type;
}

SOL_API struct sol_flow_node_type *
sol_flow_parse_string_metatype(
    struct sol_flow_parser *parser,
    const char *metatype,
    const char *str,
    const char *filename)
{
    SOL_NULL_CHECK(parser, NULL);
    SOL_NULL_CHECK(metatype, NULL);
    SOL_NULL_CHECK(str, NULL);
    SOL_NULL_CHECK(filename, NULL);

    return sol_flow_parse_buffer_metatype(
        parser, metatype, str, strlen(str), filename);
}
