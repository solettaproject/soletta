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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sol-util.h"

#define CHUNK_SIZE 4096

int
sol_util_vwrite_file(const char *path, const char *fmt, va_list args)
{
    FILE *fp;
    int ret;

    fp = fopen(path, "we");
    if (!fp)
        return -errno;

    ret = vfprintf(fp, fmt, args);
    fclose(fp);

    return ret;
}

int
sol_util_write_file(const char *path, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = sol_util_vwrite_file(path, fmt, ap);
    va_end(ap);

    return ret;
}

int
sol_util_vread_file(const char *path, const char *fmt, va_list args)
{
    FILE *fp;
    int ret;

    fp = fopen(path, "re");
    if (!fp)
        return -errno;

    ret = vfscanf(fp, fmt, args);
    fclose(fp);

    return ret;
}

int
sol_util_read_file(const char *path, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = sol_util_vread_file(path, fmt, ap);
    va_end(ap);

    return ret;
}

static ssize_t
_fill_buffer(const int fd, char *buffer, const size_t buffer_size, size_t *size_read)
{
    ssize_t size;
    unsigned int retry = 0;

    *size_read = 0;
    do {
        size = read(fd, buffer + *size_read, buffer_size - *size_read);
        if (size < 0) {
            retry++;
            if (retry >= SOL_UTIL_MAX_READ_ATTEMPTS)
                break;

            if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else
                break;
        }

        retry = 0; //We only count consecutive failures
        *size_read += (size_t)size;
    } while (size && *size_read < buffer_size);

    return size;
}

void *
sol_util_load_file_raw(const int fd, size_t *size)
{
    struct stat st;
    int saved_errno;
    char *tmp = NULL;
    char *buffer = NULL;
    size_t buffer_size = 0;
    ssize_t ret;

    if (fd < 0 || !size)
        return NULL;

    *size = 0;
    if (fstat(fd, &st) >= 0 && st.st_size) {
        buffer_size = st.st_size;
        buffer = malloc(buffer_size);
        if (!buffer)
            goto err;

        ret = _fill_buffer(fd, buffer, buffer_size, size);
        if (ret <= 0)
            goto end;
    }

    do {
        buffer_size += CHUNK_SIZE;
        tmp = realloc(buffer, buffer_size);
        if (!tmp)
            goto err;
        buffer = tmp;

        ret = _fill_buffer(fd, buffer + *size, CHUNK_SIZE, size);
    } while (ret > 0);

end:
    if (ret < 0 || !*size)
        goto err;

    if (*size < buffer_size) {
        tmp = realloc(buffer, *size);
        if (tmp)
            buffer = tmp;
    }

    return buffer;

err:
    saved_errno = errno;
    free(buffer);
    errno = saved_errno;
    *size = 0;
    return NULL;
}

char *
sol_util_load_file_string(const char *filename, size_t *size)
{
    int fd = -1, saved_errno;
    size_t read;
    char *tmp, *data = NULL;

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        goto open_err;

    data = sol_util_load_file_raw(fd, &read);
    if (!data) {
        data = strdup("");
        read = 1;
    } else if (data[read - 1] != '\0') {
        tmp = realloc(data, read + 1);
        if (!tmp)
            goto err;
        data = tmp;
        data[read] = '\0';
    }

    close(fd);
    if (size)
        *size = read;
    return data;

err:
    saved_errno = errno;
    free(data);
    close(fd);
    errno = saved_errno;
open_err:
    if (size)
        *size = 0;
    return NULL;
}

void *
sol_util_memdup(const void *data, size_t len)
{
    void *ptr;

    ptr = malloc(len);
    if (ptr)
        memcpy(ptr, data, len);
    return ptr;
}

struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

char *
sol_util_strerror(int errnum, char *buf, size_t buflen)
{
    char *ret;

    if (buflen < 1)
        return NULL;

    buf[0] = '\0';

    ret = (char *)strerror_r(errnum, buf, buflen);
    /* if buf was used it means it can be XSI version (so ret won't be
       pointing to msg string), or GNU version using non static string
       (in this case ret == buf already) */
    if (buf[0] != '\0')
        ret = buf;

    return ret;
}
