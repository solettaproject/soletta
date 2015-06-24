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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs-gen.h"

#include "sol-flow.h"
#include "sol-util.h"

struct fs_persist_data {
    FILE *file;
    void *value_ptr;
    struct sol_flow_packet *(*packet_new_fn)(const struct fs_persist_data *data);
    int (*packet_data_get_fn)(const struct sol_flow_packet *packet, void *value_ptr);
    int (*packet_send_fn)(struct sol_flow_node *node);
    size_t packet_data_size;
    bool last_set;
};

static int
fs_persist_open(struct sol_flow_node *node,
    void *data,
    const char *path)
{
    struct fs_persist_data *mdata = data;
    struct stat st;
    int r;

    mdata->file = fopen(path, "re+");
    if (!mdata->file) {
        SOL_WRN("Failed to open file %s", path);
        return -errno;
    }

    r = fstat(fileno(mdata->file), &st);
    SOL_INT_CHECK(r, < 0, r);

    /* a zero packet_data_size means dynamic size content */
    if (mdata->packet_data_size) {
        /* by returning early, mdata->last_set continues as false */
        if ((size_t)st.st_size < mdata->packet_data_size)
            return 0;

        r = fread(mdata->value_ptr, mdata->packet_data_size, 1, mdata->file);
        SOL_INT_CHECK(r, < 0, r);
    } else {
        /* by returning early, mdata->last_set continues as false */
        if (!st.st_size)
            return 0;
        mdata->value_ptr = calloc(1, st.st_size + 1);
        SOL_NULL_CHECK(mdata->value_ptr, -ENOMEM);
        do {
            r = fread(mdata->value_ptr, 256, 1, mdata->file);
        } while (r > 0);
        SOL_INT_CHECK(r, < 0, r);
        ((char *)mdata->value_ptr)[st.st_size] = '\0';
    }

    mdata->last_set = true;

    return mdata->packet_send_fn(node);
}

static void
fs_persist_close(struct sol_flow_node *node, void *data)
{
    struct fs_persist_data *mdata = data;

    fclose(mdata->file);
    if (!mdata->packet_data_size)
        free(mdata->value_ptr);
}

static int
fs_persist_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct fs_persist_data *mdata = data;
    unsigned char value[mdata->packet_data_size];
    void *value_ptr = NULL;
    size_t size = 0;
    int r;

    if (mdata->packet_data_size) {
        r = mdata->packet_data_get_fn(packet, value);
    } else {
        r = mdata->packet_data_get_fn(packet, &value_ptr);
    }
    SOL_INT_CHECK(r, < 0, r);

    r = fseek(mdata->file, SEEK_SET, 0);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->packet_data_size) {
        r = fwrite(value, mdata->packet_data_size, 1, mdata->file);
    } else {
        size = strlen(value_ptr) + 1;
        r = fwrite(value_ptr, size, 1, mdata->file);
    }
    SOL_INT_CHECK(r, < 0, r);

    fflush(mdata->file);

    if (mdata->packet_data_size) {
        memcpy(mdata->value_ptr, value, mdata->packet_data_size);
    } else {
        if (!mdata->value_ptr || strlen(mdata->value_ptr) + 1 < size) {
            void *tmp = realloc(mdata->value_ptr, size);
            SOL_NULL_CHECK(tmp, -ENOMEM);
            mdata->value_ptr = tmp;
        }
        memcpy(mdata->value_ptr, value_ptr, size);
    }

    mdata->last_set = true;

    return mdata->packet_send_fn(node);
}

struct fs_persist_boolean_data {
    struct fs_persist_data base;
    bool last_value;
};

static int
fs_persist_boolean_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_boolean(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
fs_persist_boolean_packet_send(struct sol_flow_node *node)
{
    struct fs_persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_boolean_packet
               (node, SOL_FLOW_NODE_TYPE_FS_PERSIST_BOOLEAN__OUT__OUT,
               *(bool *)mdata->value_ptr);
}

static struct sol_flow_packet *
fs_persist_boolean_packet_new(const struct fs_persist_data *data)
{
    struct fs_persist_boolean_data *mdata =
        (struct fs_persist_boolean_data *)data;

    return sol_flow_packet_new_boolean(mdata->last_value);
}

static int
fs_persist_boolean_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct fs_persist_boolean_data *mdata = data;
    const struct sol_flow_node_type_fs_persist_boolean_options *opts =
        (const struct sol_flow_node_type_fs_persist_boolean_options *)options;

    mdata->base.packet_data_size = sizeof(bool);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = fs_persist_boolean_packet_new;
    mdata->base.packet_data_get_fn = fs_persist_boolean_packet_data_get;
    mdata->base.packet_send_fn = fs_persist_boolean_packet_send;

    return fs_persist_open(node, data, opts->path);
}

struct fs_persist_byte_data {
    struct fs_persist_data base;
    unsigned char last_value;
};

static int
fs_persist_byte_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_byte(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
fs_persist_byte_packet_send(struct sol_flow_node *node)
{
    struct fs_persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_byte_packet
               (node, SOL_FLOW_NODE_TYPE_FS_PERSIST_BYTE__OUT__OUT,
               *(unsigned char *)mdata->value_ptr);
}

static struct sol_flow_packet *
fs_persist_byte_packet_new(const struct fs_persist_data *data)
{
    struct fs_persist_byte_data *mdata = (struct fs_persist_byte_data *)data;

    return sol_flow_packet_new_byte(mdata->last_value);
}

static int
fs_persist_byte_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct fs_persist_byte_data *mdata = data;
    const struct sol_flow_node_type_fs_persist_byte_options *opts =
        (const struct sol_flow_node_type_fs_persist_byte_options *)options;

    mdata->base.packet_data_size = sizeof(unsigned char);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = fs_persist_byte_packet_new;
    mdata->base.packet_data_get_fn = fs_persist_byte_packet_data_get;
    mdata->base.packet_send_fn = fs_persist_byte_packet_send;

    return fs_persist_open(node, data, opts->path);
}

struct fs_persist_irange_data {
    struct fs_persist_data base;
    struct sol_irange last_value;
};

static int
fs_persist_irange_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_irange(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
fs_persist_irange_packet_send(struct sol_flow_node *node)
{
    struct fs_persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_irange_packet
               (node, SOL_FLOW_NODE_TYPE_FS_PERSIST_INT__OUT__OUT,
               (struct sol_irange *)mdata->value_ptr);
}

static struct sol_flow_packet *
fs_persist_irange_packet_new(const struct fs_persist_data *data)
{
    struct fs_persist_irange_data *mdata =
        (struct fs_persist_irange_data *)data;

    return sol_flow_packet_new_irange(&mdata->last_value);
}

static int
fs_persist_irange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct fs_persist_irange_data *mdata = data;
    const struct sol_flow_node_type_fs_persist_int_options *opts =
        (const struct sol_flow_node_type_fs_persist_int_options *)options;

    mdata->base.packet_data_size = sizeof(struct sol_irange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = fs_persist_irange_packet_new;
    mdata->base.packet_data_get_fn = fs_persist_irange_packet_data_get;
    mdata->base.packet_send_fn = fs_persist_irange_packet_send;

    return fs_persist_open(node, data, opts->path);
}

struct fs_persist_drange_data {
    struct fs_persist_data base;
    struct sol_drange last_value;
};

static int
fs_persist_drange_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_drange(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
fs_persist_drange_packet_send(struct sol_flow_node *node)
{
    struct fs_persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_drange_packet
               (node, SOL_FLOW_NODE_TYPE_FS_PERSIST_FLOAT__OUT__OUT,
               (struct sol_drange *)mdata->value_ptr);
}

static struct sol_flow_packet *
fs_persist_drange_packet_new(const struct fs_persist_data *data)
{
    struct fs_persist_drange_data *mdata =
        (struct fs_persist_drange_data *)data;

    return sol_flow_packet_new_drange(&mdata->last_value);
}

static int
fs_persist_drange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct fs_persist_drange_data *mdata = data;
    const struct sol_flow_node_type_fs_persist_int_options *opts =
        (const struct sol_flow_node_type_fs_persist_int_options *)options;

    mdata->base.packet_data_size = sizeof(struct sol_drange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = fs_persist_drange_packet_new;
    mdata->base.packet_data_get_fn = fs_persist_drange_packet_data_get;
    mdata->base.packet_send_fn = fs_persist_drange_packet_send;

    return fs_persist_open(node, data, opts->path);
}

struct fs_persist_string_data {
    struct fs_persist_data base;
    const char *last_value;
};

static int
fs_persist_string_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_string(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
fs_persist_string_packet_send(struct sol_flow_node *node)
{
    struct fs_persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_string_packet
               (node, SOL_FLOW_NODE_TYPE_FS_PERSIST_STRING__OUT__OUT,
               (const char *)mdata->value_ptr);
}

static struct sol_flow_packet *
fs_persist_string_packet_new(const struct fs_persist_data *data)
{
    return sol_flow_packet_new_string(data->value_ptr);
}

static int
fs_persist_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct fs_persist_string_data *mdata = data;
    const struct sol_flow_node_type_fs_persist_int_options *opts =
        (const struct sol_flow_node_type_fs_persist_int_options *)options;

    mdata->base.packet_new_fn = fs_persist_string_packet_new;
    mdata->base.packet_data_get_fn = fs_persist_string_packet_data_get;
    mdata->base.packet_send_fn = fs_persist_string_packet_send;

    return fs_persist_open(node, data, opts->path);
}


#include "fs-gen.c"
