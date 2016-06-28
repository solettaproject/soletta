/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>

#include "sol-file-reader.h"
#include "sol-flow-parser.h"
#include "sol-flow-builder.h"
#include "sol-log.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "runner.h"

struct runner {
    struct sol_flow_parser *parser;

    struct sol_flow_node_type *root_type;
    struct sol_flow_node_options *root_options;

    struct sol_flow_builder *builder;
    struct sol_flow_node_type *sim_type;

    struct sol_flow_node *root;

    const char *filename;
    char *basename;
    char *dir;

    struct sol_ptr_vector file_readers;
    struct sol_ptr_vector *fbp_paths;

    struct sol_flow_parser_client parser_client;
};

static char *
stat_fullpath(const char *path, const char *basename)
{
    int err;
    struct stat s;
    char *fullpath;

    err = asprintf(&fullpath, "%s/%s", path, basename);
    if (err < 0) {
        errno = ENOMEM;
        return NULL;
    }

    if (stat(fullpath, &s) == 0 && S_ISREG(s.st_mode))
        return fullpath;

    free(fullpath);
    return NULL;
}

static char *
search_fbp_file(struct runner *r, const char *basename)
{
    char *p;
    char *fullpath;
    uint16_t i;

    fullpath = stat_fullpath(r->dir, basename);
    if (fullpath)
        return fullpath;

    SOL_PTR_VECTOR_FOREACH_IDX (r->fbp_paths, p, i) {
        fullpath = stat_fullpath(p, basename);
        if (fullpath)
            return fullpath;
    }

    SOL_ERR("Couldn't find file '%s'", basename);
    errno = EINVAL;
    return NULL;
}

static int
read_file(void *data, const char *name, struct sol_buffer *buf)
{
    struct runner *r = data;
    struct sol_file_reader *fr = NULL;
    struct sol_str_slice slice;
    char *path;
    int err;

    path = search_fbp_file(r, name);
    if (!path) {
        err = -errno;
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
    sol_buffer_init_flags(buf, (void *)slice.data, slice.len, SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
    buf->used = slice.len;
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
get_node_name(
    const char *port_name,
    const char *packet_type_name,
    int index)
{
    char *name;
    int err;

    if (index == -1) {
        err = asprintf(&name, "%s (%s)", port_name, packet_type_name);
    } else {
        err = asprintf(&name, "%s[%d] (%s)", port_name, index, packet_type_name);
    }

    if (err < 0)
        return NULL;

    return name;
}

static const char parent[] = "SIMULATOR";

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
    { &SOL_FLOW_PACKET_TYPE_ANY, "gtk/pushbutton", "PRESSED" },
    { &SOL_FLOW_PACKET_TYPE_EMPTY, "gtk/pushbutton", "PRESSED" },
    { &SOL_FLOW_PACKET_TYPE_BOOL, "gtk/toggle", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_RGB, "gtk/rgb-editor", "OUT" },
    { &SOL_FLOW_PACKET_TYPE_BYTE, "gtk/byte-editor", "OUT" },
};

static const struct map output_nodes[] = {
    { &SOL_FLOW_PACKET_TYPE_IRANGE, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_DRANGE, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_EMPTY, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_STRING, "gtk/label", "IN" },
    { &SOL_FLOW_PACKET_TYPE_BOOL, "gtk/led", "IN" },
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

    in_count = r->root_type->ports_in_count;
    out_count = r->root_type->ports_out_count;

    if (in_count == 0 && out_count == 0)
        return 0;

    r->builder = sol_flow_builder_new();
    SOL_NULL_CHECK(r->builder, -ENOMEM);

    err = sol_flow_builder_add_node(r->builder, parent, r->root_type, r->root_options);
    SOL_INT_CHECK_GOTO(err, < 0, error);

    for (i = 0; i < in_count; i++) {
        port_in = sol_flow_node_type_get_port_in(r->root_type, i);
        port_desc = sol_flow_node_get_description_port_in(r->root_type, i);
        SOL_NULL_CHECK_GOTO(port_desc, inval);

        idx = port_desc->array_size > 0 ? i - port_desc->base_port_idx : -1;
        found = false;

        for (k = 0; k < sol_util_array_size(input_nodes); k++) {
            if (port_in->packet_type == *(input_nodes[k].packet_type)) {
                node_name = get_node_name(port_desc->name, port_in->packet_type->name, idx);
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
        port_desc = sol_flow_node_get_description_port_out(r->root_type, i);
        SOL_NULL_CHECK_GOTO(port_desc, inval);

        idx = port_desc->array_size > 0 ? i - port_desc->base_port_idx : -1;
        found = false;

        for (k = 0; k < sol_util_array_size(output_nodes); k++) {
            if (port_out->packet_type == *(output_nodes[k].packet_type)) {
                node_name = get_node_name(port_desc->name, port_out->packet_type->name, idx);
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
    r->sim_type = sol_flow_builder_get_node_type(r->builder);

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

static int
parse_options(struct runner *r, const char **options_strv, struct sol_flow_node_named_options *resolved_opts)
{
    struct sol_flow_node_named_options user_opts = {}, type_opts = {}, opts = {};
    int err;

    if (!options_strv || !*options_strv)
        return 0;

    if (resolved_opts) {
        type_opts = *resolved_opts;
        *resolved_opts = (struct sol_flow_node_named_options){};
    }

    err = sol_flow_node_named_options_init_from_strv(&user_opts, r->root_type, options_strv);
    if (err < 0) {
        /* TODO: improve return value from the init function so we can
         * give better error messages. */
        fputs("Error: Options given with '-o' argument are not usable by this flow.\n", stderr);
        goto end;
    }

    if (type_opts.count + user_opts.count > 0) {
        const int member_size = sizeof(struct sol_flow_node_named_options_member);
        int count = type_opts.count + user_opts.count;

        opts.members = calloc(count, member_size);
        if (!opts.members) {
            err = -errno;
            fputs("Error: Not enough memory to build options.\n", stderr);
            goto end;
        }

        if (type_opts.count > 0)
            memcpy(opts.members, type_opts.members, type_opts.count * member_size);
        if (user_opts.count > 0)
            memcpy(opts.members + type_opts.count, user_opts.members, user_opts.count * member_size);
        opts.count = count;
    }

    if (opts.count > 0) {
        err = sol_flow_node_options_new(r->root_type, &opts, &r->root_options);
        if (err < 0) {
            fputs("Error: Couldn't create options from '-o'  argument for this flow.\n", stderr);
            goto end;
        }
        sol_flow_node_named_options_fini(&opts);
    }

    err = 0;

end:
    free(type_opts.members);
    free(user_opts.members);
    return err;
}

struct runner *
runner_new_from_file(
    const char *filename,
    const char **options_strv,
    struct sol_ptr_vector *fbps)
{
    struct runner *r;
    struct sol_buffer buf;
    int err;

    SOL_NULL_CHECK(filename, NULL);

    r = calloc(1, sizeof(*r));
    SOL_NULL_CHECK(r, NULL);

    sol_ptr_vector_init(&r->file_readers);
    r->fbp_paths = fbps;

    SOL_SET_API_VERSION(r->parser_client.api_version = SOL_FLOW_PARSER_CLIENT_API_VERSION; )
    r->parser_client.data = r;
    r->parser_client.read_file = read_file;

    r->parser = sol_flow_parser_new(&r->parser_client, NULL);
    if (!r->parser)
        goto error;

    r->filename = filename;

    r->dir = strdup(dirname(strdupa(filename)));
    SOL_NULL_CHECK_GOTO(r->dir, error);

    r->basename = strdup(basename(strdupa(filename)));

    err = read_file(r, r->basename, &buf);
    if (err < 0) {
        errno = -err;
        goto error;
    }

    r->root_type = sol_flow_parse_buffer(r->parser, &buf, filename);
    if (!r->root_type)
        goto error_parse;

    sol_buffer_fini(&buf);
    err = parse_options(r, options_strv, NULL);
    if (err < 0)
        goto error;

    close_files(r);

    return r;

error_parse:
    sol_buffer_fini(&buf);
error:
    close_files(r);
    runner_del(r);
    return NULL;
}

struct runner *
runner_new_from_type(
    const char *typename,
    const char **options_strv)
{
    struct sol_flow_node_named_options resolved_opts = {};
    struct runner *r;
    int err;

    SOL_NULL_CHECK(typename, NULL);

    r = calloc(1, sizeof(*r));
    SOL_NULL_CHECK(r, NULL);

    err = sol_flow_resolve(sol_flow_get_builtins_resolver(), typename,
        (const struct sol_flow_node_type **)&r->root_type, &resolved_opts);
    if (err < 0) {
        err = sol_flow_resolve(NULL, typename,
            (const struct sol_flow_node_type **)&r->root_type, &resolved_opts);
        if (err < 0) {
            fprintf(stderr, "Error: Couldn't find type '%s'\n", typename);
            errno = -err;
            goto error;
        }
    }

    err = parse_options(r, options_strv, &resolved_opts);
    if (err < 0)
        goto error;

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
    const struct sol_flow_node_type *type = r->sim_type;
    const struct sol_flow_node_options *opts = NULL;

    if (!type) {
        type = r->root_type;
        opts = r->root_options;
    }

    r->root = sol_flow_node_new(NULL, r->filename, type, opts);
    if (!r->root)
        return -1;

    return 0;
}

void
runner_del(struct runner *r)
{
    if (r->root_options)
        sol_flow_node_options_del(r->root_type, r->root_options);
    if (r->root)
        sol_flow_node_del(r->root);
    if (r->builder) {
        sol_flow_node_type_del(r->sim_type);
        sol_flow_builder_del(r->builder);
    }
    if (r->parser)
        sol_flow_parser_del(r->parser);
    free(r->dir);
    free(r->basename);
    free(r);
}
