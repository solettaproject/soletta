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
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sol-log.h"
#include "sol-buffer.h"
#include "sol-util.h"
#include "sol-file-reader.h"

struct sol_file_reader {
    void *contents;
    struct stat st;
    bool mmapped;
};

struct sol_file_reader *
sol_file_reader_open(const char *filename)
{
    int fd = -1;
    struct sol_file_reader *ret;

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    ret = sol_file_reader_fd(fd);
    if (fd >= 0)
        close(fd);

    return ret;
}

struct sol_file_reader *
sol_file_reader_fd(int fd)
{
    int saved_errno;
    struct sol_file_reader *fr, *result = NULL;
    struct sol_buffer *buffer;
    size_t size;

    fr = malloc(sizeof(*fr));
    if (!fr)
        return NULL;

    fr->mmapped = false;

    if (fstat(fd, &fr->st) < 0)
        goto err;

    fr->contents = mmap(NULL, fr->st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (fr->contents != MAP_FAILED) {
        fr->mmapped = true;
        goto success;
    } else if (errno == ENOMEM) {
        goto err;
    }

    buffer = sol_util_load_file_raw(fd);
    if (!buffer)
        goto err;
    fr->contents = sol_buffer_steal(buffer, &size);
    fr->st.st_size = size;
    free(buffer);

success:
    result = fr;
    fr = NULL;
err:
    saved_errno = errno;
    free(fr);
    errno = saved_errno;
    return result;
}

void
sol_file_reader_close(struct sol_file_reader *fr)
{
    if (fr->mmapped)
        munmap(fr->contents, fr->st.st_size);
    else
        free(fr->contents);
    free(fr);
}

struct sol_str_slice
sol_file_reader_get_all(const struct sol_file_reader *fr)
{
    return (struct sol_str_slice) {
               .len = fr->st.st_size,
               .data = fr->contents,
    };
}

const struct stat *
sol_file_reader_get_stat(const struct sol_file_reader *fr)
{
    return &fr->st;
}
