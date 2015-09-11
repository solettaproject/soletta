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

#include "fs-storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-util-file.h"

int
fs_write_raw(const char *name, struct sol_buffer *buffer)
{
    FILE *file;
    int ret = 0;

    file = fopen(name, "w+e");
    if (!file) {
        SOL_WRN("Could not open persistence file [%s]", name);
        return -errno;
    }

    fwrite(buffer->data, buffer->used, 1, file);
    if (ferror(file)) {
        SOL_WRN("Could not write to persistence file [%s]", name);
        ret = -EIO;
    }

    if (fclose(file)) {
        SOL_WRN("Could not close persistence file [%s]", name);
        return -errno;
    }

    return ret;
}

int
fs_read_raw(const char *name, struct sol_buffer *buffer)
{
    int r, fd;

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
