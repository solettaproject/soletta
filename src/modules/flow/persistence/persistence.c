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

#include "sol-flow/persistence.h"

#include "sol-buffer.h"
#include "sol-flow.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"

#include "fs-storage.h"
#include "efivarfs-storage.h"

struct storage_fn {
    int (*write)(const char *name, struct sol_buffer *buffer);
    int (*read)(const char *name, struct sol_buffer *buffer);
};

struct persist_data {
    void *value_ptr;

    char *name;
    char *fs_dir_path;

    struct sol_flow_packet *(*packet_new_fn)(const struct persist_data *data);
    int (*packet_data_get_fn)(const struct sol_flow_packet *packet, void *value_ptr);
    int (*packet_send_fn)(struct sol_flow_node *node);
    void *(*node_get_default_fn)(struct sol_flow_node *node);

    const struct storage_fn *storage;

    size_t packet_data_size;
};

static const struct storage_fn fs_fn = {
    .write = fs_write_raw,
    .read = fs_read_raw
};

static const struct storage_fn efivars_fn = {
    .write = efivars_write_raw,
    .read = efivars_read_raw
};

static const struct sol_str_table_ptr storage_fn_table[] = {
    SOL_STR_TABLE_PTR_ITEM("fs", &fs_fn),
    SOL_STR_TABLE_PTR_ITEM("efivars", &efivars_fn),
    { }
};

static int
storage_write(struct persist_data *mdata, void *data, size_t size)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(data, size,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    buf.used = size;

    return mdata->storage->write(mdata->name, &buf);
}

static int
storage_read(struct persist_data *mdata, struct sol_buffer *buf)
{
    return mdata->storage->read(mdata->name, buf);
}

static void
persist_close(struct sol_flow_node *node, void *data)
{
    struct persist_data *mdata = data;

    if (!mdata->packet_data_size)
        free(mdata->value_ptr);

    free(mdata->name);
}

static int
persist_do(struct persist_data *mdata, struct sol_flow_node *node, void *value)
{
    size_t size = 0;
    int r;

    if (mdata->packet_data_size)
        size = mdata->packet_data_size;
    else
        size = strlen(value) + 1;

    r = storage_write(mdata, value, size);
    SOL_INT_CHECK(r, < 0, r);

    /* No packet_data_size means dynamic content (string). Let's reallocate if needed */
    if (!mdata->packet_data_size) {
        if (!mdata->value_ptr || strlen(mdata->value_ptr) + 1 < size) {
            void *tmp = realloc(mdata->value_ptr, size);
            SOL_NULL_CHECK(tmp, -ENOMEM);
            mdata->value_ptr = tmp;
        }
    }
    memcpy(mdata->value_ptr, value, size);

    return mdata->packet_send_fn(node);
}

static int
persist_reset(struct persist_data *mdata, struct sol_flow_node *node)
{
    void *value;

    value = mdata->node_get_default_fn(node);

    return persist_do(mdata, node, value);
}

static int
persist_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct persist_data *mdata = data;
    void *value;
    int r;

    if (mdata->packet_data_size) {
        value = alloca(mdata->packet_data_size);
        r = mdata->packet_data_get_fn(packet, value);
    } else
        r = mdata->packet_data_get_fn(packet, &value);

    SOL_INT_CHECK(r, < 0, r);

    return persist_do(mdata, node, value);
}

static int
reset_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct persist_data *mdata = data;

    return persist_reset(mdata, node);
}

static int
persist_open(struct sol_flow_node *node,
    void *data,
    const char *storage,
    const char *name)
{
    struct persist_data *mdata = data;
    struct sol_str_slice storage_slice;
    int r;

    if (!storage || *storage == '\0') {
        SOL_WRN("Must define a storage type");
        return -EINVAL;
    }

    storage_slice = sol_str_slice_from_str(storage);
    if (!sol_str_table_ptr_lookup(storage_fn_table, storage_slice, &mdata->storage)) {
        SOL_WRN("Invalid storage [%s]", storage);
        return -EINVAL;
    }

    mdata->name = strdup(name);
    SOL_NULL_CHECK(mdata->name, -ENOMEM);

    /* a zero packet_data_size means dynamic size content */
    if (mdata->packet_data_size) {
        struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(mdata->value_ptr,
            mdata->packet_data_size, SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

        r = storage_read(mdata, &buf);
    } else {
        struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;

        r = storage_read(mdata, &buf);
        if (r >= 0)
            mdata->value_ptr = sol_buffer_steal(&buf, NULL);
        else
            sol_buffer_fini(&buf);
    }
    if (r == -ENOENT) {
        /* No file. Send default value */
        return persist_reset(mdata, node);
    }
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return mdata->packet_send_fn(node);

err:
    persist_close(node, mdata);
    return r;
}

struct persist_boolean_data {
    struct persist_data base;
    bool last_value;
    bool default_value;
};

static void *
persist_boolean_node_get_default(struct sol_flow_node *node)
{
    struct persist_boolean_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value;
}

static int
persist_boolean_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_boolean(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_boolean_packet_send(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_boolean_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_BOOLEAN__OUT__OUT,
               *(bool *)mdata->value_ptr);
}

static struct sol_flow_packet *
persist_boolean_packet_new(const struct persist_data *data)
{
    struct persist_boolean_data *mdata =
        (struct persist_boolean_data *)data;

    return sol_flow_packet_new_boolean(mdata->last_value);
}

static int
persist_boolean_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_boolean_data *mdata = data;
    const struct sol_flow_node_type_persistence_boolean_options *opts =
        (const struct sol_flow_node_type_persistence_boolean_options *)options;

    mdata->base.packet_data_size = sizeof(bool);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = persist_boolean_packet_new;
    mdata->base.packet_data_get_fn = persist_boolean_packet_data_get;
    mdata->base.packet_send_fn = persist_boolean_packet_send;
    mdata->base.node_get_default_fn = persist_boolean_node_get_default;
    mdata->default_value = opts->default_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_byte_data {
    struct persist_data base;
    unsigned char last_value;
    unsigned char default_value;
};

static void *
persist_byte_node_get_default(struct sol_flow_node *node)
{
    struct persist_byte_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value;
}

static int
persist_byte_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_byte(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_byte_packet_send(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_byte_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_BYTE__OUT__OUT,
               *(unsigned char *)mdata->value_ptr);
}

static struct sol_flow_packet *
persist_byte_packet_new(const struct persist_data *data)
{
    struct persist_byte_data *mdata = (struct persist_byte_data *)data;

    return sol_flow_packet_new_byte(mdata->last_value);
}

static int
persist_byte_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_byte_data *mdata = data;
    const struct sol_flow_node_type_persistence_byte_options *opts =
        (const struct sol_flow_node_type_persistence_byte_options *)options;

    mdata->base.packet_data_size = sizeof(unsigned char);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = persist_byte_packet_new;
    mdata->base.packet_data_get_fn = persist_byte_packet_data_get;
    mdata->base.packet_send_fn = persist_byte_packet_send;
    mdata->base.node_get_default_fn = persist_byte_node_get_default;
    mdata->default_value = opts->default_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_irange_data {
    struct persist_data base;
    struct sol_irange last_value;
    struct sol_irange default_value;
};

static void *
persist_irange_node_get_default(struct sol_flow_node *node)
{
    struct persist_irange_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value.val;
}

static int
persist_irange_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_irange(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_irange_packet_send(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_irange_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_INT__OUT__OUT,
               (struct sol_irange *)mdata->value_ptr);
}

static struct sol_flow_packet *
persist_irange_packet_new(const struct persist_data *data)
{
    struct persist_irange_data *mdata =
        (struct persist_irange_data *)data;

    return sol_flow_packet_new_irange(&mdata->last_value);
}

static int
persist_irange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_irange_data *mdata = data;
    const struct sol_flow_node_type_persistence_int_options *opts =
        (const struct sol_flow_node_type_persistence_int_options *)options;

    if (opts->storage_only_val)
        mdata->base.packet_data_size = sizeof(int32_t);
    else
        mdata->base.packet_data_size = sizeof(struct sol_irange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = persist_irange_packet_new;
    mdata->base.packet_data_get_fn = persist_irange_packet_data_get;
    mdata->base.packet_send_fn = persist_irange_packet_send;
    mdata->base.node_get_default_fn = persist_irange_node_get_default;
    mdata->default_value = opts->default_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_drange_data {
    struct persist_data base;
    struct sol_drange last_value;
    struct sol_drange default_value;
};

static void *
persist_drange_node_get_default(struct sol_flow_node *node)
{
    struct persist_drange_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value.val;
}

static int
persist_drange_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_drange(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_drange_packet_send(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_drange_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_FLOAT__OUT__OUT,
               (struct sol_drange *)mdata->value_ptr);
}

static struct sol_flow_packet *
persist_drange_packet_new(const struct persist_data *data)
{
    struct persist_drange_data *mdata =
        (struct persist_drange_data *)data;

    return sol_flow_packet_new_drange(&mdata->last_value);
}

static int
persist_drange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_drange_data *mdata = data;
    const struct sol_flow_node_type_persistence_float_options *opts =
        (const struct sol_flow_node_type_persistence_float_options *)options;

    if (opts->storage_only_val)
        mdata->base.packet_data_size = sizeof(double);
    else
        mdata->base.packet_data_size = sizeof(struct sol_drange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = persist_drange_packet_new;
    mdata->base.packet_data_get_fn = persist_drange_packet_data_get;
    mdata->base.packet_send_fn = persist_drange_packet_send;
    mdata->base.node_get_default_fn = persist_drange_node_get_default;
    mdata->default_value = opts->default_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_string_data {
    struct persist_data base;
    const char *last_value;
    char *default_value;
};

static void *
persist_string_node_get_default(struct sol_flow_node *node)
{
    struct persist_string_data *mdata = sol_flow_node_get_private_data(node);

    return mdata->default_value;
}

static int
persist_string_packet_data_get(const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_string(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_string_packet_send(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_string_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_STRING__OUT__OUT,
               (const char *)mdata->value_ptr);
}

static struct sol_flow_packet *
persist_string_packet_new(const struct persist_data *data)
{
    return sol_flow_packet_new_string(data->value_ptr);
}

static int
persist_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_string_data *mdata = data;
    const struct sol_flow_node_type_persistence_string_options *opts =
        (const struct sol_flow_node_type_persistence_string_options *)options;

    mdata->base.packet_new_fn = persist_string_packet_new;
    mdata->base.packet_data_get_fn = persist_string_packet_data_get;
    mdata->base.packet_send_fn = persist_string_packet_send;
    mdata->base.node_get_default_fn = persist_string_node_get_default;

    if (opts->default_value)
        mdata->default_value = strdup((char *)opts->default_value);

    return persist_open(node, data, opts->storage, opts->name);
}

static void
persist_string_close(struct sol_flow_node *node, void *data)
{
    struct persist_string_data *mdata = data;

    free(mdata->default_value);

    persist_close(node, mdata);
}

#include "persistence-gen.c"
