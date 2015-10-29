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

#include "sol-macros.h"

#include <dirent.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdbool.h>

#define CHUNK_SIZE 4096
#define SOL_UTIL_MAX_READ_ATTEMPTS 10

int sol_util_write_file(const char *path, const char *fmt, ...) SOL_ATTR_PRINTF(2, 3);
int sol_util_vwrite_file(const char *path, const char *fmt, va_list args) SOL_ATTR_PRINTF(2, 0);
int sol_util_read_file(const char *path, const char *fmt, ...) SOL_ATTR_SCANF(2, 3);
int sol_util_vread_file(const char *path, const char *fmt, va_list args) SOL_ATTR_SCANF(2, 0);
struct sol_buffer *sol_util_load_file_raw(const int fd) SOL_ATTR_WARN_UNUSED_RESULT;
char *sol_util_load_file_string(const char *filename, size_t *size) SOL_ATTR_WARN_UNUSED_RESULT;
char *sol_util_load_file_fd_string(const int fd, size_t *size) SOL_ATTR_WARN_UNUSED_RESULT;
int sol_util_get_rootdir(char *out, size_t size) SOL_ATTR_WARN_UNUSED_RESULT;
int sol_util_fd_set_flag(int fd, int flag) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * Fills @a buffer with data read from file @a fd
 *
 * Data read will be *appended* to the end of used buffer (i.e. buffer->used).
 * If one wants data to be inserted at beginning of buffer, then one must
 * call @c sol_buffer_reset() on buffer before calling @c sol_util_fill_buffer.
 *
 * By using this function to fill the buffer, one doesn't need to care about
 * EAGAIN or EINTR being returned by @c read() raw call.
 */
ssize_t sol_util_fill_buffer(const int fd, struct sol_buffer *buffer, const size_t size);
bool sol_util_iterate_dir(const char *path, bool (*iterate_dir_cb)(void *data, const char *dir_path, struct dirent *ent), const void *data);
