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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-efi-update");

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "update-common/file.h"
#include "update-common/http.h"

#include "sol-buffer.h"
#include "sol-http.h"
#include "sol-json.h"
#include "sol-mainloop.h"
#include "sol-update.h"
#include "sol-update-modules.h"
#include "sol-util.h"
#include "sol-vector.h"

#define SOL_UPDATE_FILE_NAME "sol-update-file"
#define BACKUP_SUFFIX "_old"

enum task {
    TASK_CHECK,
    TASK_FETCH,
    TASK_UPDATE
};

enum fetch_task {
    FETCH_DOWNLOAD = 0,
    FETCH_CHECK_HASH = 50
};

struct sol_update_handle {
    enum task task;
    enum fetch_task fetch_task;
    struct update_http_handle *http_handle;
    struct update_check_hash_handle *check_hash_handle;
    FILE *file;
    struct sol_timeout *timeout;
    char *hash;
    char *hash_algorithm;

    union {
        void (*cb_check)(void *data, int status, const struct sol_update_info *response);
        void (*cb_fetch)(void *data, int status);
        void (*cb_install)(void *data, int status);
    };
    const void *user_data;

    size_t size;

    bool on_callback : 1;
    bool resume : 1;
};

static char *update_fetch_url;
static char *update_meta_url;
static char *soletta_exec_file_path;

static struct sol_ptr_vector handles = SOL_PTR_VECTOR_INIT;

static void
delete_handle(struct sol_update_handle *handle)
{
    free(handle->hash);
    free(handle->hash_algorithm);
    if (handle->http_handle)
        http_cancel(handle->http_handle);
    sol_ptr_vector_remove(&handles, handle);
    free(handle);
}

static bool
install_timeout(void *data)
{
    struct sol_update_handle *handle = data;
    struct st;
    char *backup_path = NULL;
    int r;

    handle->timeout = NULL;
    handle->on_callback = true;

    r = asprintf(&backup_path, "%s%s", soletta_exec_file_path, BACKUP_SUFFIX);
    if (r < 0) {
        SOL_WRN("Could not create backup file name: %s",
            sol_util_strerrora(errno));
        backup_path = NULL;
        goto end;
    }

    r = rename(soletta_exec_file_path, backup_path);
    if (r < 0) {
        /* If there's no current file - i.e., a failed update happened before
         * Let's just write a new one. */
        if (errno != ENOENT) {
            SOL_WRN("Could not create backup file: %s", sol_util_strerrora(errno));
            goto end;
        }
    }

    /* We create an 'updating' file on /boot, so EFI startup.nsh knows that
     * we are updating. It will then create 'check_update' file there, and
     * remove 'updating'.
     * Once we restart soletta, we erase 'check-update' if we are not
     * 'SOLETTA_EXEC_FILE_OLD', which means that update failed */
    if (sol_util_write_file("/boot/updating", "1") < 0) {
        SOL_WRN("Could not create '/boot/updating' guard file");
        goto end;
    }

    r = sol_move_file(SOL_UPDATE_FILE_NAME, soletta_exec_file_path,
        S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH);
    if (r < 0) {
        SOL_WRN("Could not install update file: %s", sol_util_strerrora(errno));
        goto end;
    }

    errno = 0;

end:
    handle->cb_install((void *)handle->user_data, -errno);
    delete_handle(handle);
    free(backup_path);

    return false;
}

static struct sol_update_handle *
install(void (*cb)(void *data, int status), const void *data)
{
    struct sol_update_handle *handle;

    SOL_NULL_CHECK(cb, NULL);

    handle = calloc(1, sizeof(struct sol_update_handle));
    SOL_NULL_CHECK(handle, NULL);

    handle->task = TASK_UPDATE;
    handle->cb_install = cb;
    handle->user_data = data;

    handle->timeout = sol_timeout_add(0, install_timeout, handle);
    SOL_NULL_CHECK_MSG_GOTO(handle->timeout, err, "Could not create timeout");

    return handle;

err:
    free(handle);

    return NULL;
}

static void
check_post_install(void)
{
    char *cmdline = NULL, *backup_path = NULL;
    int r;

    r = asprintf(&backup_path, "%s%s", soletta_exec_file_path, BACKUP_SUFFIX);
    SOL_INT_CHECK(r, <= 0);

    r = sol_util_read_file("/proc/self/cmdline", "%ms", &cmdline);
    SOL_INT_CHECK_GOTO(r, <= 0, end);

    if (strstartswith(cmdline, backup_path)) {
        SOL_WRN("Running backuped Soletta. Failed update?");
        goto end;
    }

    unlink("/boot/check-update");

end:
    free(cmdline);
    free(backup_path);
}

static void
check_hash_cb(void *data, int status)
{
    struct sol_update_handle *handle = data;

    handle->check_hash_handle = NULL;

    handle->cb_fetch((void *)handle->user_data, status);

    delete_handle(handle);
}

static void
fetch_end_cb(void *data, int status)
{
    struct sol_update_handle *handle = data;
    int fd;

    handle->http_handle = NULL;
    handle->on_callback = true;

    if (status != SOL_HTTP_STATUS_OK) {
        SOL_WRN("Failure getting update file, connection returned: %d", status);
        handle->cb_fetch((void *)handle->user_data, -EINVAL);
        delete_handle(handle);
        return;
    }

    if (fflush(handle->file) != 0) {
        SOL_WRN("Could not write temporary file: %s", sol_util_strerrora(errno));
        goto file_err;
    }

    /* Sync file contents to storage */
    fd = fileno(handle->file);

    if (fsync(fd) < 0) {
        SOL_WRN("Could not ensure temporary file is synced to storage: %s",
            sol_util_strerrora(errno));
        close(fd);
        goto file_err;
    }

    /* Check hash */
    handle->fetch_task = FETCH_CHECK_HASH;
    handle->check_hash_handle = check_file_hash(handle->file, handle->hash,
        handle->hash_algorithm, check_hash_cb, handle);
    SOL_NULL_CHECK_MSG_GOTO(handle->check_hash_handle, hash_err,
        "Could not check hash of downloaded file");

    /* TODO fetch should also verify a file signature, to be sure that we
     * downloaded the file from a trusted source */

    handle->on_callback = false;

    return;

hash_err:
    fclose(handle->file);
file_err:
    handle->cb_fetch((void *)handle->user_data, -errno);
    delete_handle(handle);
}

static void
fetch_recv_cb(void *data, struct sol_buffer *buffer)
{
    struct sol_update_handle *handle = data;
    int fd;

    if (!handle->file) {
        fd = open(SOL_UPDATE_FILE_NAME, O_CREAT | O_RDWR | O_CLOEXEC,
            S_IRUSR | S_IWUSR);
        if (fd < 0) {
            SOL_WRN("Could not create temporary file: %s",
                sol_util_strerrora(errno));
            goto err_create;
        }
        /* Don't care about errors, as this is more 'nice to have' than a must */
        /* TODO should I care about EINTR, though? */
        ftruncate(fd, handle->size);
        handle->file = fdopen(fd, "w+e");
        if (!handle->file) {
            SOL_WRN("Could not create temporary file: %s",
                sol_util_strerrora(errno));
            close(fd);
            goto err_create;
        }
    }

    fwrite(buffer->data, 1, buffer->used, handle->file);
    if (ferror(handle->file)) {
        SOL_WRN("Could not write temporary file");
        goto err_write;
    }

    return;

err_write:
    fclose(handle->file);
err_create:
    handle->on_callback = true;
    handle->cb_fetch((void *)handle->user_data, -EINVAL);
    delete_handle(handle);
}

static void
meta_cb(void *data, int status, const struct sol_buffer *meta)
{
    struct sol_update_handle *handle = data;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    char *fetch_url = NULL, *version = NULL;
    int r;

    handle->http_handle = NULL;

    SOL_NULL_CHECK_MSG_GOTO(meta, err,
        "Could not get meta information about update");

    sol_json_scanner_init(&scanner, meta->data, meta->used);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "hash"))
            handle->hash = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "hash-algorithm"))
            handle->hash_algorithm = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "version"))
            version = sol_json_token_get_unescaped_string_copy(&value);
        else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "size")) {
            if (sol_json_token_get_uint64(&value, &handle->size) != 0)
                SOL_WRN("Could not get size of update file");
        } else
            SOL_WRN("Unknown response member: %.*s",
                SOL_STR_SLICE_PRINT(sol_json_token_to_slice(&token)));
    }

    if (!handle->hash || !handle->hash_algorithm || !version) {
        SOL_WRN("Malformed response of meta information");
        goto err;
    }

    r = asprintf(&fetch_url, "%s/%s", update_fetch_url, version);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    handle->http_handle = http_fetch(fetch_url, fetch_recv_cb,
        fetch_end_cb, handle, handle->resume);
    SOL_NULL_CHECK_MSG_GOTO(handle->http_handle, err,
        "Could not create HTTP connection to fetch update");

    free(version);
    free(fetch_url);

    return;

err:
    handle->cb_fetch((void *)handle->user_data, -EINVAL);
    delete_handle(handle);
    free(version);
    free(fetch_url);
}

static struct sol_update_handle *
fetch(void (*cb)(void *data, int status), const void *data, bool resume)
{
    struct sol_update_handle *handle;

    handle = calloc(1, sizeof(struct sol_update_handle));
    SOL_NULL_CHECK(handle, NULL);
    handle->cb_fetch = cb;
    handle->user_data = data;
    handle->resume = resume;
    handle->task = TASK_FETCH;
    handle->fetch_task = FETCH_DOWNLOAD;

    if (sol_ptr_vector_append(&handles, handle))
        goto err_append;

    /* First, let's get hash and hash-algorithm */
    handle->http_handle = http_get_metadata(update_meta_url, meta_cb, handle);
    SOL_NULL_CHECK_MSG_GOTO(handle->http_handle, err_http,
        "Could not create HTTP connection to get information about update");

    return handle;

err_http:
    sol_ptr_vector_del_last(&handles);
err_append:
    free(handle);
    return NULL;
}

static void
check_cb(void *data, int status, const struct sol_buffer *meta)
{
    struct sol_update_handle *handle = data;
    struct sol_update_info response = { };

    handle->on_callback = true;
    handle->http_handle = NULL;

    if (status < 0) {
        handle->cb_check((void *)handle->user_data, status, NULL);
    } else {
        if (!metadata_to_update_info(meta, &response))
            handle->cb_check((void *)handle->user_data, -EINVAL, NULL);
        else
            handle->cb_check((void *)handle->user_data, 0, &response);
    }

    free((char *)response.version);

    delete_handle(handle);
}

static struct sol_update_handle *
check(void (*cb)(void *data, int status, const struct sol_update_info *response),
    const void *data)
{
    struct sol_update_handle *handle;

    handle = calloc(1, sizeof(struct sol_update_handle));
    SOL_NULL_CHECK(handle, NULL);
    handle->cb_check = cb;
    handle->user_data = data;
    handle->task = TASK_CHECK;

    if (sol_ptr_vector_append(&handles, handle))
        goto err_append;

    handle->http_handle = http_get_metadata(update_meta_url, check_cb, handle);
    SOL_NULL_CHECK_MSG_GOTO(handle->http_handle, err_http,
        "Could not create HTTP connection to check for update");

    return handle;

err_http:
    sol_ptr_vector_del_last(&handles);
err_append:
    free(handle);
    return NULL;
}

static bool
cancel(struct sol_update_handle *handle)
{
    bool b = false;

    if (handle->http_handle)
        b = http_cancel(handle->http_handle);
    else if (handle->check_hash_handle)
        b = cancel_check_file_hash(handle->check_hash_handle);

    if (b)
        delete_handle(handle);

    return b;
}

static int
get_progress(struct sol_update_handle *handle)
{
    if (handle->task == TASK_FETCH) {
        /* Divide task into two subtasks, so progress can account for
         * download and check. */
        if (handle->size && handle->file) {
            long position = ftell(handle->file) / 2;
            float progress = (float)position / (float)handle->size;
            return (int)(progress * 100.0) + handle->fetch_task;
        }
        return 0;
    }
    return -1;
}

static int
init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    update_fetch_url = getenv("SOL_UPDATE_FETCH_URL");
    if (update_fetch_url)
        update_fetch_url = strdup(update_fetch_url);

#ifdef LINUX_MICRO_EFI_UPDATE_FETCH_URL
    if (!update_fetch_url && strlen(LINUX_MICRO_EFI_UPDATE_FETCH_URL))
        update_fetch_url = strdup(LINUX_MICRO_EFI_UPDATE_FETCH_URL);
#endif

    update_meta_url = getenv("SOL_UPDATE_META_URL");
    if (update_meta_url)
        update_meta_url = strdup(update_meta_url);

#ifdef LINUX_MICRO_EFI_UPDATE_META_URL
    if (!update_meta_url && strlen(LINUX_MICRO_EFI_UPDATE_META_URL))
        update_meta_url = strdup(LINUX_MICRO_EFI_UPDATE_META_URL);
#endif

    soletta_exec_file_path = getenv("SOL_APP_FILE_PATH");
    if (soletta_exec_file_path)
        soletta_exec_file_path = strdup(soletta_exec_file_path);

#ifdef LINUX_MICRO_EFI_UPDATE_APP_PATH
    if (!soletta_exec_file_path && strlen(LINUX_MICRO_EFI_UPDATE_APP_PATH))
        soletta_exec_file_path = strdup(LINUX_MICRO_EFI_UPDATE_APP_PATH);
#endif
    SOL_NULL_CHECK_MSG_GOTO(update_meta_url, err,
        "No valid URL to get update metadata. Missing build config or "
        "export SOL_UPDATE_META_URL='URL'?");

    SOL_NULL_CHECK_MSG_GOTO(update_fetch_url, err,
        "No valid URL to fetch updates. Missing build config or "
        "export SOL_UPDATE_FETCH_URL='URL'?");

    SOL_NULL_CHECK_MSG_GOTO(soletta_exec_file_path, err,
        "No path of Soletta application. Missing build config or "
        "export SOL_APP_FILE_PATH='PATH'?");

    SOL_DBG("Using followings URLs to get update:\n"
        "Application path: %s\n"
        "Meta URL: %s\n"
        "Fetch URL: %s",
        soletta_exec_file_path, update_meta_url, update_fetch_url);

    check_post_install();

    return 0;

err:
    free(update_meta_url);
    free(update_fetch_url);
    free(soletta_exec_file_path);

    return -EINVAL;
}

static void
shutdown(void)
{
    struct sol_update_handle *handle;
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (&handles, handle, i) {
        /* Try to cancel pending tasks, if fail, free handles. */
        if (!sol_update_cancel(handle))
            delete_handle(handle);
    }
    sol_ptr_vector_clear(&handles);

    free(update_meta_url);
    free(update_fetch_url);
    free(soletta_exec_file_path);
}

SOL_UPDATE_DECLARE(LINUX_MICRO_EFI_UPDATE,
    .check = check,
    .fetch = fetch,
    .cancel = cancel,
    .get_progress = get_progress,
    .install = install,
    .init = init,
    .shutdown = shutdown
    );
