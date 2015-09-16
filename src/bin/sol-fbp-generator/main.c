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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sol-arena.h"
#include "sol-buffer.h"
#include "sol-conffile.h"
#include "sol-fbp-internal-log.h"
#include "sol-fbp.h"
#include "sol-file-reader.h"
#include "sol-flow-static.h"
#include "sol-mainloop.h"
#include "sol-missing.h"
#include "sol-str-slice.h"
#include "sol-util.h"

#include "type-store.h"

#ifdef USE_MEMMAP
#include "sol-memmap-storage.h"
#endif

static struct {
    const char *conf_file;
    const char *output_file;
    const char *export_symbol;

    struct sol_ptr_vector json_files;
    struct sol_ptr_vector fbp_search_paths;
    char *fbp_basename;
} args;

static struct sol_arena *str_arena;
static struct sol_buffer output_buffer;

/* In order to ensure that each generated fbp type has an unique id. */
static unsigned int fbp_id_count;

struct fbp_data {
    struct type_store *store;
    char *filename;
    char *name;
    struct sol_fbp_graph graph;
    struct sol_vector declared_fbp_types;
    int id;
};

struct declared_fbp_type {
    char *name;
    int id;
};

static struct port_description error_port = {
    .name = (char *)SOL_FLOW_NODE_PORT_ERROR_NAME,
    .data_type = (char *)"error",
    .base_port_idx = SOL_FLOW_NODE_PORT_ERROR,
};

SOL_ATTR_PRINTF(1, 2) static void
out(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    sol_buffer_append_vprintf(&output_buffer, fmt, ap);
    va_end(ap);
}

static struct sol_fbp_node *
get_node(const struct fbp_data *data, uint16_t i)
{
    return sol_vector_get(&data->graph.nodes, i);
}

static struct type_description *
get_node_type_description(const struct fbp_data *data, uint16_t i)
{
    struct sol_fbp_node *n = sol_vector_get(&data->graph.nodes, i);

    assert(n);
    return n->user_data;
}

static void
handle_suboptions(const struct sol_fbp_meta *meta,
    void (*handle_func)(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file), const char *fbp_file)
{
    uint16_t i = 0;
    char *p, *remaining;

    remaining = strndupa(meta->value.data, meta->value.len);
    SOL_NULL_CHECK(remaining);

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
    out("            .%.*s.%s,\n", SOL_STR_SLICE_PRINT(meta->key), option);
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
    const char *irange_drange_fields[] = { "val", "min", "max", "step", NULL };

    if (check_suboption(option, meta, fbp_file))
        out("            .%.*s.%s = %s,\n", SOL_STR_SLICE_PRINT(meta->key),
            irange_drange_fields[index], option);
}

static void
handle_rgb_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file)
{
    const char *rgb_fields[] = { "red", "green", "blue",
                                 "red_max", "green_max", "blue_max", NULL };

    if (check_suboption(option, meta, fbp_file))
        out("            .%.*s.%s = %s,\n", SOL_STR_SLICE_PRINT(meta->key),
            rgb_fields[index], option);
}

static void
handle_direction_vector_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *fbp_file)
{
    const char *direction_vector_fields[] = { "x", "y", "z",
                                              "min", "max", NULL };

    if (check_suboption(option, meta, fbp_file))
        out("            .%.*s.%s = %s,\n", SOL_STR_SLICE_PRINT(meta->key),
            direction_vector_fields[index], option);
}

static bool
handle_option(const struct sol_fbp_meta *meta, struct sol_vector *options, const char *fbp_file)
{
    struct option_description *o;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (options, o, i) {
        struct sol_fbp_meta unquoted_meta;

        if (!sol_str_slice_str_eq(meta->key, o->name))
            continue;

        /* Option values from the conffile other than strings might
        * have quotes. E.g. 0|3 is currently represented as a string
        * "0|3" in JSON. When reading we don't have the type
        * information, but at this point we do, so unquote them. */
        if (!streq(o->data_type, "string") && meta->value.len > 1
            && meta->value.data[0] == '"' && meta->value.data[meta->value.len - 1] == '"') {
            unquoted_meta = *meta;
            unquoted_meta.value.data += 1;
            unquoted_meta.value.len -= 2;
            meta = &unquoted_meta;
        }

        if (streq(o->data_type, "int") || streq(o->data_type, "float")) {
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
                out("            .%.*s = %.*s,\n", SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(meta->value));
            else
                out("            .%.*s = \"%.*s\",\n", SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(meta->value));

        } else {
            out("            .%.*s = %.*s,\n", SOL_STR_SLICE_PRINT(meta->key), SOL_STR_SLICE_PRINT(meta->value));
        }

        return true;
    }

    sol_fbp_log_print(fbp_file, meta->position.line, meta->position.column,
        "Invalid option key '%.*s'", SOL_STR_SLICE_PRINT(meta->key));
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
    const char **opts_as_string = NULL;
    const char *const *opt;

    if (sol_conffile_resolve_path(id, &type_name, &opts_as_string, args.conf_file) < 0) {
        sol_fbp_log_print(fbp_file, n->position.line, n->position.column, "Couldn't resolve type id '%s'", id);
        return NULL;
    }

    if (!opts_as_string)
        return type_name;

    /* Conffile may contain options for this node type */
    for (opt = opts_as_string; *opt != NULL; opt++)
        handle_conffile_option(n, *opt, fbp_file);

    return type_name;
}

static struct type_description *
sol_fbp_generator_resolve_type(struct type_store *common_store, struct type_store *parent_store, struct sol_fbp_node *n, const char *fbp_file)
{
    const char *type_name_as_string;
    const char *type_name;
    struct type_description *desc;

    type_name_as_string = strndupa(n->component.data, n->component.len);

    desc = type_store_find(common_store, type_name_as_string);
    if (desc)
        return desc;

    desc = type_store_find(parent_store, type_name_as_string);
    if (desc)
        return desc;

    type_name = sol_fbp_generator_resolve_id(n, type_name_as_string, fbp_file);
    if (!type_name)
        return NULL;

    desc = type_store_find(common_store, type_name);
    if (desc)
        return desc;

    return type_store_find(parent_store, type_name);
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
            *port_number = p->base_port_idx;
            return p;
        }
    }

    if (sol_str_slice_str_eq(*name, SOL_FLOW_NODE_PORT_ERROR_NAME)) {
        *port_number = error_port.base_port_idx;
        return &error_port;
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
            SOL_STR_SLICE_PRINT(*name), SOL_STR_SLICE_PRINT(*component));

        break;
    }

    return false;
}

static bool
handle_port_index_error(struct sol_fbp_position *p, struct port_description *port_desc,
    struct sol_str_slice *component, int port_idx, const char *fbp_file)
{
    if (port_idx == -1) {
        sol_fbp_log_print(fbp_file, p->line, p->column,
            "Port '%s' from node type '%.*s' is an array port and no index was given'",
            port_desc->name, SOL_STR_SLICE_PRINT(*component));
    } else {
        sol_fbp_log_print(fbp_file, p->line, p->column,
            "Port '%s' from node type '%.*s' has size '%d', but given index '%d' is out of bounds",
            port_desc->name, SOL_STR_SLICE_PRINT(*component), port_desc->array_size, port_idx);
    }
    return false;
}

static bool
generate_options(const struct fbp_data *data)
{
    struct sol_fbp_meta *m;
    struct sol_fbp_node *n;
    uint16_t i, j;

    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, i) {
        struct type_description *desc = n->user_data;

        if (n->meta.len <= 0)
            continue;

        out("    static const struct %s opts%d =\n", desc->options_symbol, i);
        out("        %s_OPTIONS_DEFAULTS(\n", desc->symbol);
        SOL_VECTOR_FOREACH_IDX (&n->meta, m, j) {
            if (!handle_option(m, &desc->options, data->filename))
                return EXIT_FAILURE;
        }
        out("        );\n\n");
    }

    return true;
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

        /* The graph is expected to be valid. */
        assert(conn->src < data->graph.nodes.len);
        assert(conn->dst < data->graph.nodes.len);

        spec->src = conn->src;
        spec->dst = conn->dst;
        spec->src_port = UINT16_MAX;
        spec->dst_port = UINT16_MAX;

        src_desc = get_node_type_description(data, conn->src);
        dst_desc = get_node_type_description(data, conn->dst);

        src_port_desc = check_port_existence(&src_desc->out_ports, &conn->src_port, &spec->src_port);
        if (!src_port_desc) {
            struct sol_fbp_node *n = get_node(data, conn->src);
            free(conn_specs);
            return handle_port_error(&n->out_ports, &conn->src_port, &n->component, data->filename);
        }

        if (src_port_desc->array_size > 0) {
            if (conn->src_port_idx == -1 || conn->src_port_idx >= src_port_desc->array_size) {
                struct sol_fbp_node *n = get_node(data, conn->src);
                free(conn_specs);
                return handle_port_index_error(&conn->position, src_port_desc, &n->component, conn->src_port_idx, data->filename);
            }
            spec->src_port += conn->src_port_idx;
        }

        dst_port_desc = check_port_existence(&dst_desc->in_ports, &conn->dst_port, &spec->dst_port);
        if (!dst_port_desc) {
            struct sol_fbp_node *n = get_node(data, conn->dst);
            free(conn_specs);
            return handle_port_error(&n->in_ports, &conn->dst_port, &n->component, data->filename);
        }

        if (dst_port_desc->array_size > 0) {
            if (conn->dst_port_idx == -1 || conn->dst_port_idx >= dst_port_desc->array_size) {
                struct sol_fbp_node *n = get_node(data, conn->dst);
                free(conn_specs);
                return handle_port_index_error(&conn->position, dst_port_desc, &n->component, conn->dst_port_idx, data->filename);
            }
            spec->dst_port += conn->dst_port_idx;
        }

        if (!port_types_compatible(src_port_desc->data_type, dst_port_desc->data_type)) {
            sol_fbp_log_print(data->filename,
                conn->position.line, conn->position.column,
                "Couldn't connect '%s %.*s -> %.*s %s'. Source port "
                "type '%s' doesn't match destination port type '%s'",
                src_desc->name, SOL_STR_SLICE_PRINT(conn->src_port),
                SOL_STR_SLICE_PRINT(conn->dst_port), dst_desc->name,
                src_port_desc->data_type, dst_port_desc->data_type);
            free(conn_specs);
            return false;
        }
    }

    qsort(conn_specs, data->graph.conns.len, sizeof(struct sol_flow_static_conn_spec),
        compare_conn_specs);

    out("    static const struct sol_flow_static_conn_spec conns[] = {\n");
    for (i = 0; i < data->graph.conns.len; i++) {
        struct sol_flow_static_conn_spec *spec = &conn_specs[i];
        out("        { %d, %d, %d, %d },\n",
            spec->src, spec->src_port, spec->dst, spec->dst_port);
    }
    out("        SOL_FLOW_STATIC_CONN_SPEC_GUARD\n"
        "    };\n\n");

    free(conn_specs);
    return true;
}

static bool
generate_exported_port(const char *node, struct sol_vector *ports, struct sol_fbp_exported_port *e, const char *fbp_file)
{
    struct port_description *p;
    uint16_t base;

    p = check_port_existence(ports, &e->port, &base);
    if (!p) {
        sol_fbp_log_print(fbp_file, e->position.line, e->position.column,
            "Couldn't export '%.*s'. Port '%.*s' doesn't exist in node '%s'",
            SOL_STR_SLICE_PRINT(e->exported_name), SOL_STR_SLICE_PRINT(e->port), node);
        return false;
    }
    if (e->port_idx == -1) {
        uint16_t last = base + (p->array_size ? : 1);
        for (; base < last; base++) {
            out("        { %d, %d },\n", e->node, base);
        }
    } else {
        if (e->port_idx >= p->array_size) {
            sol_fbp_log_print(fbp_file, e->position.line, e->position.column,
                "Couldn't export '%.*s'. Index '%d' is out of range (port size: %d).",
                SOL_STR_SLICE_PRINT(e->exported_name), e->port_idx, p->array_size);
            return false;
        }
        out("        { %d, %d },\n", e->node, base + e->port_idx);
    }

    return true;
}

static bool
generate_exports(const struct fbp_data *data)
{
    struct sol_fbp_exported_port *e;
    struct type_description *n;
    uint16_t i;

    if (data->graph.exported_in_ports.len > 0) {
        out("    static const struct sol_flow_static_port_spec exported_in[] = {\n");
        SOL_VECTOR_FOREACH_IDX (&data->graph.exported_in_ports, e, i) {
            assert(e->node < data->graph.nodes.len);
            n = get_node_type_description(data, e->node);
            if (!generate_exported_port(n->name, &n->in_ports, e, data->filename))
                return false;
        }
        out("        SOL_FLOW_STATIC_PORT_SPEC_GUARD\n"
            "    };\n\n");
    }

    if (data->graph.exported_out_ports.len > 0) {
        out("    static const struct sol_flow_static_port_spec exported_out[] = {\n");
        SOL_VECTOR_FOREACH_IDX (&data->graph.exported_out_ports, e, i) {
            assert(e->node < data->graph.nodes.len);
            n = get_node_type_description(data, e->node);
            if (!generate_exported_port(n->name, &n->out_ports, e, data->filename))
                return false;
        }
        out("        SOL_FLOW_STATIC_PORT_SPEC_GUARD\n"
            "    };\n\n");
    }

    return true;
}

static void
generate_node_specs(const struct fbp_data *data)
{
    struct declared_fbp_type *dec_type;
    struct sol_fbp_node *n;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->declared_fbp_types, dec_type, i) {
        out("    const struct sol_flow_node_type *type_%s = create_%d_%s_type();\n",
            dec_type->name, dec_type->id, dec_type->name);
    }

    /* We had to create this node spec as static in order to keep it alive,
     * since sol_flow_static_new_type() doesn't copy the informations.
     * Also, we had to set NULL here and set the real value after because
     * the types are not constant values. */
    out("\n    static struct sol_flow_static_node_spec nodes[] = {\n");
    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, i) {
        if (n->meta.len <= 0) {
            out("        [%d] = {NULL, \"%.*s\", NULL},\n", i, SOL_STR_SLICE_PRINT(n->name));
        } else {
            out("        [%d] = {NULL, \"%.*s\", (struct sol_flow_node_options *) &opts%d},\n",
                i, SOL_STR_SLICE_PRINT(n->name), i);
        }
    }
    out("        SOL_FLOW_STATIC_NODE_SPEC_GUARD\n"
        "    };\n");
}

static void
generate_node_type_assignments(const struct fbp_data *data)
{
    struct declared_fbp_type *dec_type;
    struct sol_fbp_node *n;
    uint16_t i;

    out("\n");

    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, i) {
        struct type_description *desc = n->user_data;
        out("    nodes[%d].type = %s;\n", i, desc->symbol);
    }

    SOL_VECTOR_FOREACH_IDX (&data->declared_fbp_types, dec_type, i) {
        out("\n    if (!type_%s)\n"
            "        return NULL;\n",
            dec_type->name);
    }
}

static bool
generate_create_type_function(struct fbp_data *data)
{
    out("\nstatic const struct sol_flow_node_type *\n"
        "create_%d_%s_type(void)\n"
        "{\n",
        data->id,
        data->name);

    if (!generate_options(data) || !generate_connections(data) || !generate_exports(data))
        return false;

    generate_node_specs(data);

    out("\n"
        "    struct sol_flow_static_spec spec = {\n"
        "        .api_version = SOL_FLOW_STATIC_API_VERSION,\n"
        "        .nodes = nodes,\n"
        "        .conns = conns,\n"
        "        .exported_in = %s,\n"
        "        .exported_out = %s,\n"
        "    };\n",
        data->graph.exported_in_ports.len > 0 ? "exported_in" : "NULL",
        data->graph.exported_out_ports.len > 0 ? "exported_out" : "NULL");

    generate_node_type_assignments(data);

    out("\n"
        "    return sol_flow_static_new_type(&spec);\n"
        "}\n\n");

    return true;
}

struct generate_context {
    struct sol_vector modules;
    struct sol_vector types_to_initialize;
};

static bool
is_declared_type(struct fbp_data *data, const struct sol_str_slice name)
{
    struct declared_fbp_type *dec_type;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->declared_fbp_types, dec_type, i) {
        if (sol_str_slice_str_eq(name, dec_type->name))
            return true;
    }
    return false;
}

static bool
contains_slice(const struct sol_vector *v, const struct sol_str_slice name)
{
    struct sol_str_slice *slice;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (v, slice, i) {
        if (sol_str_slice_eq(*slice, name))
            return true;
    }
    return false;
}

static bool
collect_context_info(struct generate_context *ctx, struct fbp_data *data)
{
    struct sol_fbp_node *node;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, node, i) {
        struct type_description *desc;
        const char *sep;
        struct sol_str_slice name, module, symbol;

        /* Need to go via descriptions to get the real resolved name,
         * after conffile pass. */
        desc = node->user_data;
        name = sol_str_slice_from_str(desc->name);

        /* Ignore since these are completely defined in the generated code. */
        if (is_declared_type(data, name)) {
            continue;
        }

        symbol = sol_str_slice_from_str(desc->symbol);
        if (!contains_slice(&ctx->types_to_initialize, symbol)) {
            struct sol_str_slice *t;
            t = sol_vector_append(&ctx->types_to_initialize);
            if (!t)
                return false;
            *t = symbol;
        }

        module = name;
        sep = strstr(name.data, "/");
        if (sep) {
            module.len = sep - module.data;
        }

        if (!contains_slice(&ctx->modules, module)) {
            struct sol_str_slice *m;
            m = sol_vector_append(&ctx->modules);
            if (!m)
                return false;
            *m = module;
        }
    }

    return true;
}

#ifdef USE_MEMMAP
static void
generate_memory_map_struct(const struct sol_ptr_vector *maps, int *elements)
{
    const struct sol_memmap_map *map;
    const struct sol_str_table_ptr *iter;
    const struct sol_memmap_entry *entry;
    int i;

    *elements = 0;

    SOL_PTR_VECTOR_FOREACH_IDX (maps, map, i) {
        out("\nstatic const struct sol_memmap_map _memmap%d = {\n", i);
        out("   .version = %d,\n"
            "   .path = \"%s\"\n"
            "   .entries {\n",
            map->version, map->path);

        for (iter = map->entries; iter->key; iter++) {
            entry = iter->val;
            out("       SOL_MEMMAP_ENTRY_BIT_SIZE(\"%s\", %lu, %lu, %u, %u),\n",
                iter->key, entry->offset, entry->size, entry->bit_offset,
                entry->bit_size);
        }

        out("    }\n"
            "};\n");
    }

    *elements = i;
}
#endif

static int
generate(struct sol_vector *fbp_data_vector)
{
    struct generate_context _ctx = {
        .modules = SOL_VECTOR_INIT(struct sol_str_slice),
        .types_to_initialize = SOL_VECTOR_INIT(struct sol_str_slice),
    }, *ctx = &_ctx;

    struct fbp_data *data;
    struct sol_str_slice *module, *symbol;
    struct sol_ptr_vector *memory_maps;
    uint16_t i;
    int r, memmap_elems = 0;

    out(
        "#include \"sol-flow.h\"\n"
        "#include \"sol-flow-static.h\"\n");

    if (!args.export_symbol) {
        out("#include \"sol-mainloop.h\"\n");
    }

    if (sol_conffile_resolve_memmap_path(&memory_maps, args.conf_file) < 0) {
        SOL_ERR("Couldn't resolve memory mappings on file [%s]", args.conf_file);
        return EXIT_FAILURE;
    }

    out("\n");

    SOL_VECTOR_FOREACH_IDX (fbp_data_vector, data, i) {
        if (!collect_context_info(ctx, data))
            return EXIT_FAILURE;
    }

    /* Header name is currently inferred from the module name. */
    SOL_VECTOR_FOREACH_IDX (&ctx->modules, module, i) {
        out("#include \"sol-flow/%.*s.h\"\n", SOL_STR_SLICE_PRINT(*module));
    }

#ifdef USE_MEMMAP
    if (memory_maps) {
        out("#include \"sol-memmap-storage.h\"\n");
        generate_memory_map_struct(memory_maps, &memmap_elems);
    }
#endif

    /* Reverse since the dependencies appear later in the vector. */
    SOL_VECTOR_FOREACH_REVERSE_IDX (fbp_data_vector, data, i) {
        if (!generate_create_type_function(data)) {
            SOL_ERR("Couldn't generate %s type function", data->name);
            r = EXIT_FAILURE;
            goto end;
        }
    }

    out(
        "static void\n"
        "initialize_types(void)\n"
        "{\n");
    SOL_VECTOR_FOREACH_IDX (&ctx->types_to_initialize, symbol, i) {
        out(
            "    if (%.*s->init_type)\n"
            "        %.*s->init_type();\n",
            SOL_STR_SLICE_PRINT(*symbol),
            SOL_STR_SLICE_PRINT(*symbol));
    }
    if (memmap_elems) {
        out("\n");
        for (i = 0; i < memmap_elems; i++)
            out("   sol_memmap_add_map(&_memmap%d);\n", i);
    }
    out(
        "}\n\n");

    if (!args.export_symbol) {
        out(
            "static struct sol_flow_node *flow;\n"
            "\n"
            "static void\n"
            "startup(void)\n"
            "{\n"
            "    const struct sol_flow_node_type *type;\n\n"
            "    initialize_types();\n"
            "    type = create_0_root_type();\n"
            "    if (!type)\n"
            "        return;\n\n"
            "    flow = sol_flow_node_new(NULL, NULL, type, NULL);\n"
            "}\n\n"
            "static void\n"
            "shutdown(void)\n"
            "{\n"
            "    sol_flow_node_del(flow);\n"
            "}\n\n"
            "SOL_MAIN_DEFAULT(startup, shutdown);\n");
    } else {
        out(
            "const struct sol_flow_node_type *\n"
            "%s(void) {\n"
            "    static const struct sol_flow_node_type *type = NULL;\n"
            "    if (!type) {\n"
            "        initialize_types();\n"
            "        type = create_0_root_type();\n"
            "    }\n"
            "\n"
            "    return type;\n"
            "}\n",
            args.export_symbol);
    }

    r = EXIT_SUCCESS;

end:
    sol_vector_clear(&ctx->modules);
    sol_vector_clear(&ctx->types_to_initialize);
    return r;
}

static bool
sol_fbp_generator_type_store_load_file(struct type_store *common_store, const char *json_file)
{
    struct sol_file_reader *fr = NULL;

    fr = sol_file_reader_open(json_file);
    if (!fr) {
        SOL_ERR("Couldn't open json file '%s': %s\n", json_file, sol_util_strerrora(errno));
        return false;
    }

    if (!type_store_read_from_json(common_store, sol_file_reader_get_all(fr))) {
        SOL_ERR("Couldn't read from json file '%s', please check its format.", json_file);
        sol_file_reader_close(fr);
        return false;
    }

    sol_file_reader_close(fr);

    return true;
}

static bool
sol_fbp_generator_type_store_load(struct type_store *common_store)
{
    const char *file;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&args.json_files, file, i) {
        if (!sol_fbp_generator_type_store_load_file(common_store, file))
            return false;
    }

    return true;
}

static bool handle_json_path(const char *path);

static bool
handle_json_dir(const char *path)
{
    DIR *dp;
    struct dirent *entry;
    char full_path[PATH_MAX];
    int r;

    dp = opendir(path);
    if (!dp)
        return false;

    while ((entry = readdir(dp))) {
        if (entry->d_name[0] == '.')
            continue;

        r = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (r < 0 || r >= (int)sizeof(full_path)) {
            closedir(dp);
            return false;
        }

        if (!handle_json_path(full_path)) {
            closedir(dp);
            return false;
        }
    }

    closedir(dp);

    return true;
}

static const char *
get_file_ext(const char *file)
{
    char *ext = strrchr(file, '.');

    if (!ext)
        ext = (char *)"";

    return ext;
}

static bool
handle_json_path(const char *path)
{
    struct stat s;
    char *dup_path;

    if (stat(path, &s) != 0)
        return false;

    if (S_ISDIR(s.st_mode))
        return handle_json_dir(path);

    if (S_ISREG(s.st_mode) && streq(get_file_ext(path), ".json")) {
        dup_path = sol_arena_strdup(str_arena, path);
        SOL_NULL_CHECK(dup_path, false);

        if (sol_ptr_vector_append(&args.json_files, dup_path) < 0) {
            SOL_ERR("Couldn't handle json path '%s': %s\n", path, sol_util_strerrora(errno));
            return false;
        }
    }

    return true;
}

static bool
handle_include_path(char *path)
{
    struct stat s;

    if (stat(path, &s) != 0 || !S_ISDIR(s.st_mode))
        return false;

    if (sol_ptr_vector_append(&args.fbp_search_paths, path) < 0) {
        SOL_ERR("Couldn't handle FBP search path '%s': %s\n", path, sol_util_strerrora(errno));
        return false;
    }

    return true;
}

static bool
search_fbp_file(char *fullpath, const char *basename)
{
    struct stat s;
    const char *p;
    uint16_t i;
    int err;

    SOL_PTR_VECTOR_FOREACH_IDX (&args.fbp_search_paths, p, i) {
        err = snprintf(fullpath, PATH_MAX, "%s/%s", p, basename);
        if (err < 0 || err >= PATH_MAX)
            return false;

        if (stat(fullpath, &s) == 0 && S_ISREG(s.st_mode))
            return true;
    }

    return false;
}

static void
print_usage(const char *program)
{
    fprintf(stderr, "usage: %s [-c CONF] [-j DESC -j DESC...] [-s SYMBOL] INPUT OUTPUT\n"
        "Generates C code from INPUT into the OUTPUT file.\n\n"
        "Options:\n"
        "    -c  Uses the CONF .json file for resolving unknown types.\n"
        "    -j  When resolving types, use the passed DESC files. If DESC is\n"
        "        a directory then all the .json files in the directory will be used.\n"
        "        Multiple -j can be passed.\n"
        "    -s  Define a function named SYMBOL that will return the type from FBP\n"
        "        and don't generate any main function or entry point.\n"
        "    -I  Define search path for FBP files\n"
        "\n",
        program);
}

static bool
parse_args(int argc, char *argv[])
{
    char *filename, *dup_path;
    bool has_json_file = false;
    int opt;

    if (argc < 3) {
        print_usage(argv[0]);
        return false;
    }

    sol_ptr_vector_init(&args.json_files);
    sol_ptr_vector_init(&args.fbp_search_paths);

    while ((opt = getopt(argc, argv, "s:c:j:I:")) != -1) {
        switch (opt) {
        case 's':
            args.export_symbol = optarg;
            break;
        case 'c':
            if (access(optarg, R_OK) == -1) {
                SOL_ERR("Can't access conf file '%s': %s",
                    optarg, sol_util_strerrora(errno));
                return false;
            }
            args.conf_file = optarg;
            break;
        case 'j':
            has_json_file = true;
            if (!handle_json_path(optarg)) {
                SOL_ERR("Can't access JSON description path '%s': %s",
                    optarg, sol_util_strerrora(errno));
                return false;
            }
            break;
        case 'I':
            if (!handle_include_path(optarg)) {
                SOL_ERR("Can't access include path '%s': %s",
                    optarg, sol_util_strerrora(errno));
                return false;
            }
            break;
        case '?':
            print_usage(argv[0]);
            return false;
        }
    }

    if (optind != argc - 2) {
        fprintf(stderr, "A single FBP input file and output file is required."
            " e.g. './sol-fbp-generator -j builtins.json simple.fbp simple-fbp.c'\n");
        return false;
    }

    if (!has_json_file) {
        fprintf(stderr, "At least one JSON file containing the declaration of the nodes"
            " (module) used in the FBP is required.\n");
        return false;
    }

    filename = argv[optind];
    args.output_file = argv[optind + 1];

    args.fbp_basename = sol_arena_strdup(str_arena, basename(strdupa(filename)));
    if (!args.fbp_basename) {
        SOL_ERR("Couldn't get %s basename.", filename);
        return false;
    }

    dup_path = sol_arena_strdup(str_arena, dirname(strdupa(filename)));
    if (!dup_path) {
        SOL_ERR("Couldn't get %s dirname.", filename);
        return false;
    }
    if (sol_ptr_vector_append(&args.fbp_search_paths, dup_path) < 0) {
        SOL_ERR("Couldn't handle include path '%s': %s\n", dup_path, sol_util_strerrora(errno));
        return false;
    }

    return true;
}

static bool
store_exported_ports(struct fbp_data *data, struct sol_vector *type_ports, struct sol_vector *exported_ports, bool desc_in_ports)
{
    struct port_description *p, *port;
    struct sol_fbp_exported_port *e;
    int base_port_idx = 0;
    uint16_t i, j;

    sol_vector_init(type_ports, sizeof(struct port_description));
    SOL_VECTOR_FOREACH_IDX (exported_ports, e, i) {
        struct type_description *desc = get_node_type_description(data,
            e->node);
        struct sol_vector *desc_ports;

        p = sol_vector_append(type_ports);
        SOL_NULL_CHECK(p, false);

        p->name = strndup(e->exported_name.data, e->exported_name.len);
        SOL_NULL_CHECK(p->name, false);

        if (desc_in_ports)
            desc_ports = &desc->in_ports;
        else
            desc_ports = &desc->out_ports;

        SOL_VECTOR_FOREACH_IDX (desc_ports, port, j) {
            if (sol_str_slice_str_eq(e->port, port->name)) {
                int array_size = 0;
                /* case the whole array was exported */
                if (e->port_idx == -1)
                    array_size = port->array_size;

                p->data_type = strdup(port->data_type);
                p->array_size = array_size;
                p->base_port_idx = base_port_idx++;
                if (array_size > 1)
                    base_port_idx += array_size - 1;
            }
        }
        SOL_NULL_CHECK(p->data_type, false);
    }

    return true;
}

static void
ports_clear(struct sol_vector *ports)
{
    struct port_description *port;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (ports, port, i) {
        free(port->name);
        free(port->data_type);
    }
    sol_vector_clear(ports);
}

static bool
add_fbp_type_to_type_store(struct type_store *parent_store, struct fbp_data *data)
{
    struct type_description type;
    int r;
    char node_type[2048];
    bool ret = false;

    type.name = data->name;

    r = snprintf(node_type, sizeof(node_type), "type_%s", data->name);
    if (r < 0 || r >= (int)sizeof(node_type))
        return false;

    type.symbol = node_type;

    /* useless for fbp type */
    type.options_symbol = (char *)"";

    if (!store_exported_ports(data, &type.in_ports,
        &data->graph.exported_in_ports, true))
        goto fail_in_ports;

    if (!store_exported_ports(data, &type.out_ports,
        &data->graph.exported_out_ports, false))
        goto fail_out_ports;

    /* useless for fbp type */
    sol_vector_init(&type.options, sizeof(struct option_description));

    ret = type_store_add_type(parent_store, &type);
    if (!ret)
        SOL_WRN("Failed to add type %s to store", type.name);
    else
        SOL_DBG("Type %s added to store", type.name);

    sol_vector_clear(&type.options);
fail_out_ports:
    ports_clear(&type.out_ports);
fail_in_ports:
    ports_clear(&type.in_ports);

    return ret;
}

static bool
resolve_node(struct fbp_data *data, struct type_store *common_store)
{
    struct sol_fbp_node *n;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, i) {
        n->user_data = sol_fbp_generator_resolve_type(common_store, data->store, n, data->filename);
        if (!n->user_data)
            return false;
        SOL_DBG("Node %.*s resolved", SOL_STR_SLICE_PRINT(n->name));
    }

    return true;
}

static struct fbp_data *
create_fbp_data(struct sol_vector *fbp_data_vector, struct sol_ptr_vector *file_readers,
    struct type_store *common_store, const char *name, const char *fbp_basename)
{
    struct fbp_data *data;
    struct sol_fbp_error *fbp_error;
    struct sol_file_reader *fr;
    char filename[PATH_MAX];

    if (!search_fbp_file(filename, fbp_basename)) {
        SOL_ERR("Couldn't find file '%s': %s\n", fbp_basename, sol_util_strerrora(errno));
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

    data->store = type_store_new();
    if (!data->store) {
        SOL_ERR("Couldn't create fbp type store.");
        return NULL;
    }

    sol_vector_init(&data->declared_fbp_types, sizeof(struct declared_fbp_type));

    data->name = sol_arena_strdup(str_arena, name);
    if (!data->name) {
        SOL_ERR("Couldn't create fbp data.");
        return NULL;
    }

    data->filename = sol_arena_strdup(str_arena, filename);
    if (!data->filename) {
        SOL_ERR("Couldn't create fbp data.");
        return NULL;
    }

    data->id = fbp_id_count++;
    SOL_DBG("Creating fbp data for %s (%s)", data->name, data->filename);

    if (data->graph.declarations.len > 0) {
        struct declared_fbp_type *dec_type;
        struct fbp_data *d;
        struct sol_fbp_declaration *dec;
        char *dec_file, *dec_name;
        uint16_t i, data_idx;

        /* Get data index in order to use it after we handle the declarations. */
        data_idx = fbp_data_vector->len - 1;

        SOL_VECTOR_FOREACH_IDX (&data->graph.declarations, dec, i) {
            if (!sol_str_slice_str_eq(dec->metatype, "fbp")) {
                SOL_ERR("DECLARE metatype '%.*s' not supported.", SOL_STR_SLICE_PRINT(dec->metatype));
                return NULL;
            }

            dec_file = strndupa(dec->contents.data, dec->contents.len);
            dec_name = strndupa(dec->name.data, dec->name.len);

            d = create_fbp_data(fbp_data_vector, file_readers, common_store, dec_name, dec_file);
            if (!d)
                return NULL;

            /* We need to do this because we may have appended new data to fbp_data_vector,
             * this changes the position of data pointers since it's a sol_vector. */
            data = sol_vector_get(fbp_data_vector, data_idx);

            if (!add_fbp_type_to_type_store(data->store, d)) {
                SOL_ERR("Couldn't create fbp data.");
                return NULL;
            }

            dec_type = sol_vector_append(&data->declared_fbp_types);
            if (!dec_type) {
                SOL_ERR("Couldn't create fbp data.");
                return NULL;
            }

            dec_type->name = d->name;
            dec_type->id = d->id;
        }
    }

    if (!resolve_node(data, common_store)) {
        SOL_ERR("Failed to resolve node type.");
        return NULL;
    }

    return data;
}

static int
write_file(const char *filename, const struct sol_buffer *buf)
{
    int fd;
    char *p = buf->data;
    ssize_t size = buf->used;
    int err = 0;

    fd = creat(filename, 0600);
    if (fd < 0)
        return -errno;

    while (size > 0) {
        ssize_t written = write(fd, p, size);
        if (written < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else {
                err = -errno;
                break;
            }
        }
        p += written;
        size -= written;
    }

    close(fd);
    return err;
}

int
main(int argc, char *argv[])
{
    struct fbp_data *data;
    struct sol_file_reader *fr;
    struct sol_ptr_vector file_readers;
    struct sol_vector fbp_data_vector;
    struct type_store *common_store;
    uint16_t i;
    uint8_t result = EXIT_FAILURE;
    int err;

    if (sol_init() < 0)
        goto end;

    sol_buffer_init(&output_buffer);

    str_arena = sol_arena_new();
    if (!str_arena) {
        SOL_ERR("Couldn't create str arena");
        goto fail_arena;
    }

    if (!parse_args(argc, argv))
        goto fail_args;

    common_store = type_store_new();
    if (!common_store)
        goto fail_store;
    if (!sol_fbp_generator_type_store_load(common_store))
        goto fail_store_load;

    sol_vector_init(&fbp_data_vector, sizeof(struct fbp_data));
    sol_ptr_vector_init(&file_readers);
    if (!create_fbp_data(&fbp_data_vector, &file_readers, common_store, "root", args.fbp_basename))
        goto fail_data;

    result = generate(&fbp_data_vector);
    if (result != EXIT_SUCCESS)
        goto fail_generate;

    err = write_file(args.output_file, &output_buffer);
    if (err < 0) {
        SOL_ERR("Couldn't write file '%s': %s",
            args.output_file, sol_util_strerrora(-err));
        goto fail_write;
    }

    result = EXIT_SUCCESS;

fail_write:
fail_generate:
fail_data:
    SOL_VECTOR_FOREACH_IDX (&fbp_data_vector, data, i) {
        if (data->store)
            type_store_del(data->store);
        sol_fbp_graph_fini(&data->graph);
        sol_vector_clear(&data->declared_fbp_types);
    }
    sol_vector_clear(&fbp_data_vector);
    SOL_PTR_VECTOR_FOREACH_IDX (&file_readers, fr, i)
        sol_file_reader_close(fr);
    sol_ptr_vector_clear(&file_readers);
fail_store_load:
    type_store_del(common_store);
fail_store:
    sol_ptr_vector_clear(&args.json_files);
    sol_ptr_vector_clear(&args.fbp_search_paths);
fail_args:
    sol_arena_del(str_arena);
fail_arena:
    sol_buffer_fini(&output_buffer);
    sol_shutdown();
end:
    return result;
}
