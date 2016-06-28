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

#include "sol-flow/ipm.h"
#include "sol-flow-internal.h"
#include "sol-ipm.h"
#include "sol-log.h"

#include <sol-util.h>
#include <errno.h>

#define SEND_BLOB(_blob, _id, _node) \
    do { \
        r = sol_ipm_send(_id, _blob); \
        if (r < 0) \
            sol_flow_send_error_packet(_node, -r, "Could not send IPM message"); \
        sol_blob_unref(_blob); \
    } while (0)

struct ipm_data {
    uint32_t id;
};

struct ipm_writer_node_type {
    struct sol_flow_node_type base;
    uint16_t consumed_port;
};

static void
common_reader_close(struct sol_flow_node *node, void *data)
{
    struct ipm_data *mdata = data;

    sol_ipm_set_receiver(mdata->id, NULL, NULL);
}

static void
common_consumed_callback(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;
    struct ipm_writer_node_type *type;

    type = (struct ipm_writer_node_type *)sol_flow_node_get_type(node);

    sol_flow_send_empty_packet(node, type->consumed_port);
}

/*********************** Boolean node *****************************/

static int
boolean_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_boolean_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_BOOLEAN_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_boolean_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
boolean_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    struct sol_blob *blob;
    bool in_value;
    int r;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = SOL_BLOB_NEW_DUP(in_value);
    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
boolean_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_BOOLEAN_READER__OUT__OUT, *(bool *)message->mem);
    sol_blob_unref(message);
}

static int
boolean_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_boolean_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_BOOLEAN_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_boolean_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, boolean_receiver, node);
}

/*********************** String node *****************************/

static int
string_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_string_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_STRING_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_string_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
string_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    const char *in_value;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = sol_blob_new_dup_str(in_value);
    SOL_NULL_CHECK(in_value, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
string_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_STRING_READER__OUT__OUT, message->mem);
    sol_blob_unref(message);
}

static int
string_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_string_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_STRING_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_string_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, string_receiver, node);
}

/*********************** Float node *****************************/

static int
float_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_float_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_FLOAT_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_float_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
float_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    struct sol_blob *blob;
    struct sol_drange in_value;
    int r;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = SOL_BLOB_NEW_DUP(in_value);
    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
float_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_FLOAT_READER__OUT__OUT, message->mem);
    sol_blob_unref(message);
}

static int
float_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_float_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_FLOAT_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_float_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, float_receiver, node);
}

/*********************** Integer node *****************************/

static int
int_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_int_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_INT_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_int_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
int_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    struct sol_blob *blob;
    struct sol_irange in_value;
    int r;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = SOL_BLOB_NEW_DUP(in_value);
    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
int_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_INT_READER__OUT__OUT, message->mem);
    sol_blob_unref(message);
}

static int
int_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_int_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_INT_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_int_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, int_receiver, node);
}

/*********************** RGB node *****************************/

static int
rgb_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_rgb_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_RGB_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_rgb_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
rgb_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    struct sol_rgb in_value;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = SOL_BLOB_NEW_DUP(in_value);
    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
rgb_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_RGB_READER__OUT__OUT, message->mem);
    sol_blob_unref(message);
}

static int
rgb_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_rgb_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_RGB_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_rgb_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, rgb_receiver, node);

    return 0;
}

/*********************** Direction vector node *****************************/

static int
direction_vector_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_direction_vector_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_DIRECTION_VECTOR_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_direction_vector_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
direction_vector_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    struct sol_direction_vector in_value;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = SOL_BLOB_NEW_DUP(in_value);
    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
direction_vector_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_DIRECTION_VECTOR_READER__OUT__OUT, message->mem);
    sol_blob_unref(message);
}

static int
direction_vector_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_direction_vector_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_DIRECTION_VECTOR_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_direction_vector_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, direction_vector_receiver, node);
}

/*********************** Byte node *****************************/

static int
byte_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_byte_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_BYTE_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_byte_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
byte_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    unsigned char in_value;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    blob = SOL_BLOB_NEW_DUP(in_value);
    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
byte_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_BYTE_READER__OUT__OUT,
        *(unsigned char *)message->mem);
    sol_blob_unref(message);
}

static int
byte_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_byte_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_BYTE_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_byte_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, byte_receiver, node);
}

/*********************** Empty node *****************************/

static int
empty_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_empty_writer_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_EMPTY_WRITER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_empty_writer_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    r = sol_ipm_set_consumed_callback(mdata->id, common_consumed_callback, node);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
empty_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct ipm_data *mdata = data;
    struct sol_blob *blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, NULL, 0);
    int r;

    SOL_NULL_CHECK(blob, -ENOMEM);

    SEND_BLOB(blob, mdata->id, node);

    return 0;
}

static void
empty_receiver(void *data, uint32_t id, struct sol_blob *message)
{
    struct sol_flow_node *node = data;

    sol_flow_send_empty_packet(node,
        SOL_FLOW_NODE_TYPE_IPM_EMPTY_READER__OUT__OUT);
    sol_blob_unref(message);
}

static int
empty_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct ipm_data *mdata = data;
    const struct sol_flow_node_type_ipm_empty_reader_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IPM_EMPTY_READER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_ipm_empty_reader_options *)options;

    SOL_INT_CHECK(opts->id, < 1, -EINVAL);
    SOL_INT_CHECK(opts->id, > (int)sol_ipm_get_max_id(), -EINVAL);

    mdata->id = opts->id;

    return sol_ipm_set_receiver(opts->id, empty_receiver, node);
}

#include "ipm-gen.c"
