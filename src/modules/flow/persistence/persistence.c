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

#include "sol-flow/persistence.h"

#include "sol-buffer.h"
#include "sol-flow.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util-internal.h"
#include "sol-flow-internal.h"

#ifdef USE_FILESYSTEM
#include "sol-fs-storage.h"
#endif

#ifdef USE_EFIVARS
#include "sol-efivarfs-storage.h"
#endif

#ifdef USE_MEMMAP
#include "sol-memmap-storage.h"
#endif

struct storage_fn {
    int (*write)(const char *name, struct sol_blob *blob,
        void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
        const void *data);
    int (*read)(const char *name, struct sol_buffer *buffer);
};

struct persist_data {
    const struct storage_fn *storage;
    void *value_ptr;
    char *name;
    char *fs_dir_path;
    size_t packet_data_size;
};

struct write_cb_data {
    struct persist_data *mdata;
    struct sol_flow_node *node;
    bool send_packet;
};

struct persistence_node_type {
    struct sol_flow_node_type base;
    int (*get_packet_data)(size_t packet_data_size, const struct sol_flow_packet *packet, void *value_ptr);
    int (*send_packet)(struct sol_flow_node *node);
    void *(*get_default)(struct sol_flow_node *node);
};

#ifdef USE_FILESYSTEM
static const struct storage_fn fs_fn = {
    .write = sol_fs_write_raw,
    .read = sol_fs_read_raw
};
#endif

#ifdef USE_EFIVARS
static const struct storage_fn efivars_fn = {
    .write = sol_efivars_write_raw,
    .read = sol_efivars_read_raw
};
#endif

#ifdef USE_MEMMAP
static const struct storage_fn memmap_fn = {
    .write = sol_memmap_write_raw,
    .read = sol_memmap_read_raw
};
#endif

static const struct sol_str_table_ptr storage_fn_table[] = {
#ifdef USE_FILESYSTEM
    SOL_STR_TABLE_PTR_ITEM("fs", &fs_fn),
#endif
#ifdef USE_EFIVARS
    SOL_STR_TABLE_PTR_ITEM("efivars", &efivars_fn),
#endif
#ifdef USE_MEMMAP
    SOL_STR_TABLE_PTR_ITEM("memmap", &memmap_fn),
#endif
    { }
};

static bool
update_node_value(struct persist_data *mdata, void *data, size_t len)
{
    /* No packet_data_size means dynamic content (string). Let's reallocate if needed */
    if (!mdata->packet_data_size) {
        if (!mdata->value_ptr || strlen(mdata->value_ptr) + 1 < len) {
            void *tmp = realloc(mdata->value_ptr, len);
            SOL_NULL_CHECK(tmp, false);
            mdata->value_ptr = tmp;
        }
    }
    memcpy(mdata->value_ptr, data, len);

    return true;
}

static void
write_cb(void *data, const char *name, struct sol_blob *blob, int status)
{
    struct write_cb_data *cb_data = data;
    struct persist_data *mdata = cb_data->mdata;
    struct sol_flow_node *node = cb_data->node;
    const struct persistence_node_type *type;

    type = (const struct persistence_node_type *)
        sol_flow_node_get_type(node);

    if (status < 0) {
        if (status == -ECANCELED)
            SOL_INF("Writing to [%s] superseeded by another write", name);
        else
            SOL_WRN("Could not write [%s], error: %s", name,
                sol_util_strerrora(-status));

        goto end;
    }

    if (update_node_value(mdata, blob->mem, blob->size)) {
        if (cb_data->send_packet)
            type->send_packet(node);
    }

end:
    free(cb_data);
}

static int
storage_write(struct persist_data *mdata, void *data, size_t size, struct sol_flow_node *node, bool send_packet)
{
    void *cp_data = NULL;
    struct write_cb_data *cb_data = NULL;
    struct sol_blob *blob = NULL;
    int r;

    cp_data = malloc(size);
    SOL_NULL_CHECK(cp_data, -ENOMEM);

    memcpy(cp_data, data, size);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, cp_data, size);
    SOL_NULL_CHECK_GOTO(blob, error);

    cb_data = malloc(sizeof(struct write_cb_data));
    SOL_NULL_CHECK_GOTO(cb_data, error);

    cb_data->mdata = mdata;
    cb_data->node = node;
    cb_data->send_packet = send_packet;

    r = mdata->storage->write(mdata->name, blob, write_cb, cb_data);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_blob_unref(blob);

    return r;

error:
    if (blob)
        sol_blob_unref(blob);
    free(cb_data);

    return -ENOMEM;
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
persist_do(struct persist_data *mdata, struct sol_flow_node *node, void *value,
    bool send_packet)
{
    size_t size = 0;
    int r;

    if (mdata->packet_data_size)
        size = mdata->packet_data_size;
    else
        size = strlen(value) + 1;  //To include the null terminating char

    if (mdata->value_ptr) {
        if (mdata->packet_data_size)
            r = memcmp(mdata->value_ptr, value, mdata->packet_data_size);
        else
            r = strcmp(mdata->value_ptr, value);
        if (r == 0)
            return 0;
    }

    r = storage_write(mdata, value, size, node, send_packet);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
persist_reset(struct persist_data *mdata, struct sol_flow_node *node)
{
    void *value;
    size_t size;
    const struct persistence_node_type *type;

    type = (const struct persistence_node_type *)
        sol_flow_node_get_type(node);

    value = type->get_default(node);

    if (mdata->packet_data_size)
        size = mdata->packet_data_size;
    else
        size = strlen(value) + 1;

    if (update_node_value(mdata, value, size))
        type->send_packet(node);

    return persist_do(mdata, node, value, false);
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
    const struct persistence_node_type *type;

    type = (const struct persistence_node_type *)
        sol_flow_node_get_type(node);

    if (mdata->packet_data_size) {
        /* Using alloca() is OK here since packet_data_size is always
         * a sizeof() of a fixed struct. */
        value = alloca(mdata->packet_data_size);
        r = type->get_packet_data(mdata->packet_data_size, packet, value);
    } else
        r = type->get_packet_data(mdata->packet_data_size, packet, &value);

    SOL_INT_CHECK(r, < 0, r);

    return persist_do(mdata, node, value, true);
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
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const struct persistence_node_type *type;
    struct sol_str_slice storage_slice;
    struct persist_data *mdata = data;
    int r;

    type = (const struct persistence_node_type *)
        sol_flow_node_get_type(node);

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
    r = storage_read(mdata, &buf);
    if (mdata->packet_data_size) {
        if (r >= 0) {
            /* entry's total size may be bigger than actual
             * packet_data_size (think bit fields). the useful data
             * with be the leading bytes, on all cases */
            r = sol_buffer_remove_data(&buf, mdata->packet_data_size,
                buf.used - mdata->packet_data_size);
            if (r >= 0)
                memcpy(mdata->value_ptr, buf.data, buf.used);
        }
    } else {
        if (r >= 0) {
            /* avoid reads of malformed strings */
            if (!memchr(buf.data, '\0', buf.used))
                r = -EINVAL;
            else
                mdata->value_ptr = sol_buffer_steal(&buf, NULL);
        }
    }
    sol_buffer_fini(&buf);

    if (r < 0) {
        SOL_INF("Error reading previous storage (%s). Sending default value "
            "on output port.", sol_util_strerrora(-r));
        r = persist_reset(mdata, node);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        return r;
    }
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return type->send_packet(node);

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
persist_boolean_get_default(struct sol_flow_node *node)
{
    struct persist_boolean_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value;
}

static int
persist_boolean_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_bool(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_boolean_send_packet(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_bool_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_BOOLEAN__OUT__OUT,
               *(bool *)mdata->value_ptr);
}

static int
persist_boolean_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_boolean_data *mdata = data;
    const struct sol_flow_node_type_persistence_boolean_options *opts =
        (const struct sol_flow_node_type_persistence_boolean_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_BOOLEAN_OPTIONS_API_VERSION, -EINVAL);

    mdata->base.packet_data_size = sizeof(bool);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->default_value = opts->default_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_byte_data {
    struct persist_data base;
    unsigned char last_value;
    unsigned char default_value;
};

static void *
persist_byte_get_default(struct sol_flow_node *node)
{
    struct persist_byte_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value;
}

static int
persist_byte_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_byte(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_byte_send_packet(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_byte_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_BYTE__OUT__OUT,
               *(unsigned char *)mdata->value_ptr);
}

static int
persist_byte_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_byte_data *mdata = data;
    const struct sol_flow_node_type_persistence_byte_options *opts =
        (const struct sol_flow_node_type_persistence_byte_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_BYTE_OPTIONS_API_VERSION, -EINVAL);

    mdata->base.packet_data_size = sizeof(unsigned char);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->default_value = opts->default_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_irange_data {
    struct persist_data base;
    struct sol_irange last_value;
    struct sol_irange default_value;
    bool store_only_val;
};

static void *
persist_irange_get_default(struct sol_flow_node *node)
{
    struct persist_irange_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value;
}

static int
persist_irange_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r;

    if (packet_data_size == sizeof(struct sol_irange))
        r = sol_flow_packet_get_irange(packet, value_ptr);
    else
        r = sol_flow_packet_get_irange_value(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_irange_send_packet(struct sol_flow_node *node)
{
    struct persist_irange_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_irange *val = mdata->base.value_ptr;

    if (mdata->store_only_val || (!val->step && !val->min && !val->max)) {
        struct sol_irange value = {
            .val = *(int32_t *)mdata->base.value_ptr,
            .step = mdata->default_value.step,
            .min = mdata->default_value.min,
            .max = mdata->default_value.max
        };

        return sol_flow_send_irange_packet(node,
            SOL_FLOW_NODE_TYPE_PERSISTENCE_INT__OUT__OUT,
            &value);
    }

    return sol_flow_send_irange_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_INT__OUT__OUT, val);
}

static int
persist_irange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_irange_data *mdata = data;
    const struct sol_flow_node_type_persistence_int_options *opts =
        (const struct sol_flow_node_type_persistence_int_options *)options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_INT_OPTIONS_API_VERSION, -EINVAL);

    if (opts->store_only_val)
        mdata->base.packet_data_size = sizeof(int32_t);
    else
        mdata->base.packet_data_size = sizeof(struct sol_irange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->store_only_val = opts->store_only_val;

    r = sol_irange_compose(&opts->default_value_spec, opts->default_value,
        &mdata->default_value);
    SOL_INT_CHECK(r, < 0, r);

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_drange_data {
    struct persist_data base;
    struct sol_drange last_value;
    struct sol_drange default_value;
    bool store_only_val;
};

static void *
persist_drange_get_default(struct sol_flow_node *node)
{
    struct persist_drange_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_value;
}

static int
persist_drange_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r;

    if (packet_data_size == sizeof(struct sol_drange))
        r = sol_flow_packet_get_drange(packet, value_ptr);
    else
        r = sol_flow_packet_get_drange_value(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_drange_send_packet(struct sol_flow_node *node)
{
    struct persist_drange_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange *val = mdata->base.value_ptr;
    bool no_defaults = sol_util_double_eq(val->step, 0) &&
        sol_util_double_eq(val->min, 0) &&
        sol_util_double_eq(val->max, 0);

    if (mdata->store_only_val || no_defaults) {
        struct sol_drange value = {
            .val = *(double *)mdata->base.value_ptr,
            .step = mdata->default_value.step,
            .min = mdata->default_value.min,
            .max = mdata->default_value.max
        };

        return sol_flow_send_drange_packet(node,
            SOL_FLOW_NODE_TYPE_PERSISTENCE_FLOAT__OUT__OUT,
            &value);
    }

    return sol_flow_send_drange_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_FLOAT__OUT__OUT, val);
}

static int
persist_drange_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_drange_data *mdata = data;
    const struct sol_flow_node_type_persistence_float_options *opts =
        (const struct sol_flow_node_type_persistence_float_options *)options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_FLOAT_OPTIONS_API_VERSION, -EINVAL);

    if (opts->store_only_val)
        mdata->base.packet_data_size = sizeof(double);
    else
        mdata->base.packet_data_size = sizeof(struct sol_drange);
    mdata->base.value_ptr = &mdata->last_value;
    mdata->store_only_val = opts->store_only_val;

    r = sol_drange_compose(&opts->default_value_spec, opts->default_value,
        &mdata->default_value);
    SOL_INT_CHECK(r, < 0, r);

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_string_data {
    struct persist_data base;
    const char *last_value;
    char *default_value;
};

static void *
persist_string_get_default(struct sol_flow_node *node)
{
    struct persist_string_data *mdata = sol_flow_node_get_private_data(node);

    return mdata->default_value;
}

static int
persist_string_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet,
    void *value_ptr)
{
    int r = sol_flow_packet_get_string(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
persist_string_send_packet(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_string_packet
               (node, SOL_FLOW_NODE_TYPE_PERSISTENCE_STRING__OUT__OUT,
               (const char *)mdata->value_ptr);
}

static int
persist_string_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_string_data *mdata = data;
    const struct sol_flow_node_type_persistence_string_options *opts =
        (const struct sol_flow_node_type_persistence_string_options *)options;
    int r;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_STRING_OPTIONS_API_VERSION, -EINVAL);

    mdata->default_value = strdup(opts->default_value);
    SOL_NULL_CHECK(mdata->default_value, -ENOMEM);

    r = persist_open(node, data, opts->storage, opts->name);
    if (r < 0)
        free(mdata->default_value);

    return r;
}

static void
persist_string_close(struct sol_flow_node *node, void *data)
{
    struct persist_string_data *mdata = data;

    free(mdata->default_value);

    persist_close(node, mdata);
}

struct persist_rgb_data {
    struct persist_data base;
    struct sol_rgb default_rgb;
    struct sol_rgb last_value;
};

static int
persist_rgb_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet, void *value_ptr)
{
    int r = sol_flow_packet_get_rgb(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
persist_rgb_send_packet(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_RGB__OUT__OUT,
        (struct sol_rgb *)mdata->value_ptr);
}

static void *
persist_rgb_get_default(struct sol_flow_node *node)
{
    struct persist_rgb_data *mdata = sol_flow_node_get_private_data(node);

    return &mdata->default_rgb;
}

static int
persist_rgb_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_rgb_data *mdata = data;
    const struct sol_flow_node_type_persistence_rgb_options *opts =
        (const struct sol_flow_node_type_persistence_rgb_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_RGB_OPTIONS_API_VERSION, -EINVAL);

    mdata->base.packet_data_size = sizeof(struct sol_rgb);
    mdata->default_rgb = opts->default_value;
    mdata->base.value_ptr = &mdata->last_value;

    return persist_open(node, data, opts->storage, opts->name);
}

struct persist_direction_vector_data {
    struct persist_data base;
    struct sol_direction_vector default_direction_vector;
    struct sol_direction_vector last_value;
};

static int
persist_direction_vector_get_packet_data(size_t packet_data_size,
    const struct sol_flow_packet *packet, void *value_ptr)
{
    int r = sol_flow_packet_get_direction_vector(packet, value_ptr);

    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
persist_direction_vector_send_packet(struct sol_flow_node *node)
{
    struct persist_data *mdata = sol_flow_node_get_private_data(node);

    return sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_DIRECTION_VECTOR__OUT__OUT,
        (struct sol_direction_vector *)mdata->value_ptr);
}

static void *
persist_direction_vector_get_default(struct sol_flow_node *node)
{
    struct persist_direction_vector_data *mdata =
        sol_flow_node_get_private_data(node);

    return &mdata->default_direction_vector;
}

static int
persist_direction_vector_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct persist_direction_vector_data *mdata = data;
    const struct sol_flow_node_type_persistence_direction_vector_options *opts =
        (const struct sol_flow_node_type_persistence_direction_vector_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PERSISTENCE_DIRECTION_VECTOR_OPTIONS_API_VERSION, -EINVAL);

    mdata->base.packet_data_size = sizeof(struct sol_direction_vector);
    mdata->default_direction_vector = opts->default_value;
    mdata->base.value_ptr = &mdata->last_value;

    return persist_open(node, data, opts->storage, opts->name);
}

#include "persistence-gen.c"
