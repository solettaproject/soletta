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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Helper HTTP functions to update modules.
 */

/**
 * @struct update_check_hash_handle
 *
 * @brief Handle to cancel check hash operation
 */
struct update_check_hash_handle;

/**
 * @brief Check file hash
 *
 * @param file FILE stream to have its hash checked.
 * @param hash hash to compare with file hash
 * @param hash_algorithm algorithm of hash to be used
 * @param cb callback that will be called to inform check result. If status < 0,
 * hash check failed. If status == 0, hash has been validate successfuly.
 *
 * @return handle of operation if could start checking. NULL otherwise
 */
struct update_check_hash_handle *check_file_hash(FILE *file, const char *hash,
    const char *hash_algorithm, void (*cb)(void *data, int status),
    const void *data);

/**
 * @brief Cancel ongoing file hash checking operation
 *
 * @return always true
 */
bool cancel_check_file_hash(struct update_check_hash_handle *handle);

#ifdef __cplusplus
extern "C" {
#endif
