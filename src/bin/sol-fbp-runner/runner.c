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
#include <libgen.h>

#include "sol-file-reader.h"
#include "sol-flow-parser.h"
#include "sol-flow-builder.h"
#include "sol-log.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "runner.h"

struct runner {
    struct sol_flow_parser *parser;
    struct sol_flow_node_type *root_type;
    struct sol_flow_node_named_options named_opts;
    struct sol_flow_node *root;
    struct sol_flow_builder *builder;

    const char *filename;
    char *basename;
    char *dirname;

    struct sol_ptr_vector file_readers;

    struct sol_flow_parser_client parser_client;
};

static int
read_file(void *data, const char *name, const char **buf, size_t *size)
{
    struct runner *r = data;
    struct sol_file_reader *fr = NULL;
    struct sol_str_slice slice;
    char *path;
    int err;

    err = asprintf(&path, "%s/%s", r->dirname, name);
    if (err < 0) {
        err = -ENOMEM;
        goto error;
    }

    fr = sol_file_reader_open(path);
    if (!fr) {
        err = -errno;
        SOL_ERR("Couldn't open input file '%s': %s", path, sol_util_strerrora(errno));
        goto error;
    }

    err = sol_ptr_vector_append(&r->file_readers, fr);
    if (err < 0)
        goto error;

    free(path);
    slice = sol_file_reader_get_all(fr);
    *buf = slice.data;
    *size = slice.len;
    return 0;

error:
    free(path);
    if (fr)
        sol_file_reader_close(fr);
    return err;
}

static void
close_files(struct runner *r)
{
    struct sol_file_reader *fr;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&r->file_readers, fr, i)
        sol_file_reader_close(fr);
    sol_ptr_vector_clear(&r->file_readers);
}

static char *
get_node_name(const char *port_name, int suffix, bool is_input_port)
{
    static char prefix_in[] = "node_for_input_";
    static char prefix_out[] = "node_for_output_";
    char *prefix, *name;
    int err;

    prefix = is_input_port ? prefix_in : prefix_out;
    err = asprintf(&name, "%s%s_%d", prefix, port_name, suffix);

    if (err < 0)
        return NULL;

    return name;
}

static const char parent[] = "PARENT_NODE";

static int
add_simulation_node(struct runner *r, const char *node_type, const char *port_name, const char *node_name, const char *parent_port_name, int idx, bool is_input_port)
{
    int err;

    err = sol_flow_builder_add_node_by_type(r->builder, node_name, node_type, NULL);
    SOL_INT_CHECK(err, < 0, err);

    if (is_input_port) {
        return sol_flow_builder_connect(r->builder, node_name, port_name, -1, parent,
            parent_port_name, idx);
    } else {
        return sol_flow_builder_connect(r->builder, parent, parent_port_name, idx,
            node_name, port_name, -1);
    }
    return 0;
}

struct map {
    const struct sol_flow_packet_type **packet_type;
    const char *node_type;
    const char *port_name;
};

static const struct map input_nodes[] = {
    { &SOL_FLOW_PACKET_TYPE_IRANGE, "gtk/spinbutton", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_DRANGE, "gtk/slider", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_ANY, "gtk/toggle", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_EMPTY, "gtk/toggle", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_BOOLEAN, "gtk/pushbutton", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_RGB, "gtk/rgb-editor", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_BYTE, "gtk/byte-editor", "OUT" },
};

static const struct map output_nodes[] = {
    { &SOL_FLOW_PACKET_TYPE_IRANGE, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_DRANGE, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_STRING, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_BOOLEAN, "gtk/led", "IN" },
};

int
runner_attach_simulation(struct runner *r)
{
    const struct sol_flow_port_type_in *port_in;
    const struct sol_flow_port_type_out *port_out;
    const struct sol_flow_port_description *port_desc;
    uint16_t in_count, out_count;
    int i, idx, err;
    unsigned int k;
    bool found;
    char *node_name;

    r->root_type->get_ports_counts(r->root_type, &in_count, &out_count);

    if (in_count == 0 && out_count == 0)
        return 0;

    r->builder = sol_flow_builder_new();
    SOL_NULL_CHECK(r->builder, -ENOMEM);

    err = sol_flow_builder_add_node(r->builder, parent, r->root_type, NULL);
    SOL_INT_CHECK_GOTO(err, < 0, error);

    for (i = 0; i < in_count; i++) {
        port_in = sol_flow_node_type_get_port_in(r->root_type, i);
        port_desc = sol_flow_node_get_port_in_description(r->root_type, i);
        SOL_NULL_CHECK_GOTO(port_desc, inval);

        idx = port_desc->array_size > 0 ? i - port_desc->base_port_idx : -1;
        found = false;

        for (k = 0; k < ARRAY_SIZE(input_nodes); k++) {
            if (port_in->packet_type == *(input_nodes[k].packet_type)) {
                node_name = get_node_name(port_desc->name, i - port_desc->base_port_idx, true);
                SOL_NULL_CHECK_GOTO(node_name, nomem);

                err = add_simulation_node(r, input_nodes[k].node_type, input_nodes[k].port_name, node_name,
                    port_desc->name, idx, true);

                free(node_name);
                SOL_INT_CHECK_GOTO(err, < 0, error);
                found = true;
                break;
            }
        }
        if (!found) {
            SOL_WRN("No simulation node to connect to input port '%s' of type '%s'",
                port_desc->name, port_in->packet_type->name);
        }
    }

    for (i = 0; i < out_count; i++) {
        port_out = sol_flow_node_type_get_port_out(r->root_type, i);
        port_desc = sol_flow_node_get_port_out_description(r->root_type, i);
        SOL_NULL_CHECK_GOTO(port_desc, inval);

        idx = port_desc->array_size > 0 ? i - port_desc->base_port_idx : -1;
        found = false;

        for (k = 0; k < ARRAY_SIZE(output_nodes); k++) {
            if (port_out->packet_type == *(output_nodes[k].packet_type)) {
                node_name = get_node_name(port_desc->name, i - port_desc->base_port_idx, false);
                SOL_NULL_CHECK_GOTO(node_name, nomem);

                err = add_simulation_node(r, output_nodes[k].node_type, output_nodes[k].port_name, node_name,
                    port_desc->name, idx, false);

                free(node_name);
                SOL_INT_CHECK_GOTO(err, < 0, error);
                found = true;
                break;
            }
        }
        if (!found) {
            SOL_WRN("No simulation node to connect to output port '%s' of type '%s'",
                port_desc->name, port_out->packet_type->name);
        }
    }
    r->root_type = sol_flow_builder_get_node_type(r->builder);

    return 0;

inval:
    sol_flow_builder_del(r->builder);
    return -EINVAL;
nomem:
    sol_flow_builder_del(r->builder);
    return -ENOMEM;
error:
    sol_flow_builder_del(r->builder);
    return err;
}

struct runner *
runner_new_from_file(
    const char *filename)
{
    struct runner *r;
    const char *buf;
    size_t size;
    int err;

    SOL_NULL_CHECK(filename, NULL);

    r = calloc(1, sizeof(*r));
    SOL_NULL_CHECK(r, NULL);

    sol_ptr_vector_init(&r->file_readers);

    r->parser_client.api_version = SOL_FLOW_PARSER_CLIENT_API_VERSION;
    r->parser_client.data = r;
    r->parser_client.read_file = read_file;

    r->parser = sol_flow_parser_new(&r->parser_client, NULL);
    if (!r->parser)
        goto error;

    r->filename = filename;
    r->dirname = strdup(dirname(strdupa(filename)));
    r->basename = strdup(basename(strdupa(filename)));

    err = read_file(r, r->basename, &buf, &size);
    if (err < 0) {
        errno = -err;
        goto error;
    }

    r->root_type = sol_flow_parse_buffer(r->parser, buf, size, filename);
    if (!r->root_type)
        goto error;

    close_files(r);

    return r;

error:
    close_files(r);
    runner_del(r);
    return NULL;
}

struct runner *
runner_new_from_type(
    const char *typename)
{
    struct runner *r;
    int err;

    SOL_NULL_CHECK(typename, NULL);

    r = calloc(1, sizeof(*r));
    SOL_NULL_CHECK(r, NULL);

    err = sol_flow_resolve(sol_flow_get_builtins_resolver(), typename,
        (const struct sol_flow_node_type **)&r->root_type, &r->named_opts);
    if (err < 0) {
        err = sol_flow_resolve(NULL, typename,
            (const struct sol_flow_node_type **)&r->root_type, &r->named_opts);
        if (err < 0) {
            fprintf(stderr, "Couldn't find type '%s'\n", typename);
            errno = -err;
            goto error;
        }
    }

    r->filename = strdup(typename);
    if (!r->filename)
        goto error;

    return r;

error:
    close_files(r);
    runner_del(r);
    return NULL;
}

int
runner_run(struct runner *r)
{
    r->root = sol_flow_node_new(NULL, r->filename, r->root_type, NULL);
    if (!r->root)
        return -1;

    return 0;
}

void
runner_del(struct runner *r)
{
    if (r->root)
        sol_flow_node_del(r->root);
    if (r->builder) {
        sol_flow_node_type_del(r->root_type);
        sol_flow_builder_del(r->builder);
    }
    if (r->parser)
        sol_flow_parser_del(r->parser);
    sol_flow_node_named_options_fini(&r->named_opts);
    free(r->dirname);
    free(r->basename);
    free(r);
}
