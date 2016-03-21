/*
 * This file is part of the Soletta Project
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

struct sol_file_reader;

struct sol_file_reader *sol_file_reader_open(const char *filename);
struct sol_file_reader *sol_file_reader_from_fd(int fd);
void sol_file_reader_close(struct sol_file_reader *fr);
struct sol_str_slice sol_file_reader_get_all(const struct sol_file_reader *fr);
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
