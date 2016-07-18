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

#include "sol-str-slice.h"
#include "sol-types.h"

/**
 * @addtogroup File-utils
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef sol_file_reader
 * @brief Opaque handler for a file reader.
 */
struct sol_file_reader;
typedef struct sol_file_reader sol_file_reader;

/**
 * @brief Open a file using its filename.
 *
 * @param filename The file name of the file to open.
 *
 * @return The file reader associated with the @a file on success or @c NULL
 *         on errors.
 */
struct sol_file_reader *sol_file_reader_open(const char *filename);

/**
 * @brief Create a file reader from a file descriptor.
 *
 * @param fd The file descriptor.
 *
 * @return A file reader to read contents from @a fd on success or @c NULL on
 *         errors.
 */
struct sol_file_reader *sol_file_reader_from_fd(int fd);

/**
 * @brief Closes a file reader, releasing its memory.
 *
 * @param fr A pointer to the file reader to be closed.
 */
void sol_file_reader_close(struct sol_file_reader *fr);

/**
 * @brief Get the content of the file as a sol_str_slice.
 *
 * @param fr The file reader object.
 *
 * @return a sol_str_slice containing all contents of the file. Slice will be
 *         empty if file reader is @c NULL or invalid.
 */
struct sol_str_slice sol_file_reader_get_all(const struct sol_file_reader *fr);

/**
 * @brief Retrieve stat information from a file.
 *
 * @param fr The file reader object.
 *
 * @return A pointer to the stat structure containing stat information on
 *         success or @c NULL on errors.
 */
const struct stat *sol_file_reader_get_stat(const struct sol_file_reader *fr);

/**
 * convert an open file reader to a blob.
 *
 * This will convert a valid and opened file reader to a blob, thus no
 * further explicit calls to sol_file_reader_close() should be done as
 * the blob will automatically close once its last reference is gone.
 *
 * @param fr valid opened file reader.
 *
 * @return NULL on error (and fr is closed) or new blob instance that should
 *         be sol_blob_unref() once it's not needed.
 */
struct sol_blob *sol_file_reader_to_blob(struct sol_file_reader *fr);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
