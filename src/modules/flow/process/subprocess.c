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

#include "common.h"

#include "sol-platform-linux.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"
#include "sol-flow-internal.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

struct write_data {
    struct sol_blob *blob;
    size_t offset;
};

static int
out_write(struct subprocess_data *mdata)
{
    int ret = 0;
    struct timespec start = sol_util_timespec_get_current();

    while (mdata->write_data.len) {
        struct timespec now = sol_util_timespec_get_current();
        struct timespec elapsed;
        struct write_data *w = sol_vector_get(&mdata->write_data, 0);
        ssize_t r;

        sol_util_timespec_sub(&now, &start, &elapsed);
        if (elapsed.tv_sec > 0 ||
            elapsed.tv_nsec > (time_t)CHUNK_MAX_TIME_NS)
            break;

        r = write(mdata->pipes.out[1], (uint8_t *)w->blob->mem + w->offset, w->blob->size - w->offset);
        if (r > 0) {
            w->offset += r;
        } else if (r < 0) {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN)
                break;
            else {
                ret = -errno;
                break;
            }
        }

        if (w->blob->size <= w->offset) {
            sol_blob_unref(w->blob);
            sol_vector_del(&mdata->write_data, 0);
        }
    }

    return ret;
}

static bool
on_write(void *data, int fd, uint32_t active_flags)
{
    struct subprocess_data *mdata = data;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = out_write(mdata);

    if (err < 0) {
        uint16_t i;
        struct write_data *w;

        SOL_VECTOR_FOREACH_IDX (&mdata->write_data, w, i)
            sol_blob_unref(w->blob);
        sol_vector_clear(&mdata->write_data);
    }

    if (mdata->write_data.len == 0) {
        mdata->watches.out = NULL;
        return false;
    }

    return true;
}

int
process_subprocess_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct subprocess_data *mdata = data;
    struct sol_blob *blob;
    struct write_data *w;
    int ret;

    SOL_NULL_CHECK(mdata->fork_run, -EINVAL);
    ret = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(ret, < 0, ret);

    w = sol_vector_append(&mdata->write_data);
    SOL_NULL_CHECK(w, -ENOMEM);

    w->offset = 0;
    w->blob = sol_blob_ref(blob);
    if (mdata->write_data.len > 1)
        return 0;

    mdata->watches.out = sol_fd_add(mdata->pipes.out[1], SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR, on_write, mdata);
    if (!mdata->watches.out) {
        sol_blob_unref(w->blob);
        sol_vector_del_last(&mdata->write_data);
        return -1;
    }

    return 0;
}

static void SOL_ATTR_NO_RETURN
on_fork(void *data)
{
    struct subprocess_data *mdata = data;

    close(mdata->pipes.out[1]);
    close(mdata->pipes.err[0]);
    close(mdata->pipes.in[0]);
    close(STDOUT_FILENO);
    close(STDIN_FILENO);
    close(STDERR_FILENO);

    /* dup2 is acceptable because we want the pipes to not be FD_CLOEXEC */
    if (dup2(mdata->pipes.out[0], STDIN_FILENO) < 0)
        goto err;
    if (dup2(mdata->pipes.in[1], STDOUT_FILENO) < 0)
        goto err;
    if (dup2(mdata->pipes.err[1], STDERR_FILENO) < 0)
        goto err;

    execl("/bin/sh", "sh", "-c", mdata->command, (char *)0);

/*
 * An error happened, just exiting with the error to be catch by the parent
 */
err:
    SOL_WRN("Failed in setup the files descriptors");
    close(mdata->pipes.out[1]);
    close(mdata->pipes.err[0]);
    close(mdata->pipes.in[0]);
    sol_platform_linux_fork_run_exit(-errno);
}

static int
child_read(struct sol_blob **p_blob, bool *eof, int fd)
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

        r = sol_util_fill_buffer(fd, &buf, CHUNK_READ_SIZE);
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
on_in_read(void *data, int fd, uint32_t active_flags)
{
    struct subprocess_data *mdata = data;
    struct sol_blob *blob = NULL;
    bool eof = true;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = child_read(&blob, &eof, mdata->pipes.in[0]);

    if (eof || err < 0) {
        mdata->watches.in = NULL;
        if (err < 0)
            return false;
    }

    sol_flow_send_blob_packet(mdata->node, SOL_FLOW_NODE_TYPE_PROCESS_SUBPROCESS__OUT__STDOUT, blob);
    sol_blob_unref(blob);
    if (eof)
        return false;
    return true;
}

static bool
on_err_read(void *data, int fd, uint32_t active_flags)
{
    struct subprocess_data *mdata = data;
    struct sol_blob *blob = NULL;
    bool eof = true;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = child_read(&blob, &eof, mdata->pipes.err[0]);

    if (eof || err < 0) {
        mdata->watches.err = NULL;
        if (err < 0)
            return false;
    }

    sol_flow_send_blob_packet(mdata->node, SOL_FLOW_NODE_TYPE_PROCESS_SUBPROCESS__OUT__STDERR, blob);
    sol_blob_unref(blob);
    if (eof)
        return false;
    return true;
}

static void
on_fork_exit(void *data, uint64_t pid, int status)
{
    struct subprocess_data *mdata = data;

    mdata->fork_run = NULL;
    if (mdata->watches.in)
        sol_fd_del(mdata->watches.in);
    if (mdata->watches.err)
        sol_fd_del(mdata->watches.err);
    if (mdata->watches.out) {
        struct write_data *w;
        uint16_t i;
        sol_fd_del(mdata->watches.out);
        SOL_VECTOR_FOREACH_IDX (&mdata->write_data, w, i)
            sol_blob_unref(w->blob);
        sol_vector_clear(&mdata->write_data);
    }

    mdata->watches.in = NULL;
    mdata->watches.err = NULL;
    mdata->watches.out = NULL;
    sol_flow_send_irange_value_packet(mdata->node, SOL_FLOW_NODE_TYPE_PROCESS_SUBPROCESS__OUT__STATUS, status);
}

static int
setup_watches(struct subprocess_data *mdata)
{
    mdata->watches.in = sol_fd_add(mdata->pipes.in[0], SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR, on_in_read, mdata);
    SOL_NULL_CHECK(mdata->watches.in, -1);

    mdata->watches.err = sol_fd_add(mdata->pipes.err[0], SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR, on_err_read, mdata);
    if (!mdata->watches.err) {
        sol_fd_del(mdata->watches.in);
        return -1;
    }

    return 0;
}

int
process_subprocess_start_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct subprocess_data *mdata = data;

    if (mdata->fork_run)
        return 0;

    if (setup_watches(mdata) < 0)
        return -1;

    mdata->fork_run = sol_platform_linux_fork_run(on_fork, on_fork_exit, mdata);
    SOL_NULL_CHECK_GOTO(mdata->fork_run, fork_err);

    return 0;

fork_err:
    sol_fd_del(mdata->watches.err);
    mdata->watches.err = NULL;
    sol_fd_del(mdata->watches.in);
    mdata->watches.in = NULL;
    return -1;
}

int
process_subprocess_stop_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct subprocess_data *mdata = data;

    if (!mdata->fork_run)
        return 0;

    sol_platform_linux_fork_run_send_signal(mdata->fork_run, SIGTERM);
    return 0;
}

int
process_subprocess_signal_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct subprocess_data *mdata = data;
    int32_t value;
    int ret;

    SOL_NULL_CHECK(mdata->fork_run, -EINVAL);
    ret = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(ret, < 0, ret);

    sol_platform_linux_fork_run_send_signal(mdata->fork_run, value);

    return 0;
}

int
process_subprocess_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct subprocess_data *mdata = data;
    struct sol_flow_node_type_process_subprocess_options *opts =
        (struct sol_flow_node_type_process_subprocess_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_PROCESS_SUBPROCESS_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->node = node;
    sol_vector_init(&mdata->write_data, sizeof(struct write_data));

    if (pipe2(mdata->pipes.out, O_NONBLOCK | O_CLOEXEC) < 0) {
        SOL_WRN("Failed to create out pipe");
        return -errno;
    }

    if (pipe2(mdata->pipes.in, O_NONBLOCK | O_CLOEXEC) < 0) {
        SOL_WRN("Failed to create in pipe");
        goto in_err;
    }

    if (pipe2(mdata->pipes.err, O_NONBLOCK | O_CLOEXEC) < 0) {
        SOL_WRN("Failed to create err pipe");
        goto err_err;
    }

    mdata->command = strdup(opts->command);
    SOL_NULL_CHECK_GOTO(mdata->command, flags_err);

    if (opts->start) {
        if (setup_watches(mdata) < 0)
            goto watch_err;
        mdata->fork_run = sol_platform_linux_fork_run(on_fork, on_fork_exit, mdata);
        SOL_NULL_CHECK_GOTO(mdata->fork_run, err);
    }

    return 0;

err:
    sol_fd_del(mdata->watches.in);
    sol_fd_del(mdata->watches.err);
watch_err:
    free(mdata->command);
flags_err:
    close(mdata->pipes.err[0]);
    close(mdata->pipes.err[1]);
err_err:
    close(mdata->pipes.in[0]);
    close(mdata->pipes.in[1]);
in_err:
    close(mdata->pipes.out[0]);
    close(mdata->pipes.out[1]);
    return -errno;
}

void
process_subprocess_close(struct sol_flow_node *node, void *data)
{
    struct subprocess_data *mdata = data;

    if (mdata->fork_run)
        sol_platform_linux_fork_run_stop(mdata->fork_run);

    if (mdata->watches.in)
        sol_fd_del(mdata->watches.in);
    if (mdata->watches.err)
        sol_fd_del(mdata->watches.err);
    if (mdata->watches.out) {
        struct write_data *w;
        uint16_t i;

        sol_fd_del(mdata->watches.out);
        SOL_VECTOR_FOREACH_IDX (&mdata->write_data, w, i)
            sol_blob_unref(w->blob);
        sol_vector_clear(&mdata->write_data);
    }

    close(mdata->pipes.in[0]);
    close(mdata->pipes.in[1]);
    close(mdata->pipes.err[0]);
    close(mdata->pipes.err[1]);
    close(mdata->pipes.out[0]);
    close(mdata->pipes.out[1]);

    free(mdata->command);
}
