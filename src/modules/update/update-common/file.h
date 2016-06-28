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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Helper HTTP functions to update modules.
 */

/**
 * @struct update_get_hash_handle
 *
 * @brief Handle to cancel check hash operation
 */
struct update_get_hash_handle;

/**
 * @brief Check file hash
 *
 * @param file FILE stream to have its hash checked.
 * @param hash hash to compare with file hash
 * @param hash_algorithm algorithm of hash to be used
 * @param cb callback that will be called to inform check result. If status < 0,
 * hash check failed. If status == 0, hash has been validate successfully.
 *
 * @return handle of operation if could start checking. NULL otherwise
 */
struct update_get_hash_handle *get_file_hash(FILE *file, const char *hash,
    const char *hash_algorithm, void (*cb)(void *data, int status, const char *hash),
    const void *data);

/**
 * @brief Cancel ongoing file hash checking operation
 *
 * @return always true
 */
bool cancel_get_file_hash(struct update_get_hash_handle *handle);

#ifdef __cplusplus
}
#endif
