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
#include "sol-util.h"
#include "sol-util-file.h"

struct update_check_hash_handle {
    struct sol_message_digest *md;
    FILE *file;
    char *hash;
    char *hash_algorithm;
    void (*cb)(void *data, int status);
    const void *user_data;
};

static void
delete_handle(struct update_check_hash_handle *handle)
{
    free(handle->hash);
    free(handle->hash_algorithm);
    free(handle);
}

static void
on_digest_ready_cb(void *data, struct sol_message_digest *md, struct sol_blob *output)
{
    struct update_check_hash_handle *handle = data;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice slice = sol_str_slice_from_blob(output);
    int r = 0;

    r = sol_buffer_append_as_base16(&buffer, slice, false);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if (!streq(buffer.data, handle->hash)) {

        r = -EINVAL;
        SOL_WRN("Expected hash differs of file hash, expected [%s], found [%.*s]",
            handle->hash, (int)buffer.used, (char *)buffer.data);
    }

end:
    handle->cb((void *)handle->user_data, r);
    sol_message_digest_del(md);
    sol_buffer_fini(&buffer);
    delete_handle(handle);
}

static void
on_feed_done_cb(void *data, struct sol_message_digest *md, struct sol_blob *input)
{
    struct update_check_hash_handle *handle = data;
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
    if (!size && last) {
        SOL_WRN("Nothing more to feed hash algorithm, ignoring on_feed_done request");
        return;
    }

    blob_backend = malloc(size);
    SOL_NULL_CHECK_GOTO(blob_backend, err);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, blob_backend, size);
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
    handle->cb((void *)handle->user_data, -EINVAL);
    delete_handle(handle);
}

struct update_check_hash_handle *
check_file_hash(FILE *file, const char *hash, const char *hash_algorithm,
    void (*cb)(void *data, int status), const void *data)
{
    struct sol_message_digest_config cfg = {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = hash_algorithm,
        .on_digest_ready = on_digest_ready_cb,
        .on_feed_done = on_feed_done_cb,
    };
    struct sol_message_digest *md;
    struct update_check_hash_handle *handle;

    SOL_NULL_CHECK(file, NULL);
    SOL_NULL_CHECK(hash, NULL);
    SOL_NULL_CHECK(hash_algorithm, NULL);
    SOL_NULL_CHECK(cb, NULL);

    handle = calloc(1, sizeof(struct update_check_hash_handle));
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
    on_feed_done_cb(handle, md, NULL);

    return handle;

err:
    free(handle->hash);
    free(handle->hash_algorithm);
    free(handle);

    return NULL;
}

bool
cancel_check_file_hash(struct update_check_hash_handle *handle)
{
    if (handle->md)
        sol_message_digest_del(handle->md);

    delete_handle(handle);

    return true;
}
