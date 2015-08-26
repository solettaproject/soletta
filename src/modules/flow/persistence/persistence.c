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

#include "persistence-gen.h"

#include "sol-buffer.h"
#include "sol-flow.h"
#include "sol-util.h"

#include "fs-storage.h"
#include "efivarfs-storage.h"

struct persist_data {
    void *value_ptr;

    char *name;
    char *fs_dir_path;
    char *storage;

    struct sol_flow_packet *(*packet_new_fn)(const struct persist_data *data);
    int (*packet_data_get_fn)(const struct sol_flow_packet *packet, void *value_ptr);
    int (*packet_send_fn)(struct sol_flow_node *node);

    int (*storage_write)(const char *name, struct sol_buffer *buffer);
    int (*storage_read)(const char *name, struct sol_buffer *buffer);
    int (*storage_get_size)(const char *name, size_t *size);

    size_t packet_data_size;
};

static int
storage_write(struct persist_data *mdata, void *data, size_t size)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(data, size,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    buf.used = size;

    return mdata->storage_write(mdata->name, &buf);
}

static int
storage_read(struct persist_data *mdata, void *data, size_t size)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(data, size,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    return mdata->storage_read(mdata->name, &buf);
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
persist_open(struct sol_flow_node *node,
    void *data,
    const char *storage,
    const char *name)
{
    struct persist_data *mdata = data;
    size_t size;
    int r;

    if (!storage || *storage == '\0') {
        SOL_WRN("Must define a storage type");
        return -EINVAL;
    }

    if (streq(storage, "fs")) {
        mdata->storage_read = fs_read;
        mdata->storage_write = fs_write;
        mdata->storage_get_size = fs_get_size;
    } else if (streq(storage, "efivars")) {
        mdata->storage_read = efivars_read;
        mdata->storage_write = efivars_write;
        mdata->storage_get_size = efivars_get_size;
    } else {
        SOL_WRN("Invalid storage [%s]", storage);
        return -EINVAL;
    }

    mdata->name = strdup(name);
    SOL_NULL_CHECK(mdata->name, -ENOMEM);

    r = mdata->storage_get_size(mdata->name, &size);
    if (r == -ENOENT) {
        /* No file. So, no previous value saved. Do nothing */
        /* TODO should we provide a default? A NULL port? */
        return 0;
    }
    SOL_INT_CHECK_GOTO(r, < 0, err);

    /* a zero packet_data_size means dynamic size content */
    if (mdata->packet_data_size) {
        if (size < mdata->packet_data_size)
            return 0;

        r = storage_read(mdata, mdata->value_ptr, mdata->packet_data_size);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    } else {
        mdata->value_ptr = calloc(1, size + 1);
        SOL_NULL_CHECK_GOTO(mdata->value_ptr, err_mem);

        r = storage_read(mdata, mdata->value_ptr, size);
        SOL_INT_CHECK(r, < 0, r);
        ((char *)mdata->value_ptr)[size] = '\0';
    }

    return mdata->packet_send_fn(node);

err_mem:
    r = -EINVAL;
err:
    persist_close(node, mdata);
    return r;
}

static int
persist_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct persist_data *mdata = data;
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

    if (mdata->packet_data_size) {
        r = storage_write(mdata, value, mdata->packet_data_size);
    } else {
        size = strlen(value_ptr) + 1;
        r = storage_write(mdata, value_ptr, size);
    }
    SOL_INT_CHECK(r, < 0, r);

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

    return mdata->packet_send_fn(node);
}

struct persist_boolean_data {
    struct persist_data base;
    bool last_value;
};

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

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_byte_data {
    struct persist_data base;
    unsigned char last_value;
};

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

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_irange_data {
    struct persist_data base;
    struct sol_irange last_value;
};

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

    mdata->base.packet_data_size = sizeof(struct sol_irange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = persist_irange_packet_new;
    mdata->base.packet_data_get_fn = persist_irange_packet_data_get;
    mdata->base.packet_send_fn = persist_irange_packet_send;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_drange_data {
    struct persist_data base;
    struct sol_drange last_value;
};

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
    const struct sol_flow_node_type_persistence_int_options *opts =
        (const struct sol_flow_node_type_persistence_int_options *)options;

    mdata->base.packet_data_size = sizeof(struct sol_drange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->base.packet_new_fn = persist_drange_packet_new;
    mdata->base.packet_data_get_fn = persist_drange_packet_data_get;
    mdata->base.packet_send_fn = persist_drange_packet_send;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_string_data {
    struct persist_data base;
    const char *last_value;
};

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
    const struct sol_flow_node_type_persistence_int_options *opts =
        (const struct sol_flow_node_type_persistence_int_options *)options;

    mdata->base.packet_new_fn = persist_string_packet_new;
    mdata->base.packet_data_get_fn = persist_string_packet_data_get;
    mdata->base.packet_send_fn = persist_string_packet_send;

    return persist_open(node, data, opts->storage, opts->name);
}


#include "persistence-gen.c"
