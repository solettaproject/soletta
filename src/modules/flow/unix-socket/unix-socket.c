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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unix-socket.h"
#include "sol-flow/unix-socket.h"
#include "sol-flow-internal.h"
#include "sol-buffer.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"
#include "sol-vector.h"

struct unix_socket_data {
    struct sol_flow_node *node;
    struct unix_socket *un_socket;
};

static void
common_close(struct sol_flow_node *node, void *data)
{
    struct unix_socket_data *mdata = data;

    unix_socket_del(mdata->un_socket);
}

static ssize_t
fill_buffer(const int fd, void *buf, const size_t size)
{
    struct sol_buffer buffer = SOL_BUFFER_INIT_FLAGS(buf, size,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    ssize_t ret;

    ret = sol_util_fill_buffer_exactly(fd, &buffer, size);
    if (ret < 0)
        return ret;

    return ret;
}

#define FILL_BUFFER(_fd, _buf) fill_buffer((_fd), &(_buf), sizeof(_buf))

/*
 *----------------------- boolean --------------------------
 */
static void
boolean_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    bool val;

    if (FILL_BUFFER(fd, val) < 0)
        return;

    sol_flow_send_bool_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_BOOLEAN_READER__OUT__OUT, val);
}

static int
boolean_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_boolean_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_boolean_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_BOOLEAN_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, boolean_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, boolean_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
boolean_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    bool val;
    int r;

    r = sol_flow_packet_get_bool(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &val, sizeof(val));
}

static int
boolean_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_boolean_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_boolean_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_API_CHECK(options,
        SOL_FLOW_NODE_OPTIONS_API_VERSION, -EINVAL);
    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_BOOLEAN_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

/*
 *----------------------- string --------------------------
 */
static void
string_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    char *str;
    size_t len;
    int r;

    if (FILL_BUFFER(fd, len) < 0)
        return;

    r = sol_util_fill_buffer_exactly(fd, &buf, len);
    if (r < 0)
        goto end;

    str = sol_buffer_steal(&buf, NULL);
    sol_flow_send_string_take_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_STRING_READER__OUT__OUT, str);

end:
    sol_buffer_fini(&buf);
}

static int
string_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_string_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_string_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_STRING_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, string_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, string_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
string_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    const char *val;
    size_t len;
    int r;

    r = sol_flow_packet_get_string(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    len = strlen(val);
    if (unix_socket_write(mdata->un_socket, &len, sizeof(len)) < 0) {
        SOL_WRN("Failed to write the string length");
        return -1;
    }

    return unix_socket_write(mdata->un_socket, val, len);
}

static int
string_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_string_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_string_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_STRING_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

/*
 *----------------------- rgb --------------------------
 */
static void
rgb_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    struct sol_rgb rgb;

    if (FILL_BUFFER(fd, rgb) < 0)
        return;

    sol_flow_send_rgb_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_RGB_READER__OUT__OUT, &rgb);
}

static int
rgb_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_rgb_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_rgb_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_RGB_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, rgb_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, rgb_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
rgb_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    struct sol_rgb rgb;
    int r;

    r = sol_flow_packet_get_rgb(packet, &rgb);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &rgb, sizeof(rgb));
}

static int
rgb_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_rgb_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_rgb_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_RGB_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

/*
 *----------------------- direction_vector --------------------------
 */
static void
direction_vector_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    struct sol_direction_vector direction_vector;

    if (FILL_BUFFER(fd, direction_vector) < 0)
        return;

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_DIRECTION_VECTOR_READER__OUT__OUT, &direction_vector);
}

static int
direction_vector_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_direction_vector_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_direction_vector_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_DIRECTION_VECTOR_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, direction_vector_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, direction_vector_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
direction_vector_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    struct sol_direction_vector direction_vector;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &direction_vector);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &direction_vector, sizeof(direction_vector));
}

static int
direction_vector_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_direction_vector_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_direction_vector_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_DIRECTION_VECTOR_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

/*
 *----------------------- byte --------------------------
 */
static void
byte_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    uint8_t val;

    if (FILL_BUFFER(fd, val) < 0)
        return;

    sol_flow_send_byte_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_BYTE_READER__OUT__OUT, val);
}

static int
byte_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_byte_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_byte_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_DIRECTION_VECTOR_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, byte_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, byte_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
byte_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    uint8_t val;
    int r;

    r = sol_flow_packet_get_byte(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &val, sizeof(val));
}

static int
byte_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_byte_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_byte_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_BYTE_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

/*
 *----------------------- int --------------------------
 */
static void
int_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    struct sol_irange val;

    if (FILL_BUFFER(fd, val) < 0)
        return;

    sol_flow_send_irange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_INT_READER__OUT__OUT, &val);
}

static int
int_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_int_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_int_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_INT_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, int_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, int_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
int_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    struct sol_irange val;
    int r;

    r = sol_flow_packet_get_irange(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &val, sizeof(val));
}

static int
int_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_int_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_int_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_INT_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

/*
 *----------------------- float --------------------------
 */
static void
float_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    double val;

    if (FILL_BUFFER(fd, val) < 0)
        return;

    sol_flow_send_drange_value_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_FLOAT_READER__OUT__OUT, val);
}

static int
float_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_float_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_float_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_FLOAT_READER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, float_read_data);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, float_read_data);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}

static int
float_writer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct unix_socket_data *mdata = data;
    int r;
    double val;

    r = sol_flow_packet_get_drange_value(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &val, sizeof(val));
}

static int
float_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_float_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_float_writer_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_FLOAT_WRITER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}
#include "unix-socket-gen.c"
