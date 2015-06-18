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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "sol-arena.h"
#include "sol-fbp.h"
#include "sol-fbp-internal-log.h"
#include "sol-file-reader.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-conffile.h"
#include "sol-missing.h"
#include "type-store.h"

static struct {
    const char *fbp_file;
    const char *conf_file;
    struct sol_ptr_vector json_files;
} args;

static struct sol_arena *str_arena;

static void
handle_suboptions(const struct sol_fbp_meta *meta,
    void (*handle_func)(const struct sol_fbp_meta *meta, char *option, uint16_t index))
{
    uint16_t i = 0;
    char *p, *remaining;

    remaining = strndupa(meta->value.data, meta->value.len);
    SOL_NULL_CHECK(remaining);

    printf("        .%.*s = {\n", (int)meta->key.len, meta->key.data);
    while (remaining) {
        p = memchr(remaining, '|', strlen(remaining));
        if (p)
            *p = '\0';
        handle_func(meta, remaining, i);

        if (!p)
            break;

        remaining = p + 1;
        i++;
    }
    printf("        },\n");
}

static void
handle_suboption_with_explicit_fields(const struct sol_fbp_meta *meta, char *option, uint16_t index)
{
    char *p = memchr(option, ':', strlen(option));

    if (!p) {
        sol_fbp_log_print(args.fbp_file, meta->position.line, meta->position.column, "Wrong suboption format, ignoring"
            " value '%s'. You cannot mix the formats, choose one 'opt1:val1|opt2:val2...' or 'val1|val2...'", option);
        return;
    }

    *p = '=';
    printf("            .%s,\n", option);
}

static bool
check_suboption(char *option, const struct sol_fbp_meta *meta)
{
    if (memchr(option, ':', strlen(option))) {
        sol_fbp_log_print(args.fbp_file, meta->position.line, meta->position.column, "Wrong suboption format, ignoring"
            "value '%s'. You cannot mix the formats, choose one 'opt1:val1|opt2:val2...' or 'val1|val2...'", option);
        return false;
    }

    return true;
}

static void
handle_irange_drange_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index)
{
    const char *irange_drange_fields[5] = { "val", "min", "max", "step", NULL };

    if (check_suboption(option, meta))
        printf("            .%s = %s,\n", irange_drange_fields[index], option);
}

static void
handle_rgb_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index)
{
    const char *rgb_fields[7] = { "red", "green", "blue",
                                  "red_max", "green_max", "blue_max", NULL };

    if (check_suboption(option, meta))
        printf("            .%s = %s,\n", rgb_fields[index], option);
}

static void
handle_direction_vector_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index)
{
    const char *direction_vector_fields[7] = { "x", "y", "z", "min", "max", NULL };

    if (check_suboption(option, meta))
        printf("            .%s = %s,\n", direction_vector_fields[index], option);
}

static bool
handle_options(const struct sol_fbp_meta *meta, struct sol_vector *options)
{
    struct option_description *o;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (options, o, i) {
        if (!sol_str_slice_str_eq(meta->key, o->name))
            continue;

        if (streq(o->data_type, "int") || streq(o->data_type, "double")) {
            if (memchr(meta->value.data, ':', meta->value.len))
                handle_suboptions(meta, handle_suboption_with_explicit_fields);
            else
                handle_suboptions(meta, handle_irange_drange_suboption);
        } else if (streq(o->data_type, "rgb")) {
            if (memchr(meta->value.data, ':', meta->value.len))
                handle_suboptions(meta, handle_suboption_with_explicit_fields);
            else
                handle_suboptions(meta, handle_rgb_suboption);
        } else if (streq(o->data_type, "direction_vector")) {
            if (memchr(meta->value.data, ':', meta->value.len))
                handle_suboptions(meta, handle_suboption_with_explicit_fields);
            else
                handle_suboptions(meta, handle_direction_vector_suboption);
        } else if (streq(o->data_type, "string")) {
            if (meta->value.data[0] == '"')
                printf("        .%.*s = %.*s,\n", (int)meta->key.len, meta->key.data, (int)meta->value.len, meta->value.data);
            else
                printf("        .%.*s = \"%.*s\",\n", (int)meta->key.len, meta->key.data, (int)meta->value.len, meta->value.data);

        } else {
            printf("        .%.*s = %.*s,\n", (int)meta->key.len, meta->key.data, (int)meta->value.len, meta->value.data);
        }

        return true;
    }

    sol_fbp_log_print(args.fbp_file, meta->position.line, meta->position.column,
        "Invalid option key '%.*s'", (int)meta->key.len, meta->key.data);
    return false;
}

static void
handle_conffile_option(struct sol_fbp_node *n, const char *option)
{
    struct sol_fbp_meta *m;
    struct sol_str_slice key_slice, value_slice;
    char *p;
    uint16_t i;

    /* FBP option value has a higher priority then conffile option value */
    SOL_VECTOR_FOREACH_IDX (&n->meta, m, i) {
        if (sol_str_slice_str_eq(m->key, option))
            return;
    }

    p = memchr(option, '=', strlen(option));
    if (!p) {
        sol_fbp_log_print(args.fbp_file, n->position.line, n->position.column,
            "Couldn't handle '%s' conffile option, ignoring this option...", option);
        return;
    }
    p++;

    SOL_INT_CHECK(sol_arena_slice_dup_str_n(str_arena, &key_slice, option, p - option - 1), < 0);
    SOL_INT_CHECK(sol_arena_slice_dup_str_n(str_arena, &value_slice, p, strlen(p)), < 0);

    m = sol_vector_append(&n->meta);
    SOL_NULL_CHECK(m);

    m->key = key_slice;
    m->value = value_slice;
}

static const char *
sol_fbp_generator_resolve_id(struct sol_fbp_node *n, const char *id)
{
    const char *type_name;
    const char **opts_as_string;
    const char *const *opt;

    if (sol_conffile_resolve_path(id, &type_name, &opts_as_string, args.conf_file) < 0) {
        sol_fbp_log_print(args.fbp_file, n->position.line, n->position.column, "Couldn't resolve type id '%s'", id);
        return NULL;
    }

    /* Conffile may contain options for this node type */
    for (opt = opts_as_string; *opt != NULL; opt++)
        handle_conffile_option(n, *opt);

    return type_name;
}

static struct type_description *
sol_fbp_generator_resolve_type(struct type_store *store, struct sol_fbp_node *n)
{
    const char *type_name_as_string;
    const char *type_name;
    struct type_description *desc;

    type_name_as_string = strndupa(n->component.data, n->component.len);

    desc = type_store_find(store, type_name_as_string);
    if (desc)
        return desc;

    type_name = sol_fbp_generator_resolve_id(n, type_name_as_string);
    if (!type_name)
        return NULL;

    return type_store_find(store, type_name);
}

static int
compare_conn_specs(const void *a, const void *b)
{
    const struct sol_flow_static_conn_spec *spec_a = a, *spec_b = b;
    int r;

    r = sol_util_int_compare(spec_a->src, spec_b->src);
    if (r != 0)
        return r;
    return sol_util_int_compare(spec_a->src_port, spec_b->src_port);
}

static struct port_description *
check_port_existence(struct sol_vector *ports, struct sol_str_slice *name, uint16_t *port_number)
{
    struct port_description *p;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (ports, p, i) {
        if (sol_str_slice_str_eq(*name, p->name)) {
            *port_number = i;
            return p;
        }
    }

    return NULL;
}

static bool
port_types_compatible(const char *a_type, const char *b_type)
{
    if (streq(a_type, "any") || streq(b_type, "any"))
        return true;

    return streq(a_type, b_type);
}

static bool
handle_port_error(struct sol_vector *ports, struct sol_str_slice *name, struct sol_str_slice *component)
{
    struct sol_fbp_port *p;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (ports, p, i) {
        if (!sol_str_slice_eq(*name, p->name))
            continue;

        sol_fbp_log_print(args.fbp_file, p->position.line, p->position.column,
            "Port '%.*s' doesn't exist for node type '%.*s'",
            (int)name->len, name->data, (int)component->len, component->data);

        break;
    }

    return false;
}

static bool
generate_connections(struct sol_fbp_graph *g, struct type_description **descs)
{
    struct sol_fbp_conn *conn;
    struct sol_flow_static_conn_spec *conn_specs;
    uint16_t i;

    /* Build an array of connections then sort it correctly before
     * generating the code. */
    conn_specs = calloc(g->conns.len, sizeof(struct sol_flow_static_conn_spec));
    SOL_NULL_CHECK(conn_specs, false);

    SOL_VECTOR_FOREACH_IDX (&g->conns, conn, i) {
        struct sol_flow_static_conn_spec *spec = &conn_specs[i];
        struct type_description *src_desc, *dst_desc;
        struct port_description *src_port_desc, *dst_port_desc;

        spec->src = conn->src;
        spec->dst = conn->dst;
        spec->src_port = UINT16_MAX;
        spec->dst_port = UINT16_MAX;

        src_desc = descs[conn->src];
        dst_desc = descs[conn->dst];

        src_port_desc = check_port_existence(&src_desc->out_ports, &conn->src_port, &spec->src_port);
        if (!src_port_desc) {
            struct sol_fbp_node *n = sol_vector_get(&g->nodes, conn->src);
            free(conn_specs);
            return handle_port_error(&n->out_ports, &conn->src_port, &n->component);
        }

        dst_port_desc = check_port_existence(&dst_desc->in_ports, &conn->dst_port, &spec->dst_port);
        if (!dst_port_desc) {
            struct sol_fbp_node *n = sol_vector_get(&g->nodes, conn->dst);
            free(conn_specs);
            return handle_port_error(&n->in_ports, &conn->dst_port, &n->component);
        }

        if (!port_types_compatible(src_port_desc->data_type, dst_port_desc->data_type)) {
            sol_fbp_log_print(args.fbp_file, conn->position.line, conn->position.column,
                "Couldn't connect '%s %.*s -> %.*s %s'. Source port type '%s' doesn't match destiny port type '%s'",
                src_desc->name, (int)conn->src_port.len, conn->src_port.data,
                (int)conn->dst_port.len, conn->dst_port.data, dst_desc->name,
                src_port_desc->data_type, dst_port_desc->data_type);
            free(conn_specs);
            return false;
        }
    }

    qsort(conn_specs, g->conns.len, sizeof(struct sol_flow_static_conn_spec),
        compare_conn_specs);

    printf("static const struct sol_flow_static_conn_spec conns[] = {\n");
    for (i = 0; i < g->conns.len; i++) {
        struct sol_flow_static_conn_spec *spec = &conn_specs[i];
        printf("    { %d, %d, %d, %d },\n",
            spec->src, spec->src_port, spec->dst, spec->dst_port);
    }
    printf("    SOL_FLOW_STATIC_CONN_SPEC_GUARD\n"
        "};\n\n");

    free(conn_specs);
    return true;
}

static void
generate_exports(const struct sol_fbp_graph *g)
{
    struct sol_fbp_exported_port *e;
    struct sol_fbp_node *n;
    struct sol_fbp_port *p;
    uint16_t i, j;

    if (g->exported_in_ports.len > 0) {
        printf("const struct sol_flow_static_port_spec exported_in[] = {\n");
        SOL_VECTOR_FOREACH_IDX (&g->exported_in_ports, e, i) {
            n = sol_vector_get(&g->nodes, e->node);
            SOL_VECTOR_FOREACH_IDX (&n->in_ports, p, j) {
                if (sol_str_slice_eq(e->port, p->name)) {
                    printf("    { %d, %d },\n", e->node, j);
                    break;
                }
            }
        }
        printf("    SOL_FLOW_STATIC_PORT_SPEC_GUARD\n"
            "};\n\n");
    }

    if (g->exported_out_ports.len > 0) {
        printf("const struct sol_flow_static_port_spec exported_out[] = {\n");
        SOL_VECTOR_FOREACH_IDX (&g->exported_out_ports, e, i) {
            n = sol_vector_get(&g->nodes, e->node);
            SOL_VECTOR_FOREACH_IDX (&n->out_ports, p, j) {
                if (sol_str_slice_eq(e->port, p->name)) {
                    printf("    { %d, %d },\n", e->node, j);
                    break;
                }
            }
        }
        printf("    SOL_FLOW_STATIC_PORT_SPEC_GUARD\n"
            "};\n\n");
    }
}

static int
generate(struct sol_fbp_graph *g, struct type_description **descs)
{
    struct sol_fbp_meta *m;
    struct sol_fbp_node *n;
    uint16_t i, j;

    printf("#include \"sol-flow.h\"\n"
        "#include \"sol-flow-node-types.h\"\n"
        "#include \"sol-mainloop.h\"\n"
        "\n"
        "static struct sol_flow_node *flow;\n\n");

    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        if (n->meta.len <= 0)
            continue;

        printf("static const struct %s opts%d =\n", descs[i]->options_symbol, i);
        printf("    %s_OPTIONS_DEFAULTS(\n", descs[i]->symbol);
        SOL_VECTOR_FOREACH_IDX (&n->meta, m, j) {
            if (!handle_options(m, &descs[i]->options))
                return EXIT_FAILURE;
        }
        printf("    );\n\n");
    }

    if (!generate_connections(g, descs))
        return EXIT_FAILURE;

    generate_exports(g);

    printf("static void\n"
        "startup(void)\n"
        "{\n");

    printf("    const struct sol_flow_node_type *type;\n\n"
        "    const struct sol_flow_static_node_spec nodes[] = {\n");
    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        if (n->meta.len <= 0) {
            printf("        [%d] = {%s, \"%.*s\", NULL},\n",
                i, descs[i]->symbol, (int)n->name.len, n->name.data);
        } else {
            printf("        [%d] = {%s, \"%.*s\", (struct sol_flow_node_options *) &opts%d},\n",
                i, descs[i]->symbol, (int)n->name.len, n->name.data, i);
        }
    }
    printf("        SOL_FLOW_STATIC_NODE_SPEC_GUARD\n"
        "    };\n\n");

    printf("    type = sol_flow_static_new_type(nodes, conns, %s, %s, NULL);\n",
        g->exported_in_ports.len > 0 ? "exported_in" : "NULL",
        g->exported_out_ports.len > 0 ? "exported_out" : "NULL");
    printf("    if (!type)\n"
        "        return;\n\n"
        "   flow = sol_flow_node_new(NULL, NULL, type, NULL);\n"
        "}\n\n"
        "static void\n"
        "shutdown(void)\n"
        "{\n"
        "    sol_flow_node_del(flow);\n"
        "}\n\n"
        "SOL_MAIN_DEFAULT(startup, shutdown);\n");

    return EXIT_SUCCESS;
}

static bool
sol_fbp_generator_type_store_load_file(struct type_store *store, const char *json_file)
{
    struct sol_file_reader *fr = NULL;

    fr = sol_file_reader_open(json_file);
    if (!fr) {
        SOL_ERR("Couldn't open json file '%s': %s\n", json_file, sol_util_strerrora(errno));
        return false;
    }

    if (!type_store_read_from_json(store, sol_file_reader_get_all(fr))) {
        SOL_ERR("Couldn't read from json file '%s', please check its format.", json_file);
        sol_file_reader_close(fr);
        return false;
    }

    sol_file_reader_close(fr);

    return true;
}

static bool
sol_fbp_generator_type_store_load(struct type_store *store)
{
    const char *file;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&args.json_files, file, i) {
        if (!sol_fbp_generator_type_store_load_file(store, file))
            return false;
    }

    return true;
}

static bool
sol_fbp_generator_handle_args(int argc, char *argv[])
{
    bool has_json_file = false;
    int opt;

    if (argc < 2) {
        fprintf(stderr, "sol-fbp-generator usage: ./sol-fbp-generator [-c conf_file]"
            "[-j json_file -j json_file ...] fbp_file\n");
        return false;
    }

    sol_ptr_vector_init(&args.json_files);

    while ((opt = getopt(argc, argv, "c:j:")) != -1) {
        switch (opt) {
        case 'c':
            args.conf_file = optarg;
            break;
        case 'j':
            has_json_file = true;
            sol_ptr_vector_append(&args.json_files, optarg);
            break;
        case '?':
            fprintf(stderr, "sol-fbp-generator usage: ./sol-fbp-generator [-c conf_file]"
                "[-j json_file -j json_file ...] fbp_file\n");
            return false;
        }
    }

    if (optind != argc - 1) {
        fprintf(stderr, "A single FBP input file is required.\n");
        return false;
    }

    if (!has_json_file) {
        fprintf(stderr, "At least one JSON file containing the declaration of the nodes"
            " (module) used in the FBP is required.\n");
        return false;
    }

    args.fbp_file = argv[optind];
    return true;
}

static struct type_description **
resolve_nodes(const struct sol_fbp_graph *g, struct type_store *store)
{
    struct sol_fbp_node *n;
    struct type_description **descs;
    uint16_t i;

    descs = calloc(g->nodes.len, sizeof(void *));
    if (!descs)
        return NULL;

    SOL_VECTOR_FOREACH_IDX (&g->nodes, n, i) {
        descs[i] = sol_fbp_generator_resolve_type(store, n);
        if (!descs[i]) {
            free(descs);
            return NULL;
        }
    }

    return descs;
}

int
main(int argc, char *argv[])
{
    struct sol_fbp_error *fbp_error;
    struct sol_file_reader *fr = NULL;
    struct sol_fbp_graph graph = {};
    struct type_store *store;
    struct type_description **descs;
    uint8_t result = EXIT_FAILURE;

    if (sol_init() < 0)
        goto end;

    if (!sol_fbp_generator_handle_args(argc, argv))
        goto fail_args;

    if (args.conf_file && access(args.conf_file, R_OK) == -1) {
        SOL_ERR("Couldn't open file '%s': %s", args.conf_file, sol_util_strerrora(errno));
        goto fail_args;
    }

    store = type_store_new();
    if (!store)
        goto fail_store;
    if (!sol_fbp_generator_type_store_load(store))
        goto fail_store_load;

    fr = sol_file_reader_open(args.fbp_file);
    if (!fr) {
        SOL_ERR("Couldn't open file '%s': %s\n", args.fbp_file, sol_util_strerrora(errno));
        goto fail_file;
    }

    if (sol_fbp_graph_init(&graph) != 0) {
        SOL_ERR("Couldn't initialize graph");
        goto fail_graph;
    }

    fbp_error = sol_fbp_parse(sol_file_reader_get_all(fr), &graph);
    if (fbp_error) {
        sol_fbp_log_print(args.fbp_file, fbp_error->position.line, fbp_error->position.column, fbp_error->msg);
        sol_fbp_error_free(fbp_error);
        goto fail_parse;
    }

    str_arena = sol_arena_new();
    if (!str_arena) {
        SOL_ERR("Couldn't create str arena");
        goto fail_parse;
    }

    descs = resolve_nodes(&graph, store);
    if (!descs) {
        SOL_ERR("Failed to resolve node types");
        goto fail_resolve;
    }

    result = generate(&graph, descs);

fail_resolve:
    free(descs);
    sol_arena_del(str_arena);
fail_parse:
    sol_fbp_graph_fini(&graph);
fail_graph:
    sol_file_reader_close(fr);
fail_file:
fail_store_load:
    type_store_del(store);
fail_args:
fail_store:
    sol_shutdown();
end:
    return result;
}
