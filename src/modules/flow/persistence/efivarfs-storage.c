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

#include "efivarfs-storage.h"

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

#define EFIVARS_DEFAULT_ATTR 0x7
#define SOLETTA_EFIVARS_GUID "076027a8-c791-41d7-940f-3d465869f821"
#define EFIVARFS_VAR_PATH "/sys/firmware/efi/efivars/%s-" SOLETTA_EFIVARS_GUID

struct efi_var {
    uint32_t attributes;
    uint8_t data[];
};

int
efivars_write(const char *name, void *data, size_t size)
{
#ifdef FEATURE_FILESYSTEM
    struct efi_var *efi_var;
    FILE *file;
    char path[PATH_MAX];
    int r;

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file [%s]", path);
        return -EINVAL;
    }

    efi_var = calloc(1, sizeof(uint32_t) + size);
    SOL_NULL_CHECK(efi_var, -ENOMEM);

    efi_var->attributes = EFIVARS_DEFAULT_ATTR;
    memcpy(efi_var->data, data, size);

    file = fopen(path, "w+e");
    if (!file) {
        SOL_WRN("Could not open persistence file [%s]", path);
        free(efi_var);
        return -errno;
    }

    /* TODO should short writes be considered? If so, something on sol-util-file? */
    r = fwrite(efi_var, sizeof(uint32_t) + size, 1, file);

    if (fclose(file)) {
        SOL_WRN("Could not close persistence file [%s]", path);
        free(efi_var);
        return -errno;
    }

    free(efi_var);
    return 0;

#else
    SOL_WRN("No file system available");
    return -ENOSYS;
#endif
}

int
efivars_read(const char *name, void *data, size_t size)
{
#ifdef FEATURE_FILESYSTEM
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    char path[PATH_MAX];
    int r, fd;

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file [%s]", path);
        return -EINVAL;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Could not open persistence file [%s]", path);
        return -errno;
    }

    r = sol_util_fill_buffer(fd, &buffer, size + sizeof(uint32_t));

    memcpy(data, (char *)buffer.data + sizeof(uint32_t), buffer.used - sizeof(uint32_t));

    sol_buffer_fini(&buffer);

    close(fd);

    return r;

#else
    SOL_WRN("No file system available");
    return -ENOSYS;
#endif
}

int
efivars_get_size(const char *name, size_t *size)
{
#ifdef FEATURE_FILESYSTEM
    char path[PATH_MAX];
    struct stat st;
    int r;

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file");
        return -EINVAL;
    }

    if (stat(path, &st)) {
        SOL_WRN("Could not get persistence file [%s] size: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }

    /* efivar size needs to discount attributes size (uint32_t) */
    *size = (size_t)st.st_size - sizeof(uint32_t);

    return 0;

#else
    SOL_WRN("No file system available");
    return -ENOSYS;
#endif
}
