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
get_node_name(const char *port_name, bool is_input_port)
{
    static char prefix_in[] = "node_for_input_";
    static char prefix_out[] = "node_for_output_";
    char *prefix, *name;
    int res;

    prefix = is_input_port ? prefix_in : prefix_out;
    res = asprintf(&name, "%s%s", prefix, port_name);

    if (res < 0)
        return NULL;

    return name;
}

static const char parent[] = "PARENT_NODE";

static int
add_simulation_node(struct runner *r, const char *node_type, const char *port_name, int idx, bool is_input_port)
{
    const struct sol_flow_node_type_description *description;
    const char *exported_port_name;
    char *node_name;

    description = r->root_type->description;
    exported_port_name = is_input_port ? description->ports_in[idx]->name :
                         description->ports_out[idx]->name;

    node_name = get_node_name(exported_port_name, is_input_port);
    SOL_NULL_CHECK_GOTO(node_name, error);
    sol_flow_builder_add_node_by_type(r->builder, node_name, node_type, NULL);

    if (is_input_port) {
        sol_flow_builder_connect(r->builder, node_name, port_name, parent,
            description->ports_in[idx]->name);
    } else {
        sol_flow_builder_connect(r->builder, parent,
            description->ports_out[idx]->name, node_name, port_name);
    }
    free(node_name);

    return 0;

error:
    sol_flow_builder_del(r->builder);
    free(node_name);
    return -ENOMEM;
}

struct map {
    const char *packet_name;
    const char *node_type;
    const char *port_name;
};

static const struct map input_nodes[] = {
    { "IRange", "gtk/spinbutton", "OUT" },
    { "DRange", "gtk/slider", "OUT" },
    { "Any", "gtk/toggle", "OUT" },
    { "Empty", "gtk/toggle", "OUT" },
    { "Boolean", "gtk/pushbutton", "OUT" },
    { "RGB", "gtk/rgb-editor", "OUT" },
    { "Byte", "byte/editor", "OUT" },
};

static const struct map output_nodes[] = {
    { "IRange", "gtk/label", "IN" },
    { "DRange", "gtk/label", "IN" },
    { "String", "gtk/label", "IN" },
    { "Boolean", "gtk/led", "IN" },
};

static int
attach_simulation_nodes(struct runner *r)
{
    const struct sol_flow_port_type_in *port_in;
    const struct sol_flow_port_type_out *port_out;
    uint16_t i, j, in_count, out_count;
    bool found;
    int err;

    r->root_type->get_ports_counts(r->root_type, &in_count, &out_count);

    if (in_count == 0 && out_count == 0)
        return 0;

    r->builder = sol_flow_builder_new();
    SOL_NULL_CHECK_GOTO(r->builder, fail_builder);
    sol_flow_builder_add_node(r->builder, parent, r->root_type, NULL);

    for (i = 0; i < in_count; i++) {
        port_in = sol_flow_node_type_get_port_in(r->root_type, i);
        found = false;

        for (j = 0; j < ARRAY_SIZE(input_nodes); j++) {
            if (streq(port_in->packet_type->name, input_nodes[j].packet_name)) {
                err = add_simulation_node(r, input_nodes[j].node_type, input_nodes[j].port_name, i, true);
                if (err < 0)
                    return err;
                found = true;
            }
        }
        if (!found) {
            SOL_WRN("No simulation node to connect to input port '%s' of type '%s'",
                r->root_type->description->ports_in[i]->name, port_in->packet_type->name);
        }
    }

    for (i = 0; i < out_count; i++) {
        port_out = sol_flow_node_type_get_port_out(r->root_type, i);
        found = false;

        for (j = 0; j < ARRAY_SIZE(output_nodes); j++) {
            if (streq(port_out->packet_type->name, output_nodes[j].packet_name)) {
                err = add_simulation_node(r, output_nodes[j].node_type, output_nodes[j].port_name, i, false);
                if (err < 0)
                    return err;
                found = true;
            }
        }
        if (!found) {
            SOL_WRN("No simulation node to connect to output port '%s' of type '%s'",
                r->root_type->description->ports_out[i]->name, port_out->packet_type->name);
        }
    }
    r->root_type = sol_flow_builder_get_node_type(r->builder);

    return 0;

fail_builder:
    return -ENOMEM;
}

struct runner *
runner_new(const char *filename, bool provide_sim_nodes)
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

    if (provide_sim_nodes) {
        err = attach_simulation_nodes(r);
        if (err < 0)
            goto error;
    }

    close_files(r);

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
    if (r->parser)
        sol_flow_parser_del(r->parser);
    if (r->builder)
        sol_flow_builder_del(r->builder);
    free(r->dirname);
    free(r->basename);
    free(r);
}
