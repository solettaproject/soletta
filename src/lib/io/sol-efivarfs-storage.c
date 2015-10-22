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

#include "sol-efivarfs-storage.h"

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

#define SOLETTA_EFIVARS_GUID "076027a8-c791-41d7-940f-3d465869f821"
#define EFIVARFS_VAR_DIR "/sys/firmware/efi/efivars/"
#define EFIVARFS_VAR_PATH EFIVARFS_VAR_DIR "%s-" SOLETTA_EFIVARS_GUID

static const int EFIVARS_DEFAULT_ATTR = 0x7;

static bool
check_realpath(const char *path)
{
    char real_path[PATH_MAX];

    if (realpath(path, real_path)) {
        return strstartswith(real_path, EFIVARFS_VAR_DIR);
    }

    return false;
}

SOL_API int
sol_efivars_write_raw(const char *name, const struct sol_buffer *buffer)
{
    FILE *file;
    char path[PATH_MAX];
    int r;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file [%s]", path);
        return -EINVAL;
    }

    file = fopen(path, "w+e");
    if (!file) {
        SOL_WRN("Could not open persistence file [%s]: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }
    if (!check_realpath(path)) {
        /* At this point, a file on an invalid location may have been created.
         * Should we care about it? Is there a 'realpath()' that doesn't need
         * the file to exist? Or a string path santiser? */
        SOL_WRN("Invalid name for efivars persistence packet [%s]", name);
        r = -EINVAL;
        goto end;
    }

    fwrite(&EFIVARS_DEFAULT_ATTR, sizeof(EFIVARS_DEFAULT_ATTR), 1, file);
    if (ferror(file)) {
        SOL_WRN("Coud not write peristence file [%s] attributes", path);
        r = -EIO;
        goto end;
    }

    fwrite(buffer->data, buffer->used, 1, file);

end:
    if (fclose(file)) {
        SOL_WRN("Could not close persistence file [%s]: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }

    return r;
}

SOL_API int
sol_efivars_read_raw(const char *name, struct sol_buffer *buffer)
{
    int r, fd;
    char path[PATH_MAX];
    uint32_t b;
    struct sol_buffer attr = SOL_BUFFER_INIT_FLAGS(&b, sizeof(uint32_t),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file [%s]", path);
        return -EINVAL;
    }
    if (!check_realpath(path)) {
        SOL_WRN("Invalid name for efivars persistence packet [%s]", name);
        return -EINVAL;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Could not open persistence file [%s]: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }

    /* Read (to discard) the first uint32_t containing the attributes */
    r = sol_util_fill_buffer(fd, &attr, sizeof(uint32_t));
    if (r < 0) {
        SOL_WRN("Could not read persistence file [%s] attributes", path);
        goto end;
    }

    if (buffer->capacity) {
        r = sol_util_fill_buffer(fd, buffer, buffer->capacity);
    } else {
        size_t size;
        char *data = sol_util_load_file_fd_string(fd, &size);
        if (data) {
            buffer->capacity = size;
            buffer->used = size;
            buffer->data = data;
            r = size;
        } else
            r = -errno;
    }

end:
    close(fd);

    return r;
}
