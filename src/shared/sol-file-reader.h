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
#include "sol-types.h"

struct sol_file_reader;

struct sol_file_reader *sol_file_reader_open(const char *filename);
struct sol_file_reader *sol_file_reader_fd(int fd);
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
