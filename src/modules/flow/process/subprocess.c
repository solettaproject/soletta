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

#include "sol-platform-linux.h"
#include "sol-util.h"

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
stdout_write(struct subprocess_data *mdata)
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

        r = write(mdata->pipes.stdout[1], (uint8_t *)w->blob->mem + w->offset, w->blob->size - w->offset);
        if (r > 0) {
            w->offset += r;
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

        if (w->blob->size <= w->offset) {
            sol_blob_unref(w->blob);
            sol_vector_del(&mdata->write_data, 0);
        }
    }

    return ret;
}

static bool
on_write(void *data, int fd, unsigned int active_flags)
{
    struct subprocess_data *mdata = data;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = stdout_write(mdata);

    if (err < 0) {
        uint16_t i;
        struct write_data *w;

        SOL_VECTOR_FOREACH_IDX (&mdata->write_data, w, i)
            sol_blob_unref(w->blob);
        sol_vector_clear(&mdata->write_data);
    }

    if (mdata->write_data.len == 0) {
        mdata->watches.stdout = NULL;
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

    mdata->watches.stdout = sol_fd_add(mdata->pipes.stdout[1], SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR, on_write, mdata);
    if (!mdata->watches.stdout) {
        sol_blob_unref(w->blob);
        sol_vector_del(&mdata->write_data, mdata->write_data.len - 1);
        return -1;
    }

    return 0;
}

static void
on_fork(void *data)
{
    struct subprocess_data *mdata = data;

    close(mdata->pipes.stdout[1]);
    close(mdata->pipes.stderr[0]);
    close(mdata->pipes.stdin[0]);
    close(STDOUT_FILENO);
    close(STDIN_FILENO);
    close(STDERR_FILENO);

    if (dup2(mdata->pipes.stdout[0], STDIN_FILENO) < 0)
        goto err;
    if (dup2(mdata->pipes.stdin[1], STDOUT_FILENO) < 0)
        goto err;
    if (dup2(mdata->pipes.stderr[1], STDERR_FILENO) < 0)
        goto err;

    execl("/bin/sh", "sh", "-c", mdata->command, (char *)0);

/*
 * An error happened, just exiting with the error to be catch by the parent
 */
err:
    SOL_WRN("Failed in setup the files descriptors");
    close(mdata->pipes.stdout[1]);
    close(mdata->pipes.stderr[0]);
    close(mdata->pipes.stdin[0]);
    exit(-errno);
}

static int
child_read(struct sol_blob **p_blob, bool *eof, int fd)
{
    char *buf = NULL;
    size_t buflen = 0;
    size_t offset = 0;
    int ret = 0;
    struct timespec start = sol_util_timespec_get_current();

    *eof = false;
    do {
        struct timespec now = sol_util_timespec_get_current();
        struct timespec elapsed;
        ssize_t r;

        sol_util_timespec_sub(&now, &start, &elapsed);
        if (elapsed.tv_sec > 0 ||
            elapsed.tv_nsec > (time_t)CHUNK_MAX_TIME_NS)
            break;

        if (offset + CHUNK_READ_SIZE > buflen) {
            char *tmp;

            /* limit reading chunks to maximum or available memory */
            if (buflen + CHUNK_READ_SIZE >= CHUNK_READ_MAX)
                break;

            /* if we're out of memory, just return without failing */
            tmp = realloc(buf, buflen + CHUNK_READ_SIZE);
            if (!tmp)
                break;

            buf = tmp;
            buflen += CHUNK_READ_SIZE;
        }

        r = read(fd, buf + offset, CHUNK_READ_SIZE);
        if (r > 0)
            offset += r;
        else if (r == 0) {
            *eof = true;
            break;
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
    } while (1);

    if (ret < 0) {
        free(buf);
        return ret;
    }

    *p_blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, buf, offset);
    SOL_NULL_CHECK_GOTO(*p_blob, blob_error);
    return 0;

blob_error:
    free(buf);
    return -ENOMEM;
}

static bool
on_stdin_read(void *data, int fd, unsigned int active_flags)
{
    struct subprocess_data *mdata = data;
    struct sol_blob *blob = NULL;
    bool eof = true;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = child_read(&blob, &eof, mdata->pipes.stdin[0]);

    if (eof || err < 0) {
        mdata->watches.stdin = NULL;
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
on_stderr_read(void *data, int fd, unsigned int active_flags)
{
    struct subprocess_data *mdata = data;
    struct sol_blob *blob = NULL;
    bool eof = true;
    int err = 0;

    if (active_flags & SOL_FD_FLAGS_ERR)
        err = -EBADF;
    else
        err = child_read(&blob, &eof, mdata->pipes.stderr[0]);

    if (eof || err < 0) {
        mdata->watches.stderr = NULL;
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
    if (mdata->watches.stdin)
        sol_fd_del(mdata->watches.stdin);
    if (mdata->watches.stderr)
        sol_fd_del(mdata->watches.stderr);
    if (mdata->watches.stdout) {
        struct write_data *w;
        uint16_t i;
        sol_fd_del(mdata->watches.stdout);
        SOL_VECTOR_FOREACH_IDX (&mdata->write_data, w, i)
            sol_blob_unref(w->blob);
        sol_vector_clear(&mdata->write_data);
    }

    mdata->watches.stdin = NULL;
    mdata->watches.stderr = NULL;
    mdata->watches.stdout = NULL;
    sol_flow_send_irange_value_packet(mdata->node, SOL_FLOW_NODE_TYPE_PROCESS_SUBPROCESS__OUT__STATUS, status);
}

static int
setup_watches(struct subprocess_data *mdata)
{
    mdata->watches.stdin = sol_fd_add(mdata->pipes.stdin[0], SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR, on_stdin_read, mdata);
    SOL_NULL_CHECK(mdata->watches.stdin, -1);

    mdata->watches.stderr = sol_fd_add(mdata->pipes.stderr[0], SOL_FD_FLAGS_IN | SOL_FD_FLAGS_ERR, on_stderr_read, mdata);
    if (!mdata->watches.stderr) {
        sol_fd_del(mdata->watches.stdin);
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
    sol_fd_del(mdata->watches.stderr);
    mdata->watches.stderr = NULL;
    sol_fd_del(mdata->watches.stdin);
    mdata->watches.stdin = NULL;
    return -1;
}

int
process_subprocess_stop_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct subprocess_data *mdata = data;

    if (!mdata->fork_run)
        return 0;

    kill(sol_platform_linux_fork_run_get_pid(mdata->fork_run), SIGTERM);
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

    kill(sol_platform_linux_fork_run_get_pid(mdata->fork_run), value);

    return 0;
}

int
process_subprocess_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct subprocess_data *mdata = data;
    struct sol_flow_node_type_process_subprocess_options *opts =
        (struct sol_flow_node_type_process_subprocess_options *)options;

    mdata->node = node;
    sol_vector_init(&mdata->write_data, sizeof(struct write_data));

    if (sol_create_pipe(mdata->pipes.stdout, 0) < 0) {
        SOL_WRN("Failed to create stdout pipe");
        return -errno;
    }

    if (sol_create_pipe(mdata->pipes.stdin, 0) < 0) {
        SOL_WRN("Failed to create stdin pipe");
        goto stdin_err;
    }

    if (sol_create_pipe(mdata->pipes.stderr, 0) < 0) {
        SOL_WRN("Failed to create stderr pipe");
        goto stderr_err;
    }

#define NONBLOCK_FD(_fd)                                \
    do {                                                \
        int flags;                                      \
        flags = fcntl(_fd, F_GETFL);                    \
        SOL_INT_CHECK_GOTO(flags, < 0, flags_err);      \
        flags |= O_NONBLOCK;                            \
        fcntl(_fd, F_SETFL, flags);                     \
    } while (0)

    NONBLOCK_FD(mdata->pipes.stdin[0]);
    NONBLOCK_FD(mdata->pipes.stderr[0]);
    NONBLOCK_FD(mdata->pipes.stdout[1]);

#undef NONBLOCK_FD

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
    sol_fd_del(mdata->watches.stdin);
    sol_fd_del(mdata->watches.stderr);
watch_err:
    free(mdata->command);
flags_err:
    close(mdata->pipes.stderr[0]);
    close(mdata->pipes.stderr[1]);
stderr_err:
    close(mdata->pipes.stdin[0]);
    close(mdata->pipes.stdin[1]);
stdin_err:
    close(mdata->pipes.stdout[0]);
    close(mdata->pipes.stdout[1]);
    return -errno;
}

void
process_subprocess_close(struct sol_flow_node *node, void *data)
{
    struct subprocess_data *mdata = data;

    if (mdata->fork_run)
        sol_platform_linux_fork_run_stop(mdata->fork_run);

    if (mdata->watches.stdin)
        sol_fd_del(mdata->watches.stdin);
    if (mdata->watches.stderr)
        sol_fd_del(mdata->watches.stderr);
    if (mdata->watches.stdout) {
        struct write_data *w;
        uint16_t i;

        sol_fd_del(mdata->watches.stdout);
        SOL_VECTOR_FOREACH_IDX (&mdata->write_data, w, i)
            sol_blob_unref(w->blob);
        sol_vector_clear(&mdata->write_data);
    }

    close(mdata->pipes.stdin[0]);
    close(mdata->pipes.stdin[1]);
    close(mdata->pipes.stderr[0]);
    close(mdata->pipes.stderr[1]);
    close(mdata->pipes.stdout[0]);
    close(mdata->pipes.stdout[1]);

    free(mdata->command);
}
