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
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sol-log.h"
#include "sol-buffer.h"
#include "sol-common-buildopts.h"
#include "sol-util-internal.h"
#include "sol-file-reader.h"

struct sol_file_reader {
    void *contents;
    struct stat st;
    bool mmapped;
};

SOL_API struct sol_file_reader *
sol_file_reader_open(const char *filename)
{
    struct sol_file_reader *fr;
    int fd;

    SOL_NULL_CHECK(filename, NULL);

    if (*filename == '\0') {
        SOL_WRN("File name shouldn't be empty");
        return NULL;
    }

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    fr = sol_file_reader_from_fd(fd);
    close(fd);
    return fr;
}

SOL_API struct sol_file_reader *
sol_file_reader_from_fd(int fd)
{
    int saved_errno;
    struct sol_file_reader *fr, *result = NULL;
    struct sol_buffer *buffer;
    size_t size;

    fr = malloc(sizeof(*fr));
    SOL_NULL_CHECK(fr, NULL);

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

    buffer = sol_util_load_file_fd_raw(fd);
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

SOL_API void
sol_file_reader_close(struct sol_file_reader *fr)
{
    SOL_NULL_CHECK(fr);

    if (fr->mmapped)
        munmap(fr->contents, fr->st.st_size);
    else
        free(fr->contents);
    free(fr);
}

SOL_API struct sol_str_slice
sol_file_reader_get_all(const struct sol_file_reader *fr)
{
    SOL_NULL_CHECK(fr, (struct sol_str_slice) {
        .len = 0,
        .data = NULL,
    });

    return (struct sol_str_slice) {
               .len = fr->st.st_size,
               .data = fr->contents,
    };
}

SOL_API const struct stat *
sol_file_reader_get_stat(const struct sol_file_reader *fr)
{
    SOL_NULL_CHECK(fr, NULL);

    return &fr->st;
}

struct sol_blob_file_reader {
    struct sol_blob base;
    struct sol_file_reader *fr;
};

static void
_sol_blob_type_file_reader_close(struct sol_blob *blob)
{
    struct sol_blob_file_reader *b = (struct sol_blob_file_reader *)blob;

    sol_file_reader_close(b->fr);
    free(blob);
}

static const struct sol_blob_type _SOL_BLOB_TYPE_FILE_READER = {
#ifndef SOL_NO_API_VERSION
    .api_version = SOL_BLOB_TYPE_API_VERSION,
    .sub_api = 1,
#endif
    .free = _sol_blob_type_file_reader_close
};

SOL_API struct sol_blob *
sol_file_reader_to_blob(struct sol_file_reader *fr)
{
    struct sol_blob_file_reader *b;
    struct sol_str_slice c;

    SOL_NULL_CHECK(fr, NULL);

    c = sol_file_reader_get_all(fr);

    b = calloc(1, sizeof(struct sol_blob_file_reader));
    SOL_NULL_CHECK_GOTO(b, error);

    sol_blob_setup(&b->base, &_SOL_BLOB_TYPE_FILE_READER, c.data, c.len);
    b->fr = fr;
    return &b->base;

error:
    sol_file_reader_close(fr);
    return NULL;
}
