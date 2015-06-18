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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-unix-socket");

#include "unix-socket.h"
#include "unix-socket-gen.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"
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

static int
socket_read(int fd, void *buf, size_t count)
{
    unsigned int attempts = SOL_UTIL_MAX_READ_ATTEMPTS;
    size_t amount = 0;

    while (attempts && (amount < count)) {
        ssize_t r = read(fd, (char *)buf + amount, count - amount);
        if (r < 0) {
            attempts--;
            if ((errno == EINTR) || (errno == EAGAIN))
                continue;
            else
                return -errno;
        }
        amount += r;
    }

    return amount == count ? 0 : -EAGAIN;
}

/*
 *----------------------- boolean --------------------------
 */
static void
boolean_read_data(void *data, int fd)
{
    struct unix_socket_data *mdata = data;
    bool val;

    if (socket_read(fd, &val, sizeof(val)) < 0)
        return;

    sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_UNIX_SOCKET_BOOLEAN_READER__OUT__OUT, val);
}

static int
boolean_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_boolean_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_boolean_reader_options *)options;

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

    r = sol_flow_packet_get_boolean(packet, &val);
    SOL_INT_CHECK(r, < 0, r);

    return unix_socket_write(mdata->un_socket, &val, sizeof(val));
}

static int
boolean_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_boolean_writer_options *opts =
        (struct sol_flow_node_type_unix_socket_boolean_writer_options *)options;

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
    char val[4096];
    size_t len;
    ssize_t r;

    if (socket_read(fd, &len, sizeof(len)) < 0)
        return;

    while (len > 0) {
        if ((r = socket_read(fd, val, len < (sizeof(val) - 1) ? len : sizeof(val) - 1)) < 0)
            return;

        val[r] = '\0';
        len -= r;
        sol_flow_send_string_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_UNIX_SOCKET_STRING_READER__OUT__OUT, val);
    }
}

static int
string_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct unix_socket_data *mdata = data;
    struct sol_flow_node_type_unix_socket_string_reader_options *opts =
        (struct sol_flow_node_type_unix_socket_string_reader_options *)options;

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

    if (socket_read(fd, &rgb, sizeof(rgb)) < 0)
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

    if (socket_read(fd, &direction_vector, sizeof(direction_vector)) < 0)
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

    if (socket_read(fd, &val, sizeof(val)) < 0)
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

    if (socket_read(fd, &val, sizeof(val)) < 0)
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

    if (socket_read(fd, &val, sizeof(val)) < 0)
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

    mdata->node = node;
    if (opts->server)
        mdata->un_socket = unix_socket_server_new(mdata, opts->path, NULL);
    else
        mdata->un_socket = unix_socket_client_new(mdata, opts->path, NULL);

    SOL_NULL_CHECK(mdata->un_socket, -1);

    return 0;
}
#include "unix-socket-gen.c"
