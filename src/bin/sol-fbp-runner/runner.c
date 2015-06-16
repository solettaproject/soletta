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
#include "sol-log.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "runner.h"

struct runner {
    struct sol_flow_parser *parser;
    struct sol_flow_node_type *root_type;
    struct sol_flow_node *root;

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

struct runner *
runner_new(const char *filename)
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
    free(r->dirname);
    free(r->basename);
    free(r);
}
