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

#include "common.h"

#include "sol-platform.h"
#include "sol-util.h"

#include <fcntl.h>
#include <unistd.h>


struct write_data {
    struct sol_blob *blob;
    size_t offset;
};

struct output_data {
    uint16_t port;
    int fd;
    struct sol_vector data;
    struct sol_ptr_vector monitors;
    struct sol_fd *watch;
};

static struct output_data stderr_data = {
    .port = SOL_FLOW_NODE_TYPE_PROCESS_STDERR__OUT__CLOSED,
    .fd = STDERR_FILENO,
    .data = SOL_VECTOR_INIT(struct write_data),
    .monitors = SOL_PTR_VECTOR_INIT,
};

static struct output_data stdout_data = {
    .port = SOL_FLOW_NODE_TYPE_PROCESS_STDOUT__OUT__CLOSED,
    .fd = STDOUT_FILENO,
    .data = SOL_VECTOR_INIT(struct write_data),
    .monitors = SOL_PTR_VECTOR_INIT,
};

static int
output_write(struct output_data *output)
{
    int ret = 0;
    struct timespec start = sol_util_timespec_get_current();

    while (output->data.len) {
        struct timespec now = sol_util_timespec_get_current();
        struct timespec elapsed;
        struct write_data *d = sol_vector_get(&output->data, 0);
        ssize_t r;

        sol_util_timespec_sub(&now, &start, &elapsed);
        if (elapsed.tv_sec > 0 ||
            elapsed.tv_nsec > (time_t)CHUNK_MAX_TIME_NS)
            break;

        r = write(output->fd, (uint8_t *)d->blob->mem + d->offset, d->blob->size - d->offset);
        if (r > 0) {
            d->offset += r;
        } else if (r < 0) {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else {
                ret = -errno;
                break;
            }
        }

        if (d->blob->size <= d->offset) {
            sol_blob_unref(d->blob);
            sol_vector_del(&output->data, 0);
        }
    }

    return ret;
}

static bool
watch_cb(void *data, int fd, unsigned int active_flags)
{
    struct output_data *output = data;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = output_write(output);

    if (err < 0) {
        uint16_t i;
        struct sol_flow_node *n;
        struct write_data *w;

        SOL_PTR_VECTOR_FOREACH_IDX (&output->monitors, n, i) {
            sol_flow_send_error_packet(n, -err, "%s", sol_util_strerrora(-err));
            sol_flow_send_boolean_packet(n, output->port, true);
        }

        SOL_VECTOR_FOREACH_IDX (&output->data, w, i)
            sol_blob_unref(w->blob);

        sol_vector_clear(&output->data);
    }

    if (output->data.len == 0) {
        output->watch = NULL;
        return false;
    }

    return true;
}

static int
watch_start(struct output_data *output)
{
    int flags;

    if (output->watch)
        return 0;

    flags = fcntl(output->fd, F_GETFL);
    SOL_INT_CHECK(flags, < 0, -errno);
    flags |= O_NONBLOCK;
    fcntl(output->fd, F_SETFL, flags);

    output->watch = sol_fd_add(output->fd, SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR, watch_cb, output);
    SOL_NULL_CHECK(output->watch, -ENOMEM);

    return 0;
}

static int
common_process(struct output_data *output, const struct sol_flow_packet *packet)
{
    struct sol_blob *blob;
    struct write_data *d;
    int ret;

    process_log_init();

    ret = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(ret, < 0, ret);

    d = sol_vector_append(&output->data);
    SOL_NULL_CHECK(d, -ENOMEM);

    d->blob = sol_blob_ref(blob);
    SOL_NULL_CHECK_GOTO(d->blob, err);

    d->offset = 0;

    if (watch_start(output) < 0)
        goto watch_err;

    return 0;

watch_err:
    sol_blob_unref(d->blob);
err:
    sol_vector_del(&output->data, output->data.len - 1);
    return -ENOMEM;
}

static int
common_open(struct output_data *output, struct sol_flow_node *node)
{
    process_log_init();

    if (sol_ptr_vector_append(&output->monitors, node) < 0)
        return -ENOMEM;

    return 0;
}

static void
common_close(struct output_data *output, const struct sol_flow_node *node)
{
    struct sol_flow_node *n;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&output->monitors, n, i) {
        if (n == node) {
            sol_ptr_vector_del(&output->monitors, i);
            break;
        }
    }

    if (!sol_ptr_vector_get_len(&output->monitors)) {
        struct write_data *w;

        if (output->watch) {
            sol_fd_del(output->watch);
            output->watch = NULL;
        }

        SOL_VECTOR_FOREACH_IDX (&output->data, w, i)
            sol_blob_unref(w->blob);

        sol_vector_clear(&output->data);
    }
}

static int
common_connect(struct output_data *output, struct sol_flow_node *node)
{
  int flags;
  flags = fcntl(output->fd, F_GETFL);
  return sol_flow_send_boolean_packet(node, output->port, (flags < 0));
}

int
process_stdout_closed_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    return common_connect(&stdout_data, node);
}

int
process_stdout_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return common_process(&stdout_data, packet);
}

int
process_stdout_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    return common_open(&stdout_data, node);
}

void
process_stdout_close(struct sol_flow_node *node, void *data)
{
    common_close(&stdout_data, node);
}

int
process_stderr_closed_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    return common_connect(&stderr_data, node);
}

int
process_stderr_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return common_process(&stderr_data, packet);
}

int
process_stderr_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    return common_open(&stderr_data, node);
}

void
process_stderr_close(struct sol_flow_node *node, void *data)
{
    common_close(&stderr_data, node);
}
