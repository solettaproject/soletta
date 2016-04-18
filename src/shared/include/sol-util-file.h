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

#include <sol-macros.h>
#include <sol-buffer.h>

#include <dirent.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Useful general file routines.
 */

/**
 * @defgroup File-utils Soletta file utility functions
 * @ingroup Utils
 *
 * @brief Contains helpers to deal with files.
 *
 * @{
 */

/**
 * @brief Max number of read attempts. It's used when a read operation
 * returns @c EAGAIN or @c EINTR.
 */
#define SOL_UTIL_MAX_READ_ATTEMPTS 10

/**
 * @brief Write the formatted string in the file pointed by @c path.
 *
 * @param path The path to a valid file.
 * @param fmt The string format.
 *
 * @return The number of written characters, if an error is encountered a
 * negative value with the error.
 *
 * @see sol_util_vwrite_file
 */
int sol_util_write_file(const char *path, const char *fmt, ...) SOL_ATTR_PRINTF(2, 3);

/**
 * @brief Write the formatted string in the file pointed by @c path.
 *
 * It is equivalent to @c sol_util_write_file except it receives @c
 * va_list instead of a variable number of arguments.
 *
 * @param path The path to a valid file.
 * @param fmt The string format
 * @param args The format arguments.
 *
 * @return The number of written characters, if an error is encountered a
 * negative value with the error.
 *
 * @see sol_util_write_file
 */
int sol_util_vwrite_file(const char *path, const char *fmt, va_list args) SOL_ATTR_PRINTF(2, 0);

/**
 * @brief Reads from a file the contents according with the formatted string.
 *
 * @param path The path to a valid file.
 * @param fmt The string format.
 *
 * @return The number of read characters, if an error is encountered a
 * negative value with the error.
 *
 * @see sol_util_vread_file
 */
int sol_util_read_file(const char *path, const char *fmt, ...) SOL_ATTR_SCANF(2, 3);

/**
 * @brief Reads from a file the contents according with the formatted string.
 *
 * It is equivalent to @c sol_util_read_file except it receives @c
 * va_list instead of a variable number of arguments.
 *
 * @param path The path to a valid file.
 * @param fmt The string format.
 * @param args The format arguments.
 *
 * @return The number of read characters, if an error is encountered a
 * negative value with the error.
 *
 * @see sol_util_vread_file
 */
int sol_util_vread_file(const char *path, const char *fmt, va_list args) SOL_ATTR_SCANF(2, 0);

/**
 * @brief Reads the contents of a file.
 *
 * The data read is put not formatted in a buffer. @note It's the caller
 * responsibility to release the buffer's memory.
 *
 * @param fd A valid file descriptor.
 *
 * @return A buffer with the data read in success, otherwise @c NULL.
 *
 * @see sol_util_load_file_string
 */
struct sol_buffer *sol_util_load_file_fd_raw(const int fd) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Reads the contents of a file.
 *
 * @note It's the caller responsibility to release the memory.
 *
 * @param filename A file's path.
 * @param size A variable to hold the amount of read data.
 *
 * @return A string with the file's content on success, otherwise NULL.
 *
 * @see sol_util_load_file_fd_string
 */
char *sol_util_load_file_string(const char *filename, size_t *size) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Reads the contents of a file.
 *
 * @note It's the caller responsibility to release the memory.
 *
 * @param fd A valid file descriptor.
 * @param size A variable to hold the amount of read data.
 *
 * @return A string with the file's content on success, otherwise NULL.
 *
 * @see sol_util_load_file_string
 */
char *sol_util_load_file_fd_string(const int fd, size_t *size) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Reads the contents of a file and append to a buffer.
 *
 * @param fd A valid file descriptor.
 * @param buf A buffer to append the file contents - It must be initialized.
 *
 * @return 0 on success, negative errno otherwise.
 */
int sol_util_load_file_fd_buffer(int fd, struct sol_buffer *buf) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Reads the contents of a file and append to a buffer.
 *
 * @param filename THe file path to be read.
 * @param buf A buffer to append the file contents - It must be initialized.
 *
 * @return 0 on success, negative errno otherwise.
 */
int sol_util_load_file_buffer(const char *filename, struct sol_buffer *buf) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Gets the root directory.
 *
 * @param out A variable to hold the root directory.
 * @param size The out's size
 *
 * @return 0 on success, otherwise a negative value with the error.
 */
int sol_util_get_rootdir(char *out, size_t size) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Set a flag into a file descriptor.
 *
 * @param fd A valid file descriptor.
 * @param flag The desired flag.
 *
 * @return 0 on success, otherwise a negative value with the error.
 */
int sol_util_fd_set_flag(int fd, int flag) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Fills @a buffer with data read from file @a fd.
 *
 * Data read will be *appended* to the end of used buffer (i.e. buffer->used).
 * If one wants data to be inserted at beginning of buffer, then one must
 * call sol_buffer_reset() on buffer before calling sol_util_fill_buffer().
 *
 * By using this function to fill the buffer, one doesn't need to care about
 * @c EAGAIN or @c EINTR being returned by @c read() raw call.
 *
 * @param fd A valid file descriptor.
 * @param buffer The buffer that will hold the file's content.
 * @param size Read up to this amount of bytes.
 *
 * @return The size of bytes filled in success, otherwise a negative
 * error indicating what failure happened. Note that the amount of
 * bytes may be smaller than the requested.
 *
 * @see sol_util_fill_buffer_exactly()
 */
ssize_t sol_util_fill_buffer(const int fd, struct sol_buffer *buffer, const size_t size);

/**
 * @brief Fills @a buffer with data read from file @a fd with an exact amount of bytes.
 *
 * Data read will be *appended* to the end of used buffer
 * (i.e. buffer->used).  If one wants data to be inserted at beginning
 * of buffer, then one must call sol_buffer_reset() on buffer before
 * calling sol_util_fill_buffer_exactly()
 *
 * By using this function to fill the buffer, one doesn't need to care
 * about @c EAGAIN or @c EINTR being returned by @c read() raw call.
 *
 * This is a convenience function that calls sol_util_fill_buffer()
 * and check if the read amount is exactly the given @a size,
 * otherwise considers a failure.
 *
 * @param fd A valid file descriptor.
 * @param buffer The buffer that will hold the file's content.
 * @param size Read exactly this amount of bytes.
 *
 * @return 0 on success, -EIO if the number of bytes read is smaller
 * than @a size or -errno on errrors.
 *
 * @see sol_util_fill_buffer()
 */
static inline int
sol_util_fill_buffer_exactly(const int fd, struct sol_buffer *buffer, const size_t size)
{
    ssize_t ret = sol_util_fill_buffer(fd, buffer, size);

    if (ret < 0)
        return (int)ret;
    else if ((size_t)ret < size)
        return -EIO;

    return 0;
}

/**
 * @brief Iterate over a directory.
 *
 * @param path A valid path.
 * @param iterate_dir_cb The callback called in which directory found.
 * @param data The user data which will be passed in the callback.
 *
 * @return @c true on success, otherwise @c false.
 */
bool sol_util_iterate_dir(const char *path, bool (*iterate_dir_cb)(void *data, const char *dir_path,
    struct dirent *ent), const void *data);

/**
 * @brief Moves file on filesystem
 *
 * This function shall move file from @a old_path to @a new_path, and it only
 * returns success if ensures that file was written to storage, using @c fsync(3)
 *
 * @param old_path path of file to be moved
 * @param new_path to where move file
 * @param mode mode that should be applied to @a new_file
 *
 * @return On success 0. On failure, a negative error indicating what failure
 * happened.
 *
 * @note This function may take time to complete its task, thus blocking the
 * mainloop. You may want to use a thread or idler to call it.
 */
int sol_util_move_file(const char *old_path, const char *new_path, mode_t mode);

/**
 * @brief Wait for some file to become available.
 *
 * When dealing with sysfs, it's really common to perform an action that
 * will create new files on sysfs. This function helps those who need
 * to wait for some of these files.
 *
 * @param path where to look for the file
 * @param nanoseconds how much time to wait
 *
 * @return @c true if file come to existence before @a nanoseconds.
 * @c false if wait ended after timeout
 *
 * @note This function blocks current thread on a busy wait. Use with
 * caution.
 */
bool sol_util_busy_wait_file(const char *path, uint64_t nanoseconds);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
