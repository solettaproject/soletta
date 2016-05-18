/*
 * This file is part of the Soletta Project
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

#include "common.h"

#include "sol-platform.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"
#include <fcntl.h>
#include <unistd.h>

struct stdin_monitor {
    struct sol_flow_node *node;
    uint16_t chunks;
    uint16_t closeds;
};

static struct sol_fd *stdin_watch;

static struct sol_vector stdin_monitors = SOL_VECTOR_INIT(struct stdin_monitor);

static int
stdin_read(struct sol_blob **p_blob, bool *eof)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    struct timespec start = sol_util_timespec_get_current();
    size_t size;
    void *v;
    int ret = 0;

    *eof = false;
    do {
        struct timespec now = sol_util_timespec_get_current();
        struct timespec elapsed;
        ssize_t r;

        sol_util_timespec_sub(&now, &start, &elapsed);
        if (elapsed.tv_sec > 0 ||
            elapsed.tv_nsec > (time_t)CHUNK_MAX_TIME_NS)
            break;

        r = sol_util_fill_buffer(STDIN_FILENO, &buf, CHUNK_READ_SIZE);
        if (r == 0) {
            *eof = true;
            break;
        } else if (r < 0) {
            /* Not a problem if failed because buffer could not be increased */
            if (r != -ENOMEM)
                ret = -errno;
            break;
        }
    } while (1);

    if (ret < 0 && ret != -EAGAIN) {
        sol_buffer_fini(&buf);
        *p_blob = NULL;
        return ret;
    }

    v = sol_buffer_steal(&buf, &size);
    *p_blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, v, size);
    SOL_NULL_CHECK_GOTO(*p_blob, blob_error);
    return 0;

blob_error:
    sol_buffer_fini(&buf);
    return -ENOMEM;
}

static bool
stdin_monitor_in_use(const struct stdin_monitor *m)
{
    return m->chunks > 0 || m->closeds > 0;
}

static bool
stdin_watch_cb(void *data, int fd, uint32_t active_flags)
{
    struct stdin_monitor *m;
    struct sol_blob *blob = NULL;
    uint16_t i;
    int err = 0;
    bool eof = true;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = stdin_read(&blob, &eof);

    SOL_VECTOR_FOREACH_IDX (&stdin_monitors, m, i) {
        if (!stdin_monitor_in_use(m))
            continue;


        if (m->chunks && blob)
            sol_flow_send_blob_packet(m->node, SOL_FLOW_NODE_TYPE_PROCESS_STDIN__OUT__OUT, blob);

        if (err < 0)
            sol_flow_send_error_packet(m->node, -err, "%s", sol_util_strerrora(-err));

        if (m->closeds && (err < 0 || eof))
            sol_flow_send_boolean_packet(m->node, SOL_FLOW_NODE_TYPE_PROCESS_STDIN__OUT__CLOSED, true);
    }

    if (blob)
        sol_blob_unref(blob);

    if (eof || err < 0) {
        stdin_watch = NULL;
        return false;
    }

    return true;
}

static int
stdin_watch_start(void)
{
    if (stdin_watch)
        return 0;

    if (sol_util_fd_set_flag(STDIN_FILENO, O_NONBLOCK) < 0)
        return -errno;

    stdin_watch = sol_fd_add(STDIN_FILENO, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR,
        stdin_watch_cb, NULL);
    SOL_NULL_CHECK(stdin_watch, -ENOMEM);
    return 0;
}

static void
stdin_watch_stop(void)
{
    if (!stdin_watch)
        return;
    sol_fd_del(stdin_watch);
    stdin_watch = NULL;
}

static void
stdin_monitor_del(uint16_t idx)
{
    sol_vector_del(&stdin_monitors, idx);
    if (stdin_monitors.len == 0)
        stdin_watch_stop();
}

static uint16_t
stdin_monitor_find(const struct sol_flow_node *node)
{
    struct stdin_monitor *m;
    uint16_t i;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&stdin_monitors, m, i) {
        if (m->node == node)
            return i;
    }
    return UINT16_MAX;
}

static int
stdin_common_connect(struct sol_flow_node *node, struct stdin_monitor **p_monitor)
{
    uint16_t i;
    int ret;

    i = stdin_monitor_find(node);
    if (i < UINT16_MAX) {
        *p_monitor = sol_vector_get(&stdin_monitors, i);
        return 0;
    }

    *p_monitor = sol_vector_append(&stdin_monitors);
    SOL_NULL_CHECK(*p_monitor, -ENOMEM);

    (*p_monitor)->node = node;
    (*p_monitor)->chunks = 0;
    (*p_monitor)->closeds = 0;

    ret = stdin_watch_start();
    if (ret < 0) {
        sol_vector_del_last(&stdin_monitors);
        *p_monitor = NULL;
    }

    return ret;
}

int
process_stdin_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct stdin_monitor *m;
    int ret;

    ret = stdin_common_connect(node, &m);
    if (ret < 0)
        return ret;

    m->chunks++;
    return 0;
}

int
process_stdin_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct stdin_monitor *m;
    uint16_t i;

    i = stdin_monitor_find(node);
    if (i == UINT16_MAX)
        return -ENOENT;

    m = sol_vector_get(&stdin_monitors, i);
    m->chunks--;
    if (!stdin_monitor_in_use(m))
        stdin_monitor_del(i);
    return 0;
}

int
process_stdin_closed_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct stdin_monitor *m;
    int ret, flags;

    ret = stdin_common_connect(node, &m);
    if (ret < 0)
        return ret;

    flags = fcntl(STDIN_FILENO, F_GETFL);
    sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_PROCESS_STDIN__OUT__CLOSED, (flags < 0));

    m->closeds++;
    return 0;
}

int
process_stdin_closed_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct stdin_monitor *m;
    uint16_t i;

    i = stdin_monitor_find(node);
    if (i == UINT16_MAX)
        return -ENOENT;

    m = sol_vector_get(&stdin_monitors, i);
    m->closeds--;
    if (!stdin_monitor_in_use(m))
        stdin_monitor_del(i);
    return 0;
}
