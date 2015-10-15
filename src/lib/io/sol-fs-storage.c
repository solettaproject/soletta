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

#include "sol-fs-storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-util-file.h"

struct cb_data {
    char *name;
    struct sol_blob *blob;
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status);
    const void *data;
    int status;
};

static bool
write_cb(void *data)
{
    struct cb_data *cb_data = data;

    cb_data->cb((void *)cb_data->data, cb_data->name, cb_data->blob, cb_data->status);
    sol_blob_unref(cb_data->blob);

    free(cb_data->name);
    free(cb_data);

    return false;
}

SOL_API int
sol_fs_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    FILE *file = NULL;
    struct cb_data *cb_data;
    int ret = 0, status = 0;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    if (cb) {
        cb_data = calloc(1, sizeof(struct cb_data));
        SOL_NULL_CHECK(cb_data, -ENOMEM);

        cb_data->blob = sol_blob_ref(blob);
        if (!cb_data->blob) {
            free(cb_data);
            return -ENOMEM;
        }

        cb_data->blob = blob;
        cb_data->data = data;
        cb_data->cb = cb;
        cb_data->name = strdup(name);
        if (!cb_data->name) {
            sol_blob_unref(blob);
            free(cb_data);
            return -ENOMEM;
        }
    }

    /* From this point on, we return success even on failure, and report
     * failure on cb, if any. So we have a uniform behaviour among storage
     * api */

    file = fopen(name, "w+e");
    if (!file) {
        SOL_WRN("Could not open persistence file [%s]", name);
        status = -errno;
        goto end;
    }

    fwrite(blob->mem, blob->size, 1, file);
    if (ferror(file)) {
        SOL_WRN("Could not write to persistence file [%s]", name);
        status = -EIO;
    }

end:
    if (file && fclose(file)) {
        SOL_WRN("Could not close persistence file [%s]", name);
        status = -errno;
    }

    if (cb) {
        cb_data->status = status;
        sol_timeout_add(0, write_cb, cb_data);
    }

    return ret;
}

SOL_API int
sol_fs_read_raw(const char *name, struct sol_buffer *buffer)
{
    int r, fd;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    fd = open(name, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Could not open persistence file [%s]", name);
        return -errno;
    }

    if (buffer->capacity) {
        r = sol_util_fill_buffer(fd, buffer, buffer->capacity);
    } else {
        size_t size = 0;
        char *data = sol_util_load_file_fd_string(fd, &size);
        if (data) {
            buffer->capacity = size;
            buffer->used = size;
            buffer->data = data;
            r = size;
        } else
            r = -errno;
    }

    close(fd);

    return r;
}
