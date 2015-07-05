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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "file-gen.h"

#include "sol-file-reader.h"
#include "sol-flow-internal.h"
#include "sol-worker-thread.h"
#include "sol-util.h"
#include "sol-mainloop.h"

/*
 * TODO:
 *
 * These types only handle a full file at once, there should be a
 * progressive version that loads chunks (blobs) and send them ASAP,
 * writing to disk as they arrive at input. In such cases there must
 * be a "reset" port so readers fseek() to start and writers truncate.
 */

static void
file_reader_blob_free(struct sol_blob *blob)
{
    struct sol_file_reader *reader = blob->mem;

    sol_file_reader_close(reader);
    free(blob);
}

static const struct sol_blob_type file_reader_blob_type = {
    .api_version = SOL_BLOB_TYPE_API_VERSION,
    .free = file_reader_blob_free
};

static const struct sol_blob_type file_reader_content_blob_type = {
    .api_version = SOL_BLOB_TYPE_API_VERSION,
};

struct file_reader_data {
    struct sol_flow_node *node;
    char *path;
    struct sol_blob *reader_blob;
    struct sol_blob *content_blob;
    struct sol_idle *idler;
};

static void
file_reader_unload(struct file_reader_data *mdata)
{
    if (mdata->idler) {
        sol_idle_del(mdata->idler);
        mdata->idler = NULL;
    }
    if (mdata->content_blob) {
        sol_blob_unref(mdata->content_blob);
        mdata->content_blob = NULL;
    }
    if (mdata->reader_blob) {
        sol_blob_unref(mdata->reader_blob);
        mdata->reader_blob = NULL;
    }
    SOL_DBG("unloaded path=\"%s\"", mdata->path ? mdata->path : "");
    free(mdata->path);
}

static int
file_reader_load(struct file_reader_data *mdata)
{
    struct sol_file_reader *reader;
    struct sol_str_slice slice;

    if (!mdata->path)
        return 0;

    reader = sol_file_reader_open(mdata->path);
    if (!reader) {
        char errmsg[1024];
        snprintf(errmsg, sizeof(errmsg), "Could not load \"%s\": %s",
            mdata->path, sol_util_strerrora(errno));
        sol_flow_send_error_packet(mdata->node, errno, errmsg);
        return -errno;
    }
    slice = sol_file_reader_get_all(reader);

    SOL_DBG("loaded path=\"%s\", data=%p, len=%zd", mdata->path, slice.data, slice.len);
    mdata->reader_blob = sol_blob_new(&file_reader_blob_type, NULL,
        reader, sizeof(reader));
    SOL_NULL_CHECK_GOTO(mdata->reader_blob, err_reader);

    mdata->content_blob = sol_blob_new(&file_reader_content_blob_type,
        mdata->reader_blob,
        slice.data, slice.len);
    SOL_NULL_CHECK_GOTO(mdata->content_blob, err_content);

    return sol_flow_send_blob_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_FILE_READER__OUT__OUT,
        mdata->content_blob);

err_content:
    sol_blob_unref(mdata->reader_blob);
err_reader:
    sol_file_reader_close(reader);
    return -ENOMEM;
}

static int
file_reader_path_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct file_reader_data *mdata = data;
    const char *path;
    int r;

    r = sol_flow_packet_get_string(packet, &path);
    SOL_INT_CHECK(r, < 0, r);

    if (path && mdata->path && streq(path, mdata->path))
        return 0;

    file_reader_unload(mdata);

    mdata->path = path ? strdup(path) : NULL;
    return file_reader_load(mdata);
}

static bool
file_reader_open_delayed(void *data)
{
    struct file_reader_data *mdata = data;

    mdata->idler = NULL;
    file_reader_load(mdata);
    return false;
}

static int
file_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_file_reader_options *opts = (const struct sol_flow_node_type_file_reader_options *)options;
    struct file_reader_data *mdata = data;

    mdata->node = node;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_FILE_READER_OPTIONS_API_VERSION, -EINVAL);

    if (opts->path) {
        mdata->path = strdup(opts->path);
        SOL_NULL_CHECK(mdata->path, -ENOMEM);
    }

    mdata->idler = sol_idle_add(file_reader_open_delayed, mdata);
    return 0;
}

static void
file_reader_close(struct sol_flow_node *node, void *data)
{
    file_reader_unload(data);
}

struct file_writer_data {
    struct sol_flow_node *node;
    char *path;
    struct sol_blob *pending_blob;
    struct sol_worker_thread *worker;
    size_t size;
    size_t done;
    int fd;
    int error;
    int permissions;
    bool canceled;
};

static int
file_writer_send(struct file_writer_data *mdata)
{
    struct sol_irange val = { 0, 0, INT32_MAX, 1 };
    int r;

    r = sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_FILE_WRITER__OUT__BUSY,
        !!mdata->worker);
    SOL_INT_CHECK(r, < 0, r);

    val.val = mdata->size;
    r = sol_flow_send_irange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_FILE_WRITER__OUT__SIZE,
        &val);
    SOL_INT_CHECK(r, < 0, r);

    val.val = mdata->done;
    return sol_flow_send_irange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_FILE_WRITER__OUT__DONE,
        &val);
}

static void
file_writer_unload(struct file_writer_data *mdata)
{
    if (mdata->worker) {
        mdata->canceled = true;
        sol_worker_thread_cancel(mdata->worker);
    }

    if (mdata->pending_blob) {
        sol_blob_unref(mdata->pending_blob);
        mdata->pending_blob = NULL;
    }

    mdata->size = 0;
    mdata->done = 0;
}

static void
file_writer_worker_thread_finished(void *data)
{
    struct file_writer_data *mdata = data;

    if (mdata->canceled)
        return;
    mdata->worker = NULL;
    file_writer_send(mdata);
}

static void
file_writer_worker_thread_feedback(void *data)
{
    struct file_writer_data *mdata = data;

    file_writer_send(mdata);
}

static bool
file_writer_worker_thread_setup(void *data)
{
    struct file_writer_data *mdata = data;

    unlink(mdata->path);

    mdata->fd = open(mdata->path,
        O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL | O_NONBLOCK,
        mdata->permissions);
    SOL_DBG("open \"%s\" fd=%d, permissions=%#o", mdata->path, mdata->fd, mdata->permissions);
    if (mdata->fd < 0) {
        char *msg;
        mdata->error = errno;
        msg = sol_util_strerrora(errno);
        sol_flow_send_error_packet(mdata->node, mdata->error, msg);
        SOL_WRN("could not open '%s': %s", mdata->path, msg);
        return false;
    }

    mdata->done = 0;
    return true;
}

static void
file_writer_worker_thread_cleanup(void *data)
{
    struct file_writer_data *mdata = data;

    if (mdata->error == 0 && mdata->done < mdata->pending_blob->size)
        mdata->error = ECANCELED;

    SOL_DBG("close \"%s\" fd=%d wrote=%zu of %zu, error=%d %s",
        mdata->path, mdata->fd, mdata->done, mdata->pending_blob->size,
        mdata->error, sol_util_strerrora(mdata->error));

    close(mdata->fd);
    mdata->fd = -1;
}

static bool
file_writer_worker_thread_iterate(void *data)
{
    struct file_writer_data *mdata = data;
    const uint8_t *p = mdata->pending_blob->mem;
    size_t todo;
    ssize_t w;

    todo = mdata->pending_blob->size - mdata->done;
    if (todo == 0  || mdata->error != 0)
        return false;

#define BLOCKSIZE (1024 * 8)
    if (todo > BLOCKSIZE)
        todo = BLOCKSIZE;
#undef BLOCKSIZE
    p += mdata->done;

    w = write(mdata->fd, p, todo);
    SOL_DBG("wrote fd=%d %zd bytes, %zu of %zu, p=%p",
        mdata->fd, w, mdata->done, mdata->pending_blob->size, p);
    if (w > 0) {
        mdata->done += w;
        sol_worker_thread_feedback(mdata->worker);
    } else if (w < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            char *msg;
            mdata->error = errno;
            msg = sol_util_strerrora(errno);
            SOL_WRN("could not write %zd bytes to fd=%d (%s): %s",
                todo, mdata->fd, mdata->path, msg);
            sol_flow_send_error_packet(mdata->node, mdata->error, msg);
            return false;
        }
    }

    return true;
}

static int
file_writer_load(struct file_writer_data *mdata)
{
    struct sol_worker_thread_spec spec = {
        .api_version = SOL_WORKER_THREAD_SPEC_API_VERSION,
        .setup = file_writer_worker_thread_setup,
        .cleanup = file_writer_worker_thread_cleanup,
        .iterate = file_writer_worker_thread_iterate,
        .finished = file_writer_worker_thread_finished,
        .feedback = file_writer_worker_thread_feedback,
        .data = mdata
    };

    if (!mdata->path || !mdata->pending_blob)
        return 0;

    mdata->error = 0;
    mdata->size = mdata->pending_blob->size;
    mdata->done = 0;
    mdata->canceled = false;
    file_writer_send(mdata);

    mdata->worker = sol_worker_thread_new(&spec);
    SOL_NULL_CHECK_GOTO(mdata->worker, error);
    return 0;

error:
    mdata->error = errno;
    sol_flow_send_error_packet(mdata->node, mdata->error, NULL);
    return -mdata->error;
}

static int
file_writer_path_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct file_writer_data *mdata = data;
    const char *path;
    int r;

    r = sol_flow_packet_get_string(packet, &path);
    SOL_INT_CHECK(r, < 0, r);

    if (path && mdata->path && streq(path, mdata->path))
        return 0;

    file_writer_unload(mdata);
    free(mdata->path);

    mdata->path = path ? strdup(path) : NULL;
    return file_writer_load(mdata);
}

static int
file_writer_permissions_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct file_writer_data *mdata = data;
    int r, permissions;

    r = sol_flow_packet_get_irange_value(packet, &permissions);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->permissions == permissions)
        return 0;

    file_writer_unload(mdata);

    mdata->permissions = permissions;
    return file_writer_load(mdata);
}

static int
file_writer_contents_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct file_writer_data *mdata = data;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    file_writer_unload(mdata);
    mdata->pending_blob = sol_blob_ref(blob);
    SOL_NULL_CHECK(mdata->pending_blob, -errno);

    return file_writer_load(mdata);
}

static int
file_writer_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_file_writer_options *opts = (const struct sol_flow_node_type_file_writer_options *)options;
    struct file_writer_data *mdata = data;

    mdata->node = node;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_FILE_WRITER_OPTIONS_API_VERSION, -EINVAL);

    if (opts->path) {
        mdata->path = strdup(opts->path);
        SOL_NULL_CHECK(mdata->path, -ENOMEM);
    }
    mdata->permissions = opts->permissions.val;

    return 0;
}

static void
file_writer_close(struct sol_flow_node *node, void *data)
{
    struct file_writer_data *mdata = data;

    file_writer_unload(mdata);
    free(mdata->path);
}


#include "file-gen.c"
