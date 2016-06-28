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

#pragma once

#include "sol-buffer.h"
#include "sol-update.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Helper HTTP functions to update modules.
 */

/**
 * @brief Handle of http tasks 'fetch' and 'get_metadata'
 */
struct update_http_handle;

/**
 * @brief Get metadata from given URL
 *
 * An example of metadata is a JSON containing useful information
 * of update
 *
 * @ param url url to get data
 * @param cb callback that will be called with data. If @c status < 0,
 * something went wrong. Callback @c metadata param contain the metadata.
 * @param data user data that will be passed to callback @a cb
 *
 * @return handle of this task. It can be cancelled via @c http_cancel.
 *
 * @see http_cancel
 */
struct update_http_handle *http_get_metadata(const char *url,
    void (*cb)(void *data, int status, const struct sol_buffer *metadata),
    const void *data);

/**
 * @brief Download a file
 *
 * @param url url to download file
 * @param recv_cb callback that receives download content so far, on
 * its @c buffer param. This callback may be called multiple times to deliver
 * chunks of download file.
 * @param end_cb callback called at end of transfer. Status is the HTTP status
 * of the transfer. So, SOL_HTTP_STATUS_OK means success.
 * @param data user data that will be passed to both callbacks
 * @param resume *NOT IMPLEMENTED* allows resume an interrupted transfer
 *
 * @return handle of this task. It can be cancelled via @c http_cancel.
 *
 * @see http_cancel
 */
struct update_http_handle *http_fetch(const char *url,
    void (*recv_cb)(void *data, const struct sol_buffer *buffer),
    void (*end_cb)(void *data, int status),
    const void *data, bool resume);

/**
 * @brief Cancel an http ongoing task.
 *
 * After this call, no callbacks of task shall be called. It is safe
 * to call it inside callbacks.
 *
 * @param handle to be cancelled
 *
 * @return true if could cancel task.
 */
bool http_cancel(struct update_http_handle *handle);

/**
 * @brief Fills a @c sol_update_info from a JSON buffer.
 *
 * Convenient function that fills a @c sol_update_info from a buffer
 * containing a JSON structured data. This JSON must contain at least the
 * fields "version" and "size".
 *
 * @param metadata from where to get information
 * @param response @c sol_update_info that will be filled with information
 *
 * @return true on success.
 */
bool metadata_to_update_info(const struct sol_buffer *metadata,
    struct sol_update_info *response);

#ifdef __cplusplus
}
#endif
