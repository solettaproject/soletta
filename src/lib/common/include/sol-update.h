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

#pragma once

#include "sol-str-slice.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to update a Soletta app. Provide way to check, fetch and
 * install updates
 */

/**
 * @defgroup Update Update
 *
 * Updating a Soletta based app may vary depending on Soletta environment and
 * configuration. For instance, updating a static Linux Micro (PID 1) Soletta
 * based app is only a matter of replacing app file, but when using shared libs,
 * may involve update/install newer versions of used libs. So, Soletta provides
 * different update modules to update Soletta based apps on different scenarios.
 * Soletta will try to use the first module it can use, but if having different
 * modules to be loaded, environment variable SOL_UPDATE_MODULE can be used to
 * define a specific one, like SOL_UPDATE_MODULE=linux-micro-efi-update.
 *
 * When Soletta update module cheks for update, it should get version
 * and size of new file to fill @c sol_update_info.
 *
 * Each update module is free to decide how to check, fetch and install the
 * update. Check update module documentation to understand how it works.
 * Note also that comparing obtained version with app current version and
 * deciding to fetch update file is up to user/developer.
 *
 * @{
 */

/**
 * @struct sol_update_handle
 *
 * @brief Handle returned by some sol_update* calls, so they can be cancelled
 * appropriately.
 */
struct sol_update_handle;

/**
 * @brief Contains update info got via @c sol_update_check call.
 *
 * @see sol_update_check
 */
struct sol_update_info {
    const char *version; /**< @brief Current version of update file */
    uint64_t size; /**< @brief Size of update file. Useful to warn user about big downloads. */
};

/**
 * @brief Check if there's an update to get.
 *
 * Module must return a @c sol_update_info containing update size and version.
 *
 * @param cb callback that will be called to return update information.
 * If status < 0, then something went wrong with check and @c response
 * is undefined. If status == 0, check went OK and @c response argument
 * shall contain obtained update information.
 * @param data user defined data to be sent to callback @a cb
 *
 * @return handle of this update task, or NULL if couldn't start the task.
 * Note that handle will be invalid after @a cb is called, or if it was cancelled.
 *
 * @see sol_update_cancel
 */
struct sol_update_handle *sol_update_check(
    void (*cb)(void *data, int status, const struct sol_update_info *response),
    const void *data);

/**
 * @brief Fetch update, so it can be installed later wit @c sol_update_install.
 *
 * Fetch will get the update. Update module should check its hash and
 * signature before informing that task was completed successfuly.
 * If everything is OK, callback @a cb will be called with status == 0.
 *
 * @param cb callback called to inform fetch completion or failure. If
 * status < 0, then something went wrong. If status == 0, fetch went OK
 * and @c sol_update_install can be used to install the update.
 *
 * @param data user specified data to be sent to callback @a cb
 * @param resume NOT IMPLEMENTED. If false, any previous data downloaded
 * shall be discarded. Otherwise, will resume a previous cancelled fetch.
 *
 * @return handle of this update task, or NULL if couldn't start the task.
 * Note that handle will be invalid after @a cb is called, or if it was cancelled.
 *
 * @see sol_update_cancel
 */
struct sol_update_handle *sol_update_fetch(void (*cb)(void *data, int status),
    const void *data, bool resume);

/**
 * @brief Install update.
 *
 * Install process vary depending on update module being used, but
 * all shall give feedback of success or not on @a cb callback.
 *
 * @param cb callback called to inform installation success or failure.
 * If status < 0, then something went wrong. If status == 0, installation
 * completed successfuly.
 * @param data user defined data to be passed to callback @a cb
 *
 * @return handle of this update task, or NULL if couldn't start the task.
 * Note that handle will be invalid after @a cb is called, or if it was cancelled.
 *
 * @see sol_update_cancel
 */
struct sol_update_handle *sol_update_install(void (*cb)(void *data, int status),
    const void *data);

/**
 * @brief Cancel an ongoing check, fetch or install task.
 *
 * Note that cancelling the task may not be possible at any moment, depending
 * on update module being used.
 *
 * @param handle handle of update task to be cancelled.
 *
 * @return true if current task was successfuly cancelled.
 *
 * @note If cancel was successful, then @a handle becomes invalid. If not,
 * task callback shall still be called.
 */
bool sol_update_cancel(struct sol_update_handle *handle);

/**
 * @brief Get progress of given update task.
 *
 * Note that getting the progress may not work for all tasks, depending
 * on update module being used.
 *
 * @param handle handle of update task to get current progress.
 *
 * @return current progress of task between 0 and 100, or -1 if
 * couldn't get progress.
 */
int sol_update_get_progress(struct sol_update_handle *handle);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
