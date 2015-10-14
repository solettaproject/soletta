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
#include "sol-flow-metatype.h"

#include "type-store.h"

#ifdef USE_MEMMAP
#include "sol-memmap-storage.h"
#endif

/**
 * sol-flow-generator is a tool to generate C code from fbp files. This program
 * doesn't use the Soletta internal node and flow descriptions due to run time
 * requirements for cross-compiling environments.
 *
 * While cross-compiling Soletta where the application is generated from a fbp
 * file the generator must not rely on libsoletta.so neither the node-type's
 * modules since we may be dealing with different architectures for the host and
 * the target binaries.
 *
 * Having said that, this program implements its own node-type (the .json files)
 * parsing and has its own type data structure - namely type-store.c.
 */

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
static struct sol_vector declared_metatypes_control_vector;

/* In order to ensure that each generated fbp type has an unique id. */
static unsigned int fbp_id_count;

struct fbp_data {
    struct type_store *store;
    char *filename;
    char *name;
    char *exported_options_symbol;
    struct sol_fbp_graph graph;
    struct sol_vector declared_fbp_types;
    struct sol_vector declared_meta_types;
    struct sol_vector exported_options;
    int id;
};

struct exported_option_description {
    struct option_description *description;
    struct sol_str_slice node_option;
};

struct exported_option {
    int node;
    struct sol_fbp_node *node_ptr;
    struct sol_str_slice node_options_symbol;
    struct sol_vector options;
};

struct declared_fbp_type {
    char *name;
    int id;
};

struct declared_metatype_control {
    struct sol_str_slice type;
    bool start_generated;
    bool end_generated;
};

struct declared_metatype {
    struct sol_str_slice type;
    struct sol_str_slice contents;
    struct sol_str_slice name;
    char *c_name;
};

struct node_data {
    struct type_description *desc;
    int type_index;
    bool is_fbp;
    bool is_metatype;
};

static struct port_description error_port = {
    .name = (char *)SOL_FLOW_NODE_PORT_ERROR_NAME,
    .data_type = (char *)"error",
    .base_port_idx = SOL_FLOW_NODE_PORT_ERROR,
};

static struct exported_option *
get_exported_option_description_by_node(struct fbp_data *data, int node)
{
    struct exported_option *ex_op;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->exported_options, ex_op, i) {
        if (ex_op->node == node)
            return ex_op;
    }

    return NULL;
}

static int
to_c_symbol(const char *str, struct sol_buffer *buf)
{
    const char *start, *p;

    sol_buffer_init(buf);

    for (start = p = str; *p; p++) {
        if (isalnum(*p) || *p == '_')
            continue;
        else {
            struct sol_str_slice slice = {
                .data = start,
                .len = p - start
            };
            int r;

            r = sol_buffer_append_slice(buf, slice);
            SOL_INT_CHECK(r, < 0, r);

            r = sol_buffer_append_printf(buf, "__X%02X__", *p);;
            SOL_INT_CHECK(r, < 0, r);

            start = p + 1;
        }
    }

    if (buf->used == 0) {
        sol_buffer_init_flags(buf, (char *)str, p - str,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
        buf->used = buf->capacity;
    } else if (start < p) {
        struct sol_str_slice slice = {
            .data = start,
            .len = p - start
        };
        int r;

        r = sol_buffer_append_slice(buf, slice);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

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
    struct node_data *nd;

    assert(n);
    nd = n->user_data;
    return nd->desc;
}

static void
handle_suboptions(const struct sol_fbp_meta *meta,
    void (*handle_func)(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *opt_name, const char *fbp_file),
    const char *opt_name, const char *fbp_file)
{
    uint16_t i = 0;
    char *p, *remaining;

    remaining = strndupa(meta->value.data, meta->value.len);

    while (remaining) {
        p = memchr(remaining, '|', strlen(remaining));
        if (p)
            *p = '\0';
        handle_func(meta, remaining, i, opt_name, fbp_file);

        if (!p)
            break;

        remaining = p + 1;
        i++;
    }
}

static void
handle_suboption_with_explicit_fields(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *opt_name, const char *fbp_file)
{
    char *p = memchr(option, ':', strlen(option));

    if (!p) {
        sol_fbp_log_print(fbp_file, meta->position.line, meta->position.column, "Wrong suboption format, ignoring"
            " value '%s'. You cannot mix the formats, choose one 'opt1:val1|opt2:val2...' or 'val1|val2...'", option);
        return;
    }

    *p = '=';
    out("            .%s.%s,\n", opt_name, option);
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

static const char *
get_irange_drange_option_value(const char *option)
{
    if (!strcasecmp(option, "nan"))
        return "NAN";
    if (!strcasecmp(option, "inf"))
        return "INFINITY";
    return option;
}

static void
handle_irange_drange_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *opt_name, const char *fbp_file)
{
    const char *irange_drange_fields[] = { "val", "min", "max", "step", NULL };

    if (check_suboption(option, meta, fbp_file))
        out("            .%s.%s = %s,\n", opt_name,
            irange_drange_fields[index],
            get_irange_drange_option_value(option));
}

static void
handle_rgb_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *opt_name, const char *fbp_file)
{
    const char *rgb_fields[] = { "red", "green", "blue",
                                 "red_max", "green_max", "blue_max", NULL };

    if (check_suboption(option, meta, fbp_file))
        out("            .%s.%s = %s,\n", opt_name,
            rgb_fields[index], option);
}

static void
handle_direction_vector_suboption(const struct sol_fbp_meta *meta, char *option, uint16_t index, const char *opt_name, const char *fbp_file)
{
    const char *direction_vector_fields[] = { "x", "y", "z",
                                              "min", "max", NULL };

    if (check_suboption(option, meta, fbp_file))
        out("            .%s.%s = %s,\n", opt_name,
            direction_vector_fields[index], option);
}

static void *
has_explicit_fields(const struct sol_str_slice slice)
{
    return memchr(slice.data, ':', slice.len);
}

static bool
handle_option(const struct sol_fbp_meta *meta, struct option_description *o,
    const char *name_prefix,
    const struct sol_str_slice opt_name,
    const char *fbp_file)
{
    char aux_name[2048];
    struct sol_buffer buf;
    struct sol_fbp_meta unquoted_meta;
    int err;
    bool r = false;

    if (!sol_str_slice_eq(meta->key, opt_name))
        return true;

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

    err = snprintf(aux_name, sizeof(aux_name), "%s%s", name_prefix, o->name);
    SOL_INT_CHECK(err, < 0, false);
    SOL_INT_CHECK(err, >= (int)sizeof(aux_name), false);
    sol_buffer_init(&buf);
    if (to_c_symbol(aux_name, &buf)) {
        SOL_ERR("Could not convert %s to C symbol", aux_name);
        goto exit;
    }

    if (streq(o->data_type, "int") || streq(o->data_type, "float")) {
        r = true;
        if (has_explicit_fields(meta->value))
            handle_suboptions(meta, handle_suboption_with_explicit_fields, (const char *)buf.data, fbp_file);
        else
            handle_suboptions(meta, handle_irange_drange_suboption, (const char *)buf.data, fbp_file);
    } else if (streq(o->data_type, "rgb")) {
        r = true;
        if (has_explicit_fields(meta->value))
            handle_suboptions(meta, handle_suboption_with_explicit_fields, (const char *)buf.data, fbp_file);
        else
            handle_suboptions(meta, handle_rgb_suboption, (const char *)buf.data, fbp_file);
    } else if (streq(o->data_type, "direction-vector")) {
        r = true;
        if (has_explicit_fields(meta->value))
            handle_suboptions(meta, handle_suboption_with_explicit_fields, (const char *)buf.data, fbp_file);
        else
            handle_suboptions(meta, handle_direction_vector_suboption, (const char *)buf.data, fbp_file);
    } else if (streq(o->data_type, "string")) {
        r = true;
        if (meta->value.data[0] == '"')
            out("            .%s = %.*s,\n", (const char *)buf.data, SOL_STR_SLICE_PRINT(meta->value));
        else
            out("            .%s = \"%.*s\",\n", (const char *)buf.data, SOL_STR_SLICE_PRINT(meta->value));
    } else {
        r = true;
        out("            .%s = %.*s,\n", (const char *)buf.data, SOL_STR_SLICE_PRINT(meta->value));
    }
exit:
    sol_buffer_fini(&buf);
    return r;
}

static bool
handle_options(const struct sol_fbp_meta *meta, struct sol_vector *options,
    const char *name_prefix, const char *fbp_file)
{
    struct option_description *o;
    uint16_t i;
    bool r = false;

    SOL_VECTOR_FOREACH_IDX (options, o, i) {
        r = handle_option(meta, o, name_prefix, sol_str_slice_from_str(o->name),
            fbp_file);
        if (!r) {
            sol_fbp_log_print(fbp_file, meta->position.line,
                meta->position.column, "Invalid option key '%.*s'",
                SOL_STR_SLICE_PRINT(meta->key));
            break;
        }
    }
    return r;
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

static struct node_data *
get_node_data(struct type_store *common_store, struct type_store *parent_store, struct sol_fbp_node *n, const char *fbp_file)
{
    struct node_data *nd;

    nd = calloc(1, sizeof(*nd));
    SOL_NULL_CHECK(nd, NULL);

    nd->desc = sol_fbp_generator_resolve_type(common_store, parent_store, n, fbp_file);
    SOL_NULL_CHECK_GOTO(nd->desc, resolve_error);

    return nd;
resolve_error:
    free(nd);
    return NULL;
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
    struct exported_option *exported_opts;
    struct exported_option_description *exported_desc;
    struct sol_fbp_meta *m;
    struct sol_fbp_node *n;
    uint16_t i, j, k;

    if (data->exported_options.len > 0) {
        out("    static const struct %s exported_opts = GENERATED_%s_OPTIONS_DEFAULT(\n",
            data->exported_options_symbol, data->exported_options_symbol);
        SOL_VECTOR_FOREACH_IDX (&data->exported_options, exported_opts, i) {
            SOL_VECTOR_FOREACH_IDX (&exported_opts->node_ptr->meta, m, j) {
                SOL_VECTOR_FOREACH_IDX (&exported_opts->options,
                    exported_desc, k) {
                    if (!handle_option(m, exported_desc->description,
                        "opt_", exported_desc->node_option, data->filename))
                        return EXIT_FAILURE;
                }
            }
        }
        out("        );\n\n");
    }
    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, i) {
        const char *name_prefix = "";
        struct type_description *desc = ((struct node_data *)n->user_data)->desc;

        if (n->meta.len <= 0)
            continue;

        out("    static const struct %s opts%d =\n", desc->options_symbol, i);
        if (!desc->generated_options)
            out("        %s_OPTIONS_DEFAULTS(\n", desc->symbol);
        else {
            out("         GENERATED_%s_OPTIONS_DEFAULT(\n",
                desc->options_symbol);
            name_prefix = "opt_";
        }

        SOL_VECTOR_FOREACH_IDX (&n->meta, m, j) {
            if (!handle_options(m, &desc->options, name_prefix, data->filename))
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
        struct sol_buffer c_name;
        to_c_symbol(dec_type->name, &c_name);
        out("    const struct sol_flow_node_type *type_%s = create_%d_%s_type();\n",
            (const char *)c_name.data, dec_type->id, (const char *)c_name.data);
        sol_buffer_fini(&c_name);
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
        struct node_data *nd = n->user_data;

        if (nd->is_fbp)
            out("    nodes[%d].type = %s;\n", i, nd->desc->symbol);
        else if (nd->is_metatype)
            out("    nodes[%d].type = &%s;\n", i, nd->desc->symbol);
        else
            out("    nodes[%d].type = external_types[%d];\n", i, nd->type_index);
    }

    SOL_VECTOR_FOREACH_IDX (&data->declared_fbp_types, dec_type, i) {
        struct sol_buffer c_name;
        to_c_symbol(dec_type->name, &c_name);
        out("\n    if (!type_%s)\n"
            "        return NULL;\n",
            (const char *)c_name.data);
        sol_buffer_fini(&c_name);
    }
}

static const char *
get_type_data_by_name(const char *type)
{
    if (streq(type, "int"))
        return "struct sol_irange";
    if (streq(type, "float"))
        return "struct sol_drange";
    if (streq(type, "string"))
        return "const char *";
    if (streq(type, "rgb"))
        return "struct sol_rgb";
    if (streq(type, "direction-vector"))
        return "struct sol_direction_vector";
    if (streq(type, "boolean"))
        return "bool";
    if (streq(type, "byte"))
        return "unsigned char";
    return NULL;
}

static bool
generate_fbp_node_options(struct fbp_data *data)
{
    struct exported_option *ex_opt;
    struct exported_option_description *op_desc;
    uint16_t i, j;
    const char *data_type;
    struct sol_buffer buf;

    out("struct %s {\n"
        "    struct sol_flow_node_options base;\n"
        "    #define OPTIONS_%s_API_VERSION (1)\n",
        data->exported_options_symbol, data->exported_options_symbol);

    SOL_VECTOR_FOREACH_IDX (&data->exported_options, ex_opt, i) {
        SOL_VECTOR_FOREACH_IDX (&ex_opt->options, op_desc, j) {
            data_type = get_type_data_by_name(op_desc->description->data_type);
            if (!data_type) {
                SOL_ERR("Unknown option type:%s", op_desc->description->data_type);
                return false;
            }
            if (to_c_symbol(op_desc->description->name, &buf)) {
                SOL_ERR("Could not convert %s to C symbol",
                    op_desc->description->name);
                sol_buffer_fini(&buf);
                return false;
            }
            out("    %s opt_%s;\n", data_type, (const char *)buf.data);
            sol_buffer_fini(&buf);
        }
    }

    out("};\n\n");
    out("#define GENERATED_%s_OPTIONS_DEFAULT(...) { \\\n"
        "    .base = { \\\n"
        "        .api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION, \\\n"
        "        .sub_api = OPTIONS_%s_API_VERSION \\\n"
        "    }, \\\n"
        "    __VA_ARGS__ \\\n"
        "}\n\n", data->exported_options_symbol, data->exported_options_symbol);

    return true;
}

static bool
generate_child_opts(struct fbp_data *data,
    const char *opts_func)
{
    uint16_t i, j;
    struct exported_option *ex_opt;
    struct exported_option_description *opt_desc;
    struct sol_buffer buf;

    out("static int\n"
        "%s(const struct sol_flow_node_type *type, uint16_t child_index, const struct sol_flow_node_options *opts, struct sol_flow_node_options *child_opts)\n"
        "{\n"
        "    struct %s *node_opts = (struct %s *)opts;\n\n",
        opts_func, data->exported_options_symbol,
        data->exported_options_symbol);

    SOL_VECTOR_FOREACH_IDX (&data->exported_options, ex_opt, i) {
        out("     %s (child_index == %d) {\n"
            "         struct %.*s *child = (struct %.*s *) child_opts;\n",
            !i ? "if" : "else if",
            ex_opt->node, SOL_STR_SLICE_PRINT(ex_opt->node_options_symbol),
            SOL_STR_SLICE_PRINT(ex_opt->node_options_symbol));
        SOL_VECTOR_FOREACH_IDX (&ex_opt->options, opt_desc, j) {
            if (to_c_symbol(opt_desc->description->name, &buf)) {
                SOL_ERR("Could not convert %s to C symbol",
                    opt_desc->description->name);
                sol_buffer_fini(&buf);
                return false;
            }
            out("        child->%.*s =  node_opts->opt_%s;\n",
                SOL_STR_SLICE_PRINT(opt_desc->node_option),
                (const char *)buf.data);
            sol_buffer_fini(&buf);
        }
        out("     }\n");
    }

    out("    return 0;\n"
        "}\n\n");
    return true;
}

static bool
generate_create_type_function(struct fbp_data *data)
{
    struct sol_buffer c_name;
    int err;
    char opts_func[2048];
    bool r = false;

    to_c_symbol(data->name, &c_name);
    if (data->exported_options.len) {
        err = snprintf(opts_func, sizeof(opts_func), "child_opts_set_%d_%s",
            data->id, (const char *)c_name.data);
        SOL_INT_CHECK_GOTO(err, < 0, exit);
        SOL_INT_CHECK_GOTO(err, >= (int)sizeof(opts_func), exit);

        if (!generate_fbp_node_options(data))
            goto exit;
        if (!generate_child_opts(data, opts_func))
            goto exit;
    }

    out("\nstatic const struct sol_flow_node_type *\n"
        "create_%d_%s_type(void)\n"
        "{\n",
        data->id,
        (const char *)c_name.data);
    sol_buffer_fini(&c_name);

    out("    struct sol_flow_node_type *node_type;\n");

    if (!generate_options(data) || !generate_connections(data) || !generate_exports(data))
        goto exit;

    generate_node_specs(data);

    out("\n"
        "    struct sol_flow_static_spec spec = {\n"
        "        .api_version = SOL_FLOW_STATIC_API_VERSION,\n"
        "        .nodes = nodes,\n"
        "        .conns = conns,\n"
        "        .exported_in = %s,\n"
        "        .exported_out = %s,\n"
        "        .child_opts_set = %s,\n"
        "    };\n",
        data->graph.exported_in_ports.len > 0 ? "exported_in" : "NULL",
        data->graph.exported_out_ports.len > 0 ? "exported_out" : "NULL",
        data->exported_options.len > 0 ? opts_func : "NULL");

    generate_node_type_assignments(data);

    out("\n"
        "    node_type = sol_flow_static_new_type(&spec);\n");

    if (data->exported_options.len > 0) {
        out("\n"
            "    node_type->options_size = sizeof(struct %s);\n"
            "    node_type->default_options = &exported_opts;\n",
            data->exported_options_symbol);
    }

    out("\n"
        "    return node_type;\n"
        "}\n\n");
    r = true;
exit:
    sol_buffer_fini(&c_name);
    return r;
}

struct generate_context {
    struct sol_vector modules;
    struct sol_vector types_to_initialize;
};

struct type_to_init {
    struct sol_str_slice symbol;
    struct sol_str_slice module;
};

static bool
is_fbp_type(struct fbp_data *data, const struct sol_str_slice name)
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
is_metatype(struct fbp_data *data, const struct sol_str_slice name)
{
    struct declared_metatype *meta;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->declared_meta_types, meta, i) {
        if (sol_str_slice_eq(name, meta->name))
            return true;
    }

    return false;
}

static bool
contains_slice(const struct sol_vector *v, const struct sol_str_slice name, uint16_t *idx)
{
    struct sol_str_slice *slice;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (v, slice, i) {
        if (sol_str_slice_eq(*slice, name)) {
            *idx = i;
            return true;
        }
    }
    return false;
}

static bool
collect_context_info(struct generate_context *ctx, struct fbp_data *data)
{
    struct sol_fbp_node *node;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, node, i) {
        struct node_data *nd;
        struct type_description *desc;
        const char *sep;
        struct sol_str_slice name, module, symbol;
        struct type_to_init *t = NULL;
        uint16_t idx;

        /* Need to go via descriptions to get the real resolved name,
         * after conffile pass. */
        nd = node->user_data;
        desc = nd->desc;
        name = sol_str_slice_from_str(desc->name);

        /* Ignore since these are completely defined in the generated code. */
        if (is_fbp_type(data, name)) {
            nd->is_fbp = true;
            continue;
        }

        if (is_metatype(data, name)) {
            nd->is_metatype = true;
            continue;
        }

        symbol = sol_str_slice_from_str(desc->symbol);
        if (!contains_slice(&ctx->types_to_initialize, symbol, &idx)) {
            t = sol_vector_append(&ctx->types_to_initialize);
            if (!t)
                return false;
            t->symbol = symbol;
            idx = ctx->types_to_initialize.len - 1;
        }
        nd->type_index = idx;

        module = name;
        sep = strstr(name.data, "/");
        if (sep) {
            module.len = sep - module.data;
        }

        if (t)
            t->module = module;

        if (!contains_slice(&ctx->modules, module, &idx)) {
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
    int i, entry_idx;

    *elements = 0;

    SOL_PTR_VECTOR_FOREACH_IDX (maps, map, i) {
        out("\n");
        for (iter = map->entries, entry_idx = 0; iter->key; iter++, entry_idx++) {
            entry = iter->val;
            out("SOL_MEMMAP_ENTRY_BIT_SIZE(map%d_entry%d, %zu, %zu, %u, %u);\n",
                i, entry_idx, entry->offset, entry->size, entry->bit_offset,
                entry->bit_size);
        }

        out("\nstatic const struct sol_str_table_ptr _memmap%d_entries[] = {\n", i);
        for (iter = map->entries, entry_idx = 0; iter->key; iter++, entry_idx++) {
            entry = iter->val;
            out("   SOL_STR_TABLE_PTR_ITEM(\"%s\", &map%d_entry%d),\n",
                iter->key, i, entry_idx);
        }
        out("   { }\n"
            "};\n");

        out("\nstatic const struct sol_memmap_map _memmap%d = {\n"
            "   .version = %d,\n"
            "   .path = \"%s\",\n"
            "   .entries = _memmap%d_entries\n"
            "};\n",
            i, map->version, map->path, i);
    }

    *elements = i;
}
#endif

static bool
generate_metatypes_start(struct fbp_data *data)
{
    struct declared_metatype_control *control;
    struct declared_metatype *meta;
    uint16_t i, j;
    sol_flow_metatype_generate_code_func generate_func;

    SOL_VECTOR_FOREACH_IDX (&data->declared_meta_types, meta, i) {
        bool has_start = false;

        SOL_VECTOR_FOREACH_IDX (&declared_metatypes_control_vector,
            control, j) {
            if (sol_str_slice_eq(control->type, meta->type) &&
                control->start_generated) {
                has_start = true;
                break;
            }
        }

        if (has_start)
            continue;

        control = sol_vector_append(&declared_metatypes_control_vector);
        if (!control) {
            SOL_ERR("Could not create metatype control variable for:%.*s",
                SOL_STR_SLICE_PRINT(meta->name));
            return false;
        }
        control->type = meta->type;
        control->start_generated = true;
        generate_func =
            sol_flow_metatype_get_generate_code_start_func(meta->type);
        if (!generate_func) {
            SOL_ERR("The meta-type:%.*s does not provide a generate code"
                " start function", SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }

        if (generate_func(&output_buffer,
            sol_str_slice_from_str(meta->c_name), meta->contents)) {
            SOL_ERR("Could not generate the start code for meta type:%.*s-%.*s",
                SOL_STR_SLICE_PRINT(meta->name),
                SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }
    }
    return true;
}

static bool
generate_metatypes_body(struct fbp_data *data)
{
    struct declared_metatype *meta;
    uint16_t i;
    sol_flow_metatype_generate_code_func generate_func;

    SOL_VECTOR_FOREACH_IDX (&data->declared_meta_types, meta, i) {
        generate_func =
            sol_flow_metatype_get_generate_code_type_func(meta->type);
        if (!generate_func) {
            SOL_ERR("The meta-type:%.*s does not provide a generate code"
                " type function", SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }
        if (generate_func(&output_buffer,
            sol_str_slice_from_str(meta->c_name), meta->contents)) {
            SOL_ERR("Could not generate the body code for meta type:%.*s-%.*s",
                SOL_STR_SLICE_PRINT(meta->name),
                SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }
    }

    return true;
}

static bool
generate_metatypes_end(struct fbp_data *data)
{
    struct declared_metatype_control *control, *found;
    struct declared_metatype *meta;
    uint16_t i, j;
    sol_flow_metatype_generate_code_func generate_func;

    SOL_VECTOR_FOREACH_IDX (&data->declared_meta_types, meta, i) {
        found = NULL;
        SOL_VECTOR_FOREACH_IDX (&declared_metatypes_control_vector,
            control, j) {
            if (sol_str_slice_eq(control->type, meta->type)) {
                found = control;
                break;
            }
        }

        if (!found) {
            SOL_ERR("Could not find the metatype:%.*s in metatypes control"
                " vector", SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }

        if (found->end_generated)
            continue;

        found->end_generated = true;
        generate_func =
            sol_flow_metatype_get_generate_code_end_func(meta->type);
        if (!generate_func) {
            SOL_ERR("The meta-type:%.*s does not provide a generate code"
                " end function", SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }
        if (generate_func(&output_buffer,
            sol_str_slice_from_str(meta->c_name), meta->contents)) {
            SOL_ERR("Could not generate the end code for meta type:%.*s-%.*s",
                SOL_STR_SLICE_PRINT(meta->name),
                SOL_STR_SLICE_PRINT(meta->type));
            return false;
        }
    }
    return true;
}

static int
generate(struct sol_vector *fbp_data_vector)
{
    struct generate_context _ctx = {
        .modules = SOL_VECTOR_INIT(struct sol_str_slice),
        .types_to_initialize = SOL_VECTOR_INIT(struct type_to_init),
    }, *ctx = &_ctx;

    struct fbp_data *data;
    struct sol_str_slice *module;
    struct sol_ptr_vector *memory_maps;
    struct type_to_init *type;
    int types_count;
    uint16_t i, j;
    int r, memmap_elems = 0;
    struct declared_metatype *meta;

    out(
        "#include <math.h>\n"
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

    types_count = ctx->types_to_initialize.len;
    out("\nstatic const struct sol_flow_node_type *external_types[%d];\n", types_count);

    SOL_VECTOR_FOREACH_IDX (fbp_data_vector, data, i) {
        if (!generate_metatypes_start(data)) {
            r = EXIT_FAILURE;
            goto end;
        }
        if (!generate_metatypes_body(data)) {
            r = EXIT_FAILURE;
            goto end;
        }
    }

    SOL_VECTOR_FOREACH_IDX (fbp_data_vector, data, i) {
        if (!generate_metatypes_end(data)) {
            r = EXIT_FAILURE;
            goto end;
        }
    }

    /* Reverse since the dependencies appear later in the vector. */
    SOL_VECTOR_FOREACH_REVERSE_IDX (fbp_data_vector, data, i) {
        if (!generate_create_type_function(data)) {
            SOL_ERR("Couldn't generate %s type function", data->name);
            r = EXIT_FAILURE;
            goto end;
        }
    }

    out(
        "static bool\n"
        "initialize_types(void)\n"
        "{\n"
        "    const struct sol_flow_node_type *t;\n"
        "    int i = 0;\n\n");
    SOL_VECTOR_FOREACH_IDX (&ctx->types_to_initialize, type, i) {
        out(
            "    if (sol_flow_get_node_type(\"%.*s\", %.*s, &t) < 0)\n"
            "        return false;\n"
            "    if (t->init_type)\n"
            "        t->init_type();\n"
            "    external_types[i++] = t;\n",
            SOL_STR_SLICE_PRINT(type->module),
            SOL_STR_SLICE_PRINT(type->symbol));
    }

    SOL_VECTOR_FOREACH_IDX (fbp_data_vector, data, i) {
        SOL_VECTOR_FOREACH_IDX (&data->declared_meta_types, meta, j) {
            out("    if (%s.init_type)\n"
                "        %s.init_type();\n",
                meta->c_name, meta->c_name);
        }
    }

    if (memmap_elems) {
        out("\n");
        for (i = 0; i < memmap_elems; i++)
            out("    sol_memmap_add_map(&_memmap%d);\n", i);
    }
    out(
        "    return true;\n"
        "}\n\n");

    if (!args.export_symbol) {
        out(
            "static const struct sol_flow_node_type *root_type;\n"
            "static struct sol_flow_node *flow;\n"
            "\n"
            "static void\n"
            "startup(void)\n"
            "{\n"
            "    if (!initialize_types())\n"
            "        return;\n"
            "    root_type = create_0_root_type();\n"
            "    if (!root_type)\n"
            "        return;\n\n"
            "    flow = sol_flow_node_new(NULL, NULL, root_type, NULL);\n"
            "}\n\n"
            "static void\n"
            "shutdown(void)\n"
            "{\n"
            "    sol_flow_node_del(flow);\n"
            "    sol_flow_node_type_del((struct sol_flow_node_type *)root_type);\n"
            "}\n\n"
            "SOL_MAIN_DEFAULT(startup, shutdown);\n");
    } else {
        out(
            "const struct sol_flow_node_type *\n"
            "%s(void) {\n"
            "    static const struct sol_flow_node_type *type = NULL;\n"
            "    if (!type) {\n"
            "        if (!initialize_types())\n"
            "            return NULL;\n"
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
search_fbp_file(char *fullpath, const struct sol_str_slice basename)
{
    struct stat s;
    const char *p;
    uint16_t i;
    int err;

    SOL_PTR_VECTOR_FOREACH_IDX (&args.fbp_search_paths, p, i) {
        err = snprintf(fullpath, PATH_MAX, "%s/%.*s", p,
            SOL_STR_SLICE_PRINT(basename));
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
store_exported_options(struct type_store *common_store,
    struct type_store *parent_store,
    struct fbp_data *data,
    struct sol_vector *sol_options,
    struct sol_vector *type_options)
{
    struct type_description *node_desc;
    struct sol_fbp_option *fbp_option;
    struct sol_fbp_node *n;
    struct option_description *op_desc, *node_op_desc, *to_export;
    struct exported_option *exported_option;
    struct exported_option_description *exported_description;
    uint16_t i, j;

    sol_vector_init(type_options, sizeof(struct option_description));

    SOL_VECTOR_FOREACH_IDX (sol_options, fbp_option, i) {
        exported_option = get_exported_option_description_by_node(data,
            fbp_option->node);

        if (!exported_option) {
            exported_option = sol_vector_append(&data->exported_options);
            if (!exported_option) {
                SOL_ERR("Could not create an option to be exported");
                return false;
            }
            exported_option->node = fbp_option->node;
            sol_vector_init(&exported_option->options,
                sizeof(struct exported_option_description));
        }

        op_desc = calloc(1, sizeof(struct option_description));
        if (!op_desc) {
            SOL_ERR("Could not create an option description");
            return false;
        }

        to_export = sol_vector_append(type_options);

        if (!to_export) {
            SOL_ERR("Could not create a option to be exported");
            goto err;
        }

        exported_description = sol_vector_append(&exported_option->options);

        if (!exported_description) {
            SOL_ERR("Could not create the exported option description");
            goto err;
        }

        exported_description->node_option = fbp_option->node_option;
        exported_description->description = op_desc;

        n = sol_vector_get(&data->graph.nodes, fbp_option->node);

        if (!n) {
            SOL_ERR("Could not find node that provides the option:%.*s",
                SOL_STR_SLICE_PRINT(fbp_option->node_option));
            return false;
        }

        exported_option->node_ptr = n;
        node_desc = sol_fbp_generator_resolve_type(common_store,
            parent_store, n,  data->filename);
        if (!node_desc) {
            SOL_ERR("Could not get description for a type for: %.*s",
                SOL_STR_SLICE_PRINT(n->name));
            return false;
        }
        exported_option->node_options_symbol =
            sol_str_slice_from_str(node_desc->options_symbol);
        SOL_VECTOR_FOREACH_IDX (&node_desc->options, node_op_desc, j) {
            /* Only set exported options */
            if (!sol_str_slice_str_eq(fbp_option->node_option,
                node_op_desc->name))
                continue;
            if (!type_store_copy_option_description(op_desc, node_op_desc,
                fbp_option->name)) {
                SOL_ERR("Could not copy the description %.*s",
                    SOL_STR_SLICE_PRINT(fbp_option->name));
                return false;
            }
        }
        *to_export = *op_desc;
    }

    return true;

err:
    free(op_desc);
    return false;
}

static bool
add_fbp_type_to_type_store(struct type_store *common_store,
    struct type_store *parent_store,
    struct fbp_data *data)
{
    struct type_description type;
    struct sol_buffer c_name;
    int r;
    char node_type[2048];
    bool ret = false;

    type.name = data->name;

    to_c_symbol(data->name, &c_name);
    r = snprintf(node_type, sizeof(node_type), "type_%s", (const char *)c_name.data);
    sol_buffer_fini(&c_name);
    if (r < 0 || r >= (int)sizeof(node_type))
        return false;

    if (data->graph.options.len > 0) {
        r = asprintf(&data->exported_options_symbol, "options_%d_%s",
            data->id, node_type);
        if (r < 0) {
            data->exported_options_symbol = NULL;
            SOL_ERR("Could not create the exporter options symbol for:%s",
                data->name);
            return false;
        }
        type.options_symbol = data->exported_options_symbol;
        type.generated_options = true;
    } else {
        type.options_symbol = (char *)"";
        type.generated_options = false;
    }

    type.symbol = node_type;

    if (!store_exported_ports(data, &type.in_ports,
        &data->graph.exported_in_ports, true))
        goto fail_in_ports;

    if (!store_exported_ports(data, &type.out_ports,
        &data->graph.exported_out_ports, false))
        goto fail_out_ports;

    if (!store_exported_options(common_store, parent_store, data,
        &data->graph.options, &type.options))
        goto fail_options;

    ret = type_store_add_type(common_store, &type);

    if (!ret)
        SOL_WRN("Failed to add type %s to store", type.name);
    else
        SOL_DBG("Type %s added to store", type.name);

fail_options:
    //Do not free the options elements. It will be freed during the exit.
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
        n->user_data = get_node_data(common_store, data->store, n, data->filename);
        if (!n->user_data)
            return false;
        SOL_DBG("Node %.*s resolved", SOL_STR_SLICE_PRINT(n->name));
    }

    return true;
}

static bool
add_metatype_to_type_store(struct type_store *store,
    struct declared_metatype *meta)
{
    struct type_description type;
    sol_flow_metatype_ports_description_func get_ports;
    bool r = false;
    struct port_description *port;
    char name[2048];
    uint16_t i;
    int wrote;

    get_ports = sol_flow_metatype_get_ports_description_func(meta->type);

    if (!get_ports) {
        SOL_ERR("Could not get ports description function for:%.*s",
            SOL_STR_SLICE_PRINT(meta->name));
        return r;
    }

    /* Beware that struct sol_flow_metatype_port_description is a copy of struct port_description */
    if (get_ports(meta->contents, &type.in_ports, &type.out_ports)) {
        SOL_ERR("Could not get ports from metatype:%.*s",
            SOL_STR_SLICE_PRINT(meta->name));
        goto exit;
    }

    wrote = snprintf(name, sizeof(name), "%.*s",
        SOL_STR_SLICE_PRINT(meta->name));
    if (wrote < 0 || wrote >= (int)sizeof(name)) {
        SOL_ERR("Could not copy the meta-type name:%.*s",
            SOL_STR_SLICE_PRINT(meta->name));
        goto exit;
    }

    type.name = name;
    type.symbol = meta->c_name;
    type.options_symbol = (char *)"";
    type.generated_options = false;
    sol_vector_init(&type.options, sizeof(struct option_description));

    r = type_store_add_type(store, &type);
    if (!r) {
        SOL_ERR("Could not store the type %.*s",
            SOL_STR_SLICE_PRINT(meta->name));
    }
exit:
    SOL_VECTOR_FOREACH_IDX (&type.in_ports, port, i) {
        free(port->name);
        free(port->data_type);
    }

    SOL_VECTOR_FOREACH_IDX (&type.out_ports, port, i) {
        free(port->name);
        free(port->data_type);
    }
    sol_vector_clear(&type.out_ports);
    sol_vector_clear(&type.in_ports);
    return r;
}

static struct fbp_data *
create_fbp_data(struct sol_vector *fbp_data_vector, struct sol_ptr_vector *file_readers,
    struct type_store *common_store, const struct sol_str_slice name,
    const struct sol_str_slice fbp_basename)
{
    struct fbp_data *data;
    struct sol_fbp_error *fbp_error;
    struct sol_file_reader *fr;
    char filename[PATH_MAX];

    if (!search_fbp_file(filename, fbp_basename)) {
        SOL_ERR("Couldn't find file '%.*s': %s\n",
            SOL_STR_SLICE_PRINT(fbp_basename), sol_util_strerrora(errno));
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
    sol_vector_init(&data->declared_meta_types, sizeof(struct declared_metatype));
    sol_vector_init(&data->exported_options, sizeof(struct exported_option));

    data->name = sol_arena_strdup_slice(str_arena, name);
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
        uint16_t i, data_idx;
        struct declared_metatype *meta;
        struct sol_buffer buf;
        char aux_name[2048];
        int err;

        /* Get data index in order to use it after we handle the declarations. */
        data_idx = fbp_data_vector->len - 1;

        SOL_VECTOR_FOREACH_IDX (&data->graph.declarations, dec, i) {
            if (sol_str_slice_str_eq(dec->metatype, "fbp")) {

                d = create_fbp_data(fbp_data_vector, file_readers,
                    common_store, dec->name, dec->contents);
                if (!d)
                    return NULL;

                /* We need to do this because we may have appended new data to fbp_data_vector,
                 * this changes the position of data pointers since it's a sol_vector. */
                data = sol_vector_get(fbp_data_vector, data_idx);

                if (!add_fbp_type_to_type_store(common_store, data->store, d)) {
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
            } else {
                meta = sol_vector_append(&data->declared_meta_types);
                if (!meta) {
                    SOL_ERR("Could not create the metatype info");
                    return NULL;
                }

                meta->type = dec->metatype;
                meta->contents = dec->contents;
                meta->name = dec->name;

                err = snprintf(aux_name, sizeof(aux_name), "%.*s_%d",
                    SOL_STR_SLICE_PRINT(meta->name), data->id);

                if (err < 0 || err >= (int)sizeof(aux_name)) {
                    SOL_ERR("Could not copy the meta-type name:%.*s",
                        SOL_STR_SLICE_PRINT(meta->name));
                    return NULL;
                }

                if (to_c_symbol(aux_name, &buf)) {
                    SOL_ERR("Could not convert %.*s to C symbol",
                        SOL_STR_SLICE_PRINT(meta->name));
                    sol_buffer_fini(&buf);
                    return NULL;
                }
                if (buf.flags & SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED) {
                    meta->c_name = strdup((const char *)buf.data);
                    if (!meta->c_name) {
                        SOL_ERR("Could not copy the meta type: %.*s c_name.",
                            SOL_STR_SLICE_PRINT(meta->name));
                        sol_buffer_fini(&buf);
                        return NULL;
                    }
                } else
                    meta->c_name = sol_buffer_steal(&buf, NULL);
                add_metatype_to_type_store(data->store, meta);
                sol_buffer_fini(&buf);
            }
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
    struct declared_metatype *meta;
    struct exported_option *exported_opt;
    struct exported_option_description *opt_description;
    struct sol_fbp_node *n;
    uint16_t i, j, k;
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
    sol_vector_init(&declared_metatypes_control_vector,
        sizeof(struct declared_metatype_control));
    sol_ptr_vector_init(&file_readers);
    if (!create_fbp_data(&fbp_data_vector, &file_readers, common_store,
        sol_str_slice_from_str("root"),
        sol_str_slice_from_str(args.fbp_basename)))
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
        SOL_VECTOR_FOREACH_IDX (&data->graph.nodes, n, j)
            free(n->user_data);
        sol_fbp_graph_fini(&data->graph);
        sol_vector_clear(&data->declared_fbp_types);
        SOL_VECTOR_FOREACH_IDX (&data->declared_meta_types, meta, j)
            free(meta->c_name);
        sol_vector_clear(&data->declared_meta_types);
        SOL_VECTOR_FOREACH_IDX (&data->exported_options, exported_opt, j) {
            SOL_VECTOR_FOREACH_IDX (&exported_opt->options, opt_description,
                k) {
                free(opt_description->description->name);
                free(opt_description->description->data_type);
                free(opt_description->description);
            }
            sol_vector_clear(&exported_opt->options);
        }
        sol_vector_clear(&data->exported_options);
        free(data->exported_options_symbol);
    }
    sol_vector_clear(&fbp_data_vector);
    sol_vector_clear(&declared_metatypes_control_vector);
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
