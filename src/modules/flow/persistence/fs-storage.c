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
fs_write(const char *name, const char *fs_dir_path, void *data, size_t size)
{
#ifdef FEATURE_FILESYSTEM
    FILE *file;
    char path[PATH_MAX];
    int r;

    if (!fs_dir_path || *fs_dir_path == '\0') {
        SOL_WRN("Persistence dir path not specified");
        return -ENOENT;
    }

    r = snprintf(path, sizeof(path), "%s/%s", fs_dir_path, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for persistence file");
        return -EINVAL;
    }

    file = fopen(path, "w+e");
    if (!file) {
        SOL_WRN("Could not open persistence file [%s]", path);
        return -errno;
    }

    /* TODO should short writes be considered? If so, something on sol-util-file? */
    r = fwrite(data, size, 1, file);

    if (fclose(file)) {
        SOL_WRN("Could not close persistence file [%s]", path);
        return -errno;
    }

    return 0;

#else
    SOL_WRN("No file system available");
    return -ENOSYS;
#endif
}

int
fs_read(const char *name, const char *fs_dir_path, void *data, size_t size)
{
#ifdef FEATURE_FILESYSTEM
    /* TODO check if sol-util-file can really be used here */
    struct sol_buffer buffer = SOL_BUFFER_INIT_FLAGS(data, size,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);
    char path[PATH_MAX];
    int r, fd;

    if (!fs_dir_path || *fs_dir_path == '\0') {
        SOL_WRN("Persistence dir path not specified");
        return -ENOENT;
    }

    r = snprintf(path, sizeof(path), "%s/%s", fs_dir_path, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for persistence file [%s]", path);
        return -EINVAL;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Could not open persistence file [%s]", path);
        return -errno;
    }

    r = sol_util_fill_buffer(fd, &buffer, size);

    close(fd);

    return r;
#else
    SOL_WRN("No file system available");
    return -ENOSYS;
#endif
}

int
fs_get_size(const char *name, const char *fs_dir_path, size_t *size)
{
#ifdef FEATURE_FILESYSTEM
    char path[PATH_MAX];
    struct stat st;
    int r;

    if (!fs_dir_path || *fs_dir_path == '\0') {
        SOL_WRN("Persistence dir path not specified");
        return -ENOENT;
    }

    r = snprintf(path, sizeof(path), "%s/%s", fs_dir_path, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for persistence file");
        return -EINVAL;
    }

    if (stat(path, &st)) {
        SOL_WRN("Could not get persistence file [%s] size: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }

    *size = (size_t)st.st_size;

    return 0;
#else
    SOL_WRN("No file system available");
    return -ENOSYS;
#endif
}

///////////////////////////////////////////////////////////////////////////
// EFIVARS
///////////////////////////////////////////////////////////////////////////

/* Comprises EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS */
#define EFIVARS_DEFAULT_ATTR 0x7
#define SOLETTA_EFIVARS_GUID "076027a8-c791-41d7-940f-3d465869f821"
#define EFIVARFS_VAR_PATH "/sys/firmware/efi/efivars/%s-" SOLETTA_EFIVARS_GUID
