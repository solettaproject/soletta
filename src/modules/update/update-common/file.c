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

struct check_hash_data {
    FILE *file;
    char *hash;
    char *hash_algorithm;
    void (*cb)(void *data, int status);
    const void *user_data;
};

static void
delete_hash_data(struct check_hash_data *hash_data)
{
    free(hash_data->hash);
    free(hash_data->hash_algorithm);
    free(hash_data);
}

static void
on_digest_ready_cb(void *data, struct sol_message_digest *md, struct sol_blob *output)
{
    struct check_hash_data *hash_data = data;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice slice = sol_str_slice_from_blob(output);
    int r = 0;

    r = sol_buffer_append_as_base16(&buffer, slice, false);
    SOL_INT_CHECK_GOTO(r, < 0, end);

    if (!streq(buffer.data, hash_data->hash)) {

        r = -EINVAL;
        SOL_WRN("Expected hash differs of file hash, expected [%s], found [%.*s]",
            hash_data->hash, (int)buffer.used, (char *)buffer.data);
    }

end:
    hash_data->cb((void *)hash_data->user_data, r);
    sol_message_digest_del(md);
    sol_buffer_fini(&buffer);
    delete_hash_data(hash_data);
}

static void
on_feed_done_cb(void *data, struct sol_message_digest *md, struct sol_blob *input)
{
    struct check_hash_data *hash_data = data;
    char buf[CHUNK_SIZE], *blob_backend = NULL;
    struct sol_blob *blob = NULL;
    size_t size;
    bool last;
    int r;

    size = fread(buf, 1, sizeof(buf), hash_data->file);
    if (ferror(hash_data->file)) {
        SOL_WRN("Could not read file for feed hash algorithm");
        goto err;
    }

    last = feof(hash_data->file);

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
    hash_data->cb((void *)hash_data->user_data, -EINVAL);
    delete_hash_data(hash_data);
}

bool
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
    struct check_hash_data *hash_data;

    SOL_NULL_CHECK(file, false);
    SOL_NULL_CHECK(hash, false);
    SOL_NULL_CHECK(hash_algorithm, false);
    SOL_NULL_CHECK(cb, false);

    hash_data = calloc(1, sizeof(struct check_hash_data));
    SOL_NULL_CHECK(hash_data, false);
    cfg.data = hash_data;

    hash_data->file = file;

    hash_data->hash = strdup(hash);
    SOL_NULL_CHECK_GOTO(hash_data->hash, err);
    hash_data->hash_algorithm = strdup(hash_algorithm);
    SOL_NULL_CHECK_GOTO(hash_data->hash_algorithm, err);
    hash_data->user_data = data;

    md = sol_message_digest_new(&cfg);
    SOL_NULL_CHECK_GOTO(md, err);

    rewind(file);
    hash_data->cb = cb;

    /* Start feeding */
    on_feed_done_cb(hash_data, md, NULL);

    return true;

err:
    free(hash_data->hash);
    free(hash_data->hash_algorithm);
    free(hash_data);

    return false;
}

int
move_file(const char *old_path, const char *new_path)
{
    FILE *new, *old;
    char buf[CHUNK_SIZE];
    size_t size, w_size;
    int r;

    /* First, try to rename */
    r = rename(old_path, new_path);
    SOL_INT_CHECK(r, == 0, r);

    /* If failed, try the hard way */
    errno = 0;

    old = fopen(old_path, "re");
    SOL_NULL_CHECK(old, -errno);

    new = fopen(new_path, "we");
    r = -errno;
    SOL_NULL_CHECK_GOTO(new, err_new);

    while ((size = fread(buf, 1, sizeof(buf), old))) {
        w_size = fwrite(buf, 1, size, new);
        if (w_size != size) {
            r = -EIO;
            goto err;
        }
    }
    if (ferror(old)) {
        r = -EIO;
        goto err;
    }

    if (fflush(new) != 0) {
        r = -errno;
        goto err;
    }
    fclose(new);

    /* Remove old file */
    fclose(old);
    unlink(old_path);

    return 0;

err:
    fclose(new);
    unlink(new_path);
err_new:
    fclose(old);

    /* So errno is not influenced by fclose and unlink above*/
    errno = -r;

    return r;
}
