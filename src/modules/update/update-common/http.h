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
struct http_update_handle;

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
struct http_update_handle *http_get_metadata(const char *url,
    void (*cb)(void *data, int status, const struct sol_buffer *metadata),
    const void *data);

/**
 * @brief Download a file
 *
 * @param url url to download file
 * @param cb_write callback that receives download content so far, on
 * its @c buffer param. This callback may be called multiple times to deliver
 * chunks of download file.
 * @param cb_end callback called at end of transfer. Status is the HTTP status
 * of the transfer. So, SOL_HTTP_STATUS_OK means success.
 * @param data user data that will be passed to both callbacks
 * @param resume *NOT IMPLEMENTED* allows resume an interrupted transfer
 *
 * @return handle of this task. It can be cancelled via @c http_cancel.
 *
 * @see http_cancel
 */
struct http_update_handle *http_fetch(const char *url,
    void (*cb_write)(void *data, struct sol_buffer *buffer),
    void (*cb_end)(void *data, int status),
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
bool http_cancel(struct http_update_handle *handle);

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
