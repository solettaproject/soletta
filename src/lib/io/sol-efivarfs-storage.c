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

#include "sol-efivarfs-storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"

#define SOLETTA_EFIVARS_GUID "076027a8-c791-41d7-940f-3d465869f821"
#define EFIVARFS_VAR_DIR "/sys/firmware/efi/efivars/"
#define EFIVARFS_VAR_PATH EFIVARFS_VAR_DIR "%s-" SOLETTA_EFIVARS_GUID

struct pending_write_data {
    char *name;
    struct sol_blob *blob;
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status);
    const void *data;
    int status;
};

static struct sol_ptr_vector pending_writes = SOL_PTR_VECTOR_INIT;

static const int EFIVARS_DEFAULT_ATTR = 0x7;

static bool
perform_pending_write(void *data)
{
    FILE *file = NULL;
    char path[PATH_MAX];
    struct pending_write_data *pending_write = data;
    int r;

    if (pending_write->status == -ECANCELED)
        goto callback;

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, pending_write->name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file [%s]", path);
        pending_write->status = -EINVAL;
        goto callback;
    }

    file = fopen(path, "w+e");
    if (!file) {
        SOL_WRN("Could not open persistence file [%s]: %s", path,
            sol_util_strerrora(errno));
        pending_write->status = -errno;
        goto callback;
    }

    fwrite(&EFIVARS_DEFAULT_ATTR, sizeof(EFIVARS_DEFAULT_ATTR), 1, file);
    if (ferror(file)) {
        SOL_WRN("Coud not write peristence file [%s] attributes", path);
        r = -EIO;
        goto close;
    }

    fwrite(pending_write->blob->mem, pending_write->blob->size, 1, file);
    if (ferror(file)) {
        SOL_WRN("Could not write to persistence file [%s]", path);
        pending_write->status = -EIO;
    }

close:
    if (fclose(file) < 0 && !pending_write->status)
        pending_write->status = -errno;

callback:
    pending_write->cb((void *)pending_write->data, pending_write->name,
        pending_write->blob, pending_write->status);

    sol_blob_unref(pending_write->blob);
    free(pending_write->name);
    sol_ptr_vector_remove(&pending_writes, pending_write);
    free(pending_write);

    return false;
}

static void
cancel_pending_write(const char *name)
{
    struct pending_write_data *pending_write;
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (&pending_writes, pending_write, i) {
        if (streq(pending_write->name, name))
            pending_write->status = -ECANCELED;
    }
}

static bool
read_from_pending(const char *name, struct sol_buffer *buffer)
{
    struct pending_write_data *pending_write;
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (&pending_writes, pending_write, i) {
        if (streq(pending_write->name, name)) {
            if (sol_buffer_ensure(buffer, pending_write->blob->size) < 0) {
                SOL_WRN("Could not ensure buffer size to fit pending blob");
                return false;
            }
            memcpy(buffer->data, pending_write->blob->mem, pending_write->blob->size);
            return true;
        }
    }

    return false;
}

SOL_API int
sol_efivars_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    struct pending_write_data *pending_write;
    struct sol_timeout *timeout;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);

    cancel_pending_write(name);

    pending_write = calloc(1, sizeof(struct pending_write_data));
    SOL_NULL_CHECK(pending_write, -ENOMEM);

    pending_write->blob = sol_blob_ref(blob);
    SOL_NULL_CHECK_GOTO(pending_write->blob, error);

    pending_write->blob = blob;
    pending_write->data = data;
    pending_write->cb = cb;
    pending_write->name = strdup(name);
    SOL_NULL_CHECK_GOTO(pending_write->name, error);

    timeout = sol_timeout_add(0, perform_pending_write, pending_write);
    SOL_NULL_CHECK_GOTO(timeout, error);

    if (sol_ptr_vector_append(&pending_writes, pending_write) < 0)
        goto error;

    return 0;

error:
    if (pending_write->blob)
        sol_blob_unref(blob);

    free(pending_write->name);
    sol_ptr_vector_remove(&pending_writes, pending_write);
    free(pending_write);

    return -ENOMEM;
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

    if (read_from_pending(name, buffer))
        return 0;

    r = snprintf(path, sizeof(path), EFIVARFS_VAR_PATH, name);
    if (r < 0 || r >= PATH_MAX) {
        SOL_WRN("Could not create path for efivars persistence file [%s]", path);
        return -EINVAL;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SOL_INF("Could not open persistence file [%s]: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }

    /* Read (to discard) the first uint32_t containing the attributes */
    r = sol_util_fill_buffer_exactly(fd, &attr, sizeof(uint32_t));
    if (r < 0) {
        SOL_WRN("Could not read persistence file [%s] attributes", path);
        goto end;
    }

    r = sol_util_load_file_fd_buffer(fd, buffer);

end:
    close(fd);

    return r;
}
