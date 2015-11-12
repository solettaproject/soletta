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

#include "sol-update.h"

/* TODO think of a better way to name file. Maybe get from modules? */
#define SOL_UPDATE_FILE_NAME "sol-update-file"

/**
 * @file
 * @brief Common routines to update modules.
 */

/**
 * Provides common functions to a Soletta update module, like
 * check, fetch or move_file. It's up to each update module
 * to use or not this provided methods or structs, although
 * using @c sol_update_handle implementation given here as base
 * implementation is highly advised.
 * Note that there are no common_install, as this is essentially
 * the update module task.
 */

enum task {
    TASK_CHECK,
    TASK_FETCH,
    TASK_UPDATE
};

/**
 * Base sol_update_handle, that can be used by common_* functions
 * provided.
 */
struct sol_update_handle {
    enum task task;
    struct sol_http_client_connection *conn;
    char *url;
    char *hash;
    char *hash_algorithm;
    char *file_path;
    FILE *file; /**< Used exclusively when digesting file */

    union {
        void (*cb_check)(void *data, int status, const struct sol_update_info *response);
        void (*cb_fetch)(void *data, int status, const char *file_path);
        void (*cb_install)(void *data, int status);
    };
    void (*cb_hash)(struct sol_update_handle *handle, int status);

    struct sol_timeout *timeout;

    const void *user_data;
};

/**
 * Perform a check. Will try to connect to given URL and fill a
 * @c sol_update_info to give on callback
 */
struct sol_update_handle *common_check(const char *url,
    void (*cb)(void *data, int status, const struct sol_update_info *response),
    const void *data);

/**
 * Fetche update file and checks its hash, using given information
 */
struct sol_update_handle *common_fetch(const struct sol_update_info *info,
    void (*cb)(void *data, int status, const char *file_path),
    const void *data, bool resume);

/**
 * Cancel a check or fetch task. Cannot cancel a install task,
 * as no common_install is provided.
 */
bool common_cancel(struct sol_update_handle *handle);

/**
 * Get progress of check or fetch task. Cannect cancel a install task,
 * as no common_install is provided.
 */
int common_get_progress(struct sol_update_handle *handle);

/**
 * Move a file from old_path to new_path. It first tries a rename(2),
 * but if it fails, do a 'hard copy'
 */
int common_move_file(const char *old_path, const char *new_path);

/**
 * Free a sol_update_handle and all it's members.
 */
void delete_handle(struct sol_update_handle *handle);
