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
#include <libgen.h>
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

static const char *SYMBOL_PLACEHOLDER_PREFIX = "NULL /* ";
static const char *SYMBOL_PLACEHOLDER_SUFFIX = " */";

static struct {
    const char *conf_file;
    struct sol_ptr_vector json_files;
    char *fbp_basename;
    char *fbp_dirname;
} args;

static struct sol_arena *str_arena;

struct fbp_data {
    struct type_description **descriptions;
    char *filename;
    char *name;
    struct sol_fbp_graph graph;
};

static void
handle_suboptions(const struct sol_fbp_meta *meta,
    void (*handle_func)(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file), const char *fbp_file)
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
        handle_func(meta, remaining, i, fbp_file);

        if (!p)
            break;

        remaining = p + 1;
        i++;
    }
    printf("        },\n");
}

static void
handle_suboption_with_explicit_fields(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file)
{
    char *p = memchr(option, ':', strlen(option));

    if (!p) {
        sol_fbp_log_print(fbp_file, meta->position.line, meta->position.column, "Wrong suboption format, ignoring"
            " value '%s'. You cannot mix the formats, choose one 'opt1:val1|opt2:val2...' or 'val1|val2...'", option);
        return;
    }

    *p = '=';
    printf("            .%s,\n", option);
}

static bool
check_suboption(char *option, const struct sol_fbp_meta *meta, const char *fbp_file)
{
    if (memchr(option, ':', strlen(option))) {
        sol_fbp_log_print(fbp_file, meta->position.line, meta->position.column, "Wrong suboption format, ignoring"
            "value '%s'. You cannot mix the formats, choose one 'opt1:val1|opt2:val2...' or 'val1|val2...'", option);
        return false;
    }

    return true;
}

static void
handle_irange_drange_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file)
{
    const char *irange_drange_fields[5] = { "val", "min", "max", "step", NULL };

    if (check_suboption(option, meta, fbp_file))
        printf("            .%s = %s,\n", irange_drange_fields[index], option);
}

static void
handle_rgb_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file)
{
    const char *rgb_fields[7] = { "red", "green", "blue",
                                  "red_max", "green_max", "blue_max", NULL };

    if (check_suboption(option, meta, fbp_file))
        printf("            .%s = %s,\n", rgb_fields[index], option);
}

static void
handle_direction_vector_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file)
{
    const char *direction_vector_fields[7] = { "x", "y", "z", "min", "max", NULL };

    if (check_suboption(option, meta, fbp_file))
        printf("            .%s = %s,\n", direction_vector_fields[index], option);
}

static bool
handle_options(const struct sol_fbp_meta *meta, struct sol_vector *options, const char *fbp_file)
{
    struct option_description *o;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (options, o, i) {
        if (!sol_str_slice_str_eq(meta->key, o->name))
            continue;

        if (streq(o->data_type, "int") || streq(o->data_type, "double")) {
            if (memchr(meta->value.data, ':', meta->value.len))
                handle_suboptions(meta, handle_suboption_with_explicit_fields, fbp_file);
            else
                handle_suboptions(meta, handle_irange_drange_suboption, fbp_file);
        } else if (streq(o->data_type, "rgb")) {
            if (memchr(meta->value.data, ':', meta->value.len))
                handle_suboptions(meta, handle_suboption_with_explicit_fields, fbp_file);
            else
                handle_suboptions(meta, handle_rgb_suboption, fbp_file);
        } else if (streq(o->data_type, "direction_vector")) {
            if (memchr(meta->value.data, ':', meta->value.len))
                handle_suboptions(meta, handle_suboption_with_explicit_fields, fbp_file);
            else
                handle_suboptions(meta, handle_direction_vector_suboption, fbp_file);
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

    sol_fbp_log_print(fbp_file, meta->position.line, meta->position.column,
        "Invalid option key '%.*s'", (int)meta->key.len, meta->key.data);
    return false;
}

static void
handle_conffile_option(struct sol_fbp_node *n, const char *option, const char *fbp_file)
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
        sol_fbp_log_print(fbp_file, n->position.line, n->position.column,
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
sol_fbp_generator_resolve_id(struct sol_fbp_node *n, const char *id, const char *fbp_file)
{
    const char *type_name;
    const char **opts_as_string;
    const char *const *opt;

    if (sol_conffile_resolve_path(id, &type_name, &opts_as_string, args.conf_file) < 0) {
        sol_fbp_log_print(fbp_file, n->position.line, n->position.column, "Couldn't resolve type id '%s'", id);
        return NULL;
    }

    /* Conffile may contain options for this node type */
    for (opt = opts_as_string; *opt != NULL; opt++)
        handle_conffile_option(n, *opt, fbp_file);

    return type_name;
}

static struct type_description *
sol_fbp_generator_resolve_type(struct type_store *store, struct sol_fbp_node *n, const char *fbp_file)
{
    const char *type_name_as_string;
    const char *type_name;
    struct type_description *desc;

    type_name_as_string = strndupa(n->component.data, n->component.len);

    desc = type_store_find(store, type_name_as_string);
    if (desc)
        return desc;

    type_name = sol_fbp_generator_resolve_id(n, type_name_as_string, fbp_file);
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
handle_port_error(struct sol_vector *ports, struct sol_str_slice *name, struct sol_str_slice *component, const char *fbp_file)
{
    struct sol_fbp_port *p;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (ports, p, i) {
        if (!sol_str_slice_eq(*name, p->name))
            continue;

        sol_fbp_log_print(fbp_file, p->position.line, p->position.column,
            "Port '%.*s' doesn't exist for node type '%.*s'",
            (int)name->len, name->data, (int)component->len, component->data);

        break;
    }

    return false;
}

static bool
generate_connections(const struct fbp_data *data)
{
    struct sol_fbp_conn *conn;
    struct sol_flow_static_conn_spec *conn_specs;
    uint16_t i;

    /* Build an array of connections then sort it correctly before
     * generating the code. */
    conn_specs = calloc(data->graph.conns.len, sizeof(struct sol_flow_static_conn_spec));
    SOL_NULL_CHECK(conn_specs, false);

    SOL_VECTOR_FOREACH_IDX (&data->graph.conns, conn, i) {
        struct sol_flow_static_conn_spec *spec = &conn_specs[i];
        struct type_description *src_desc, *dst_desc;
        struct port_description *src_port_desc, *dst_port_desc;

        spec->src = conn->src;
        spec->dst = conn->dst;
        spec->src_port = UINT16_MAX;
        spec->dst_port = UINT16_MAX;

        src_desc = data->descriptions[conn->src];
        dst_desc = data->descriptions[conn->dst];

        src_port_desc = check_port_existence(&src_desc->out_ports, &conn->src_port, &spec->src_port);
        if (!src_port_desc) {
            struct sol_fbp_node *n = sol_vector_get(&data->graph.nodes, conn->src);
            free(conn_specs);
            return handle_port_error(&n->out_ports, &conn->src_port, &n->component, data->filename);
        }

        dst_port_desc = check_port_existence(&dst_desc->in_ports, &conn->dst_port, &spec->dst_port);
        if (!dst_port_desc) {
            struct sol_fbp_node *n = sol_vector_get(&data->graph.nodes, conn->dst);
            free(conn_specs);
            return handle_port_error(&n->in_ports, &conn->dst_port, &n->component, data->filename);
        }

        if (!port_types_compatible(src_port_desc->data_type, dst_port_desc->data_type)) {
            sol_fbp_log_print(data->filename, conn->position.line, conn->position.column,
                "Couldn't connect '%s %.*s -> %.*s %s'. Source port type '%s' doesn't match destiny port type '%s'",
                src_desc->name, (int)conn->src_port.len, conn->src_port.data,
                (int)conn->dst_port.len, conn->dst_port.data, dst_desc->name,
                src_port_desc->data_type, dst_port_desc->data_type);
            free(conn_specs);
            return false;
        }
    }

    qsort(conn_specs, data->graph.conns.len, sizeof(struct sol_flow_static_conn_spec),
        compare_conn_specs);

    printf("static const struct sol_flow_static_conn_spec conns%s[] = {\n", data->name);
    for (i = 0; i < data->graph.conns.len; i++) {
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
generate_exports(const struct fbp_data *data)
{
    struct sol_fbp_exported_port *e;
    struct sol_fbp_node *n;
    struct sol_fbp_port *p;
    uint16_t i, j;

    if (data->graph.exported_in_ports.len > 0) {
        printf("const struct sol_flow_static_port_spec exported_in%s[] = {\n", data->name);
        SOL_VECTOR_FOREACH_IDX (&data->graph.exported_in_ports, e, i) {
            n = sol_vector_get(&data->graph.nodes, e->node);
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

    if (data->graph.exported_out_ports.len > 0) {
        printf("const struct sol_flow_static_port_spec exported_out%s[] = {\n", data->name);
        SOL_VECTOR_FOREACH_IDX (&data->graph.exported_out_ports, e, i) {
            n = sol_vector_get(&data->graph.nodes, e->node);
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
generate(struct sol_vector *fbp_data_vector)
{
    const size_t symbol_placeholder_prefix_size = strlen(SYMBOL_PLACEHOLDER_PREFIX);
    const size_t symbol_placeholder_suffix_size = strlen(SYMBOL_PLACEHOLDER_SUFFIX);
    struct fbp_data *data;
    struct sol_fbp_meta *m;
    struct sol_fbp_node *n;
    uint16_t i, j, k;

    printf("#include \"sol-flow.h\"\n"
        "#include \"sol-flow-node-types.h\"\n"
        "#include \"sol-mainloop.h\"\n"
        "\n"
        "static struct sol_flow_node *flow;\n\n");

    SOL_VECTOR_FOREACH_REVERSE_IDX (fbp_data_vector, data, i) {
        SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, j) {
            if (n->meta.len <= 0)
                continue;

            printf("static const struct %s opts%s%d =\n", data->descriptions[j]->options_symbol, data->name, j);
            printf("    %s_OPTIONS_DEFAULTS(\n", data->descriptions[j]->symbol);
            SOL_VECTOR_FOREACH_IDX (&n->meta, m, k) {
                if (!handle_options(m, &data->descriptions[j]->options, data->filename))
                    return EXIT_FAILURE;
            }
            printf("    );\n\n");
        }

        if (!generate_connections(data))
            return EXIT_FAILURE;

        generate_exports(data);
    }

    printf("static void\n"
        "startup(void)\n"
        "{\n");

    SOL_VECTOR_FOREACH_REVERSE_IDX (fbp_data_vector, data, i) {
        printf("    static struct sol_flow_node_type *type%s;\n\n"
            "    struct sol_flow_static_node_spec nodes%s[] = {\n", data->name, data->name);
        SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, j) {
            if (n->meta.len <= 0) {
                printf("        [%d] = {%s, \"%.*s\", NULL},\n",
                    j, data->descriptions[j]->symbol, (int)n->name.len, n->name.data);
            } else {
                printf("        [%d] = {%s, \"%.*s\", (struct sol_flow_node_options *) &opts%s%d},\n",
                    j, data->descriptions[j]->symbol, (int)n->name.len, n->name.data, data->name, j);
            }
        }
        printf("        SOL_FLOW_STATIC_NODE_SPEC_GUARD\n"
            "    };\n\n");
    }

    SOL_VECTOR_FOREACH_REVERSE_IDX (fbp_data_vector, data, i) {
        for (j = 0; j < data->graph.nodes.len; j++) {
            if (streqn(data->descriptions[j]->symbol, "NULL", 4)) {
                size_t type_len = strlen(data->descriptions[j]->symbol) - symbol_placeholder_prefix_size - symbol_placeholder_suffix_size;
                printf("    nodes%s[%d].type = type%.*s;\n\n", data->name, j, (int)type_len,
                    data->descriptions[j]->symbol + symbol_placeholder_prefix_size);
            }
        }

        printf("    type%s = sol_flow_static_new_type(nodes%s, conns%s, %s%s, %s%s, NULL);\n",
            data->name, data->name, data->name,
            data->graph.exported_in_ports.len > 0 ? "exported_in" : "NULL",
            data->graph.exported_in_ports.len > 0 ? data->name : "",
            data->graph.exported_out_ports.len > 0 ? "exported_out" : "NULL",
            data->graph.exported_out_ports.len > 0 ? data->name : "");
        printf("    if (!type%s)\n"
            "        return;\n\n", data->name);
    }

    printf("    flow = sol_flow_node_new(NULL, NULL, type, NULL);\n"
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
    char *filename;
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

    filename = argv[optind];

    args.fbp_basename = strdup(basename(strdupa(filename)));
    if (!args.fbp_basename) {
        SOL_ERR("Couldn't get %s basename.", filename);
        return false;
    }

    args.fbp_dirname = strdup(dirname(strdupa(filename)));
    if (!args.fbp_dirname) {
        free(args.fbp_basename);
        SOL_ERR("Couldn't get %s dirname args.", filename);
        return false;
    }

    return true;
}

static bool
add_fbp_type_to_type_store(struct type_store *store, struct fbp_data *data)
{
    struct port_description *p, *port;
    struct sol_fbp_exported_port *e;
    struct type_description type;
    bool ret = false;
    char type_symbol[2048];
    int r;
    uint16_t i, j;

    type.name = data->name;

    r = snprintf(type_symbol, sizeof(type_symbol), "%s%s%s",
        SYMBOL_PLACEHOLDER_PREFIX, data->name, SYMBOL_PLACEHOLDER_SUFFIX);
    if (r < 0 || r >= (int)sizeof(type_symbol))
        return false;

    type.symbol = type_symbol;

    /* useless for fbp type */
    type.options_symbol = type_symbol;

    sol_vector_init(&type.in_ports, sizeof(struct port_description));
    SOL_VECTOR_FOREACH_IDX (&data->graph.exported_in_ports, e, i) {
        p = sol_vector_append(&type.in_ports);
        SOL_NULL_CHECK_GOTO(p, fail_in_ports);

        p->name = strndupa(e->exported_name.data, e->exported_name.len);

        SOL_VECTOR_FOREACH_IDX (&data->descriptions[e->node]->in_ports, port, j) {
            if (streqn(e->port.data, port->name, e->port.len))
                p->data_type = strdupa(port->data_type);
        }
        SOL_NULL_CHECK_GOTO(p->data_type, fail_in_ports);
    }

    sol_vector_init(&type.out_ports, sizeof(struct port_description));
    SOL_VECTOR_FOREACH_IDX (&data->graph.exported_out_ports, e, i) {
        p = sol_vector_append(&type.out_ports);
        SOL_NULL_CHECK_GOTO(p, fail_out_ports);

        p->name = strndupa(e->exported_name.data, e->exported_name.len);

        SOL_VECTOR_FOREACH_IDX (&data->descriptions[e->node]->out_ports, port, j) {
            if (streqn(e->port.data, port->name, e->port.len))
                p->data_type = strdupa(port->data_type);
        }
        SOL_NULL_CHECK_GOTO(p->data_type, fail_out_ports);
    }

    /* useless for fbp type */
    sol_vector_init(&type.options, sizeof(struct option_description));

    ret = type_store_add_type(store, &type);

    sol_vector_clear(&type.options);
fail_out_ports:
    sol_vector_clear(&type.out_ports);
fail_in_ports:
    sol_vector_clear(&type.in_ports);

    return ret;
}

static bool
resolve_node(struct fbp_data *data, struct type_store *store)
{
    struct sol_fbp_node *n;
    uint16_t i;

    data->descriptions = calloc(data->graph.nodes.len, sizeof(void *));
    if (!data->descriptions)
        return false;

    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, i) {
        data->descriptions[i] = sol_fbp_generator_resolve_type(store, n, data->filename);
        if (!data->descriptions[i])
            return false;
    }

    return true;
}

static struct fbp_data *
create_fbp_data(struct sol_vector *fbp_data_vector, struct sol_ptr_vector *file_readers, struct type_store *store, const char *name, const char *fbp_basename)
{
    struct fbp_data *data;
    struct sol_fbp_error *fbp_error;
    struct sol_file_reader *fr;
    char filename[2048];
    int r;
    uint16_t data_idx;

    r = snprintf(filename, sizeof(filename), "%s/%s", args.fbp_dirname, fbp_basename);
    if (r < 0 || r >= (int)sizeof(filename)) {
        SOL_ERR("Couldn't find file '%s': %s\n", filename, sol_util_strerrora(errno));
        return NULL;
    }

    fr = sol_file_reader_open(filename);
    if (!fr) {
        SOL_ERR("Couldn't open file '%s': %s\n", filename, sol_util_strerrora(errno));
        return NULL;
    }

    if (sol_ptr_vector_append(file_readers, fr) < 0) {
        SOL_ERR("Couldn't handle file '%s': %s\n", filename, sol_util_strerrora(errno));
        sol_file_reader_close(fr);
        return NULL;
    }

    data = sol_vector_append(fbp_data_vector);
    if (!data) {
        SOL_ERR("Couldn't create fbp data.");
        return NULL;
    }

    if (sol_fbp_graph_init(&data->graph) != 0) {
        SOL_ERR("Couldn't initialize graph.");
        return NULL;
    }

    fbp_error = sol_fbp_parse(sol_file_reader_get_all(fr), &data->graph);
    if (fbp_error) {
        sol_fbp_log_print(filename, fbp_error->position.line, fbp_error->position.column, fbp_error->msg);
        sol_fbp_error_free(fbp_error);
        return NULL;
    }

    data->name = strdup(name);
    if (!data->name) {
        SOL_ERR("Couldn't create fbp data.");
        return NULL;
    }

    data->filename = strdup(filename);
    if (!data->filename) {
        SOL_ERR("Couldn't create fbp data.");
        return NULL;
    }

    /* Get data index in order to use it after we handle the declarations. */
    data_idx = fbp_data_vector->len - 1;

    if (data->graph.declarations.len > 0) {
        struct fbp_data *d;
        struct sol_fbp_declaration *dec;
        char *dec_file, *dec_name;
        uint16_t i;

        SOL_VECTOR_FOREACH_IDX (&data->graph.declarations, dec, i) {
            if (!sol_str_slice_str_eq(dec->kind, "fbp"))
                continue;

            dec_file = strndupa(dec->contents.data, dec->contents.len);
            dec_name = strndupa(dec->name.data, dec->name.len);

            d = create_fbp_data(fbp_data_vector, file_readers, store, dec_name, dec_file);
            if (!d)
                return NULL;

            if (!add_fbp_type_to_type_store(store, d)) {
                SOL_ERR("Couldn't add fbp type to type store");
                return NULL;
            }

        }
    }

    /* We need to do this because we may have appended new data to fbp_data_vector,
     * this changes the position of data pointers since it's a sol_vector. */
    data = sol_vector_get(fbp_data_vector, data_idx);

    if (!resolve_node(data, store)) {
        SOL_ERR("Failed to resolve node type.");
        return NULL;
    }

    return data;
}

int
main(int argc, char *argv[])
{
    struct fbp_data *data;
    struct sol_file_reader *fr;
    struct sol_ptr_vector file_readers;
    struct sol_vector fbp_data_vector;
    struct type_store *store;
    uint16_t i;
    uint8_t result = EXIT_FAILURE;

    if (sol_init() < 0)
        goto end;

    if (!sol_fbp_generator_handle_args(argc, argv))
        goto fail_args;

    if (args.conf_file && access(args.conf_file, R_OK) == -1) {
        SOL_ERR("Couldn't open file '%s': %s", args.conf_file, sol_util_strerrora(errno));
        goto fail_access;
    }

    store = type_store_new();
    if (!store)
        goto fail_store;
    if (!sol_fbp_generator_type_store_load(store))
        goto fail_store_load;

    sol_vector_init(&fbp_data_vector, sizeof(struct fbp_data));

    sol_ptr_vector_init(&file_readers);

    if (!create_fbp_data(&fbp_data_vector, &file_readers, store, "", args.fbp_basename))
        goto fail_graph;

    str_arena = sol_arena_new();
    if (!str_arena) {
        SOL_ERR("Couldn't create str arena");
        goto fail_arena;
    }

    result = generate(&fbp_data_vector);

    sol_arena_del(str_arena);

fail_arena:
fail_graph:
    SOL_VECTOR_FOREACH_IDX (&fbp_data_vector, data, i) {
        free(data->descriptions);
        sol_fbp_graph_fini(&data->graph);
        free(data->name);
        free(data->filename);
    }
    sol_vector_clear(&fbp_data_vector);
    SOL_PTR_VECTOR_FOREACH_IDX (&file_readers, fr, i)
        sol_file_reader_close(fr);
    sol_ptr_vector_clear(&file_readers);
fail_store_load:
    type_store_del(store);
fail_store:
fail_access:
    free(args.fbp_basename);
    free(args.fbp_dirname);
fail_args:
    sol_shutdown();
end:
    return result;
}
