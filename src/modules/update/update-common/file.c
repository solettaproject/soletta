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

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file.h"

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-message-digest.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"

#define CHUNK_SIZE 4096

struct update_get_hash_handle {
    struct sol_message_digest *md;
    FILE *file;
    char *hash;
    char *hash_algorithm;
    void (*cb)(void *data, int status, const char *hash);
    const void *user_data;
};

static void
delete_handle(struct update_get_hash_handle *handle)
{
    free(handle->hash);
    free(handle->hash_algorithm);
    free(handle);
}

static void
on_digest_ready_cb(void *data, struct sol_message_digest *md, struct sol_blob *output)
{
    struct update_get_hash_handle *handle = data;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice slice = sol_str_slice_from_blob(output);
    int r = 0;

    r = sol_buffer_append_as_base16(&buffer, slice, false);
    SOL_INT_CHECK_GOTO(r, < 0, end);

end:
    handle->cb((void *)handle->user_data, r, (char *)buffer.data);
    sol_message_digest_del(md);
    sol_buffer_fini(&buffer);
    delete_handle(handle);
}

static void
on_feed_done_cb(void *data, struct sol_message_digest *md, struct sol_blob *input, int status)
{
    struct update_get_hash_handle *handle = data;
    char buf[CHUNK_SIZE], *blob_backend = NULL;
    struct sol_blob *blob = NULL;
    size_t size;
    bool last;
    int r;

    size = fread(buf, 1, sizeof(buf), handle->file);
    if (ferror(handle->file)) {
        SOL_WRN("Could not read file for feed hash algorithm");
        goto err;
    }

    last = feof(handle->file);

    /* TODO Maybe this is a bug on sol_message_digest? Keeps calling on_feed_done
     * after send last chunk */
    if (!size && last && input) {
        SOL_WRN("Nothing more to feed hash algorithm, ignoring on_feed_done request");
        return;
    }

    blob_backend = malloc(size);
    SOL_NULL_CHECK_GOTO(blob_backend, err);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, blob_backend, size);
    SOL_NULL_CHECK_GOTO(blob, err);

    memcpy(blob_backend, buf, size);

    r = sol_message_digest_feed(md, blob, last);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    sol_blob_unref(blob);
    return;

err:
    SOL_WRN("Could not feed data to check update file hash");
    free(blob_backend);
    sol_blob_unref(blob);
    sol_message_digest_del(md);
    handle->cb((void *)handle->user_data, -EINVAL, NULL);
    delete_handle(handle);
}

struct update_get_hash_handle *
get_file_hash(FILE *file, const char *hash, const char *hash_algorithm,
    void (*cb)(void *data, int status, const char *hash), const void *data)
{
    struct sol_message_digest_config cfg = {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = hash_algorithm,
        .on_digest_ready = on_digest_ready_cb,
        .on_feed_done = on_feed_done_cb,
    };
    struct sol_message_digest *md;
    struct update_get_hash_handle *handle;

    SOL_NULL_CHECK(file, NULL);
    SOL_NULL_CHECK(hash, NULL);
    SOL_NULL_CHECK(hash_algorithm, NULL);
    SOL_NULL_CHECK(cb, NULL);

    handle = calloc(1, sizeof(struct update_get_hash_handle));
    SOL_NULL_CHECK(handle, NULL);
    cfg.data = handle;

    handle->file = file;

    handle->hash = strdup(hash);
    SOL_NULL_CHECK_GOTO(handle->hash, err);
    handle->hash_algorithm = strdup(hash_algorithm);
    SOL_NULL_CHECK_GOTO(handle->hash_algorithm, err);
    handle->user_data = data;

    md = sol_message_digest_new(&cfg);
    SOL_NULL_CHECK_GOTO(md, err);

    handle->md = md;

    rewind(file);
    handle->cb = cb;

    /* Start feeding */
    on_feed_done_cb(handle, md, NULL, 0);

    return handle;

err:
    free(handle->hash);
    free(handle->hash_algorithm);
    free(handle);

    return NULL;
}

bool
cancel_get_file_hash(struct update_get_hash_handle *handle)
{
    if (handle->md)
        sol_message_digest_del(handle->md);

    delete_handle(handle);

    return true;
}
