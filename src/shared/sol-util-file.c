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

#include "sol-util-file.h"
#include "sol-util.h"
#include "sol-mainloop.h"
#include "sol-log.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_AUXV_H
#include <sys/auxv.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

int
sol_util_vwrite_file(const char *path, const char *fmt, va_list args)
{
    FILE *fp;
    int ret, errno_bkp = 0;

    fp = fopen(path, "we");
    if (!fp)
        return -errno;

    errno = 0;

    ret = vfprintf(fp, fmt, args);
    if (errno)
        errno_bkp = errno;

    fflush(fp);
    if (errno_bkp == 0 && errno > 0)
        errno_bkp = errno;

    fclose(fp);

    if (errno_bkp)
        return -errno_bkp;

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

ssize_t
sol_util_fill_buffer(const int fd, struct sol_buffer *buffer, const size_t size)
{
    size_t bytes_read = 0;
    unsigned int retry = 0;
    ssize_t ret;

    SOL_NULL_CHECK(buffer, -EINVAL);

    ret = sol_buffer_ensure(buffer, buffer->used + size);
    if (ret < 0)
        return ret;

    do {
        ret = read(fd, (char *)buffer->data + buffer->used + bytes_read,
            size - bytes_read);
        if (ret < 0) {
            retry++;
            if (retry >= SOL_UTIL_MAX_READ_ATTEMPTS)
                break;

            if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else
                break;
        }

        retry = 0; //We only count consecutive failures
        bytes_read += (size_t)ret;
    } while (ret && bytes_read < size);

    buffer->used += bytes_read;

    if (ret > 0)
        ret = bytes_read;

    return ret;
}

struct sol_buffer *
sol_util_load_file_raw(const int fd)
{
    struct stat st;
    ssize_t ret;
    size_t buffer_size = 0;
    struct sol_buffer *buffer;

    if (fd < 0)
        return NULL;

    buffer = calloc(1, sizeof(struct sol_buffer));
    SOL_NULL_CHECK(buffer, NULL);
    sol_buffer_init(buffer);

    bool b = false;
    if (fstat(fd, &st) >= 0 && st.st_size && b) {
        ret = sol_util_fill_buffer(fd, buffer, st.st_size);
    } else {
        do {
            buffer_size += CHUNK_SIZE;
            ret = sol_util_fill_buffer(fd, buffer, CHUNK_SIZE);
        } while (ret > 0);
    }

    if (ret < 0)
        goto err;

    if (sol_buffer_trim(buffer) < 0)
        goto err;

    return buffer;

err:
    sol_buffer_fini(buffer);
    free(buffer);

    return NULL;
}

char *
sol_util_load_file_string(const char *filename, size_t *size)
{
    int fd = -1, saved_errno;
    size_t read;
    char *data = NULL;
    struct sol_buffer *buffer = NULL;

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        goto open_err;

    buffer = sol_util_load_file_raw(fd);
    if (!buffer) {
        data = strdup("");
        read = 1;
    } else {
        if (sol_buffer_at_end(buffer) != '\0') {
            sol_buffer_ensure(buffer, buffer->used + 1);
            *((char *)buffer->data + buffer->used) = '\0';
            buffer->used++;
        }
        data = sol_buffer_steal(buffer, &read);
        if (!data)
            goto err;
    }

    free(buffer);
    close(fd);
    if (size)
        *size = read;
    return data;

err:
    saved_errno = errno;
    free(data);
    close(fd);
    free(buffer);
    errno = saved_errno;
open_err:
    if (size)
        *size = 0;
    return NULL;
}

static int
get_progname(char *out, size_t size)
{
    char cwd[PATH_MAX] = { 0 }, readlink_path[PATH_MAX] = { 0 };
    char *execfn;
    int r;

#ifdef HAVE_SYS_AUXV_H
    execfn = (char *)getauxval(AT_EXECFN);
    if (execfn)
        goto done;
#endif

    r = readlink("/proc/self/exe", readlink_path, sizeof(readlink_path));
    if (r < 0)
        return -errno;
    if (r == sizeof(readlink_path))
        return -ENOMEM;

    readlink_path[r] = '\0';
    execfn = readlink_path;

#ifdef HAVE_SYS_AUXV_H
done:
#endif
    if (execfn[0] == '/')
        return snprintf(out, size, "%s", execfn);

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return -errno;

    return snprintf(out, size, "%s/%s", cwd, execfn);
}

static int
get_libname(char *out, size_t size)
{
#ifdef HAVE_DECL_DLADDR
    Dl_info info;
    int r;
    char *path;

    r = dladdr(sol_init, &info);
    if (!r)
        return -EINVAL;

    if (!info.dli_saddr) {
        SOL_WRN("No symbol 'sol_init' found");
        return -EINVAL;
    }

    /* dirname() may modify path, copy it to a local memory. */
    path = strdupa(info.dli_fname);

    return snprintf(out, size, "%s", dirname(path));
#endif /* HAVE_DECL_DLADDR */
    return -ENOSYS;
}

static const char *
strrstr(const char *haystack, const char *needle)
{
    const char *r = NULL;

    if (!haystack || !needle)
        return NULL;

    if (strlen(needle) == 0)
        return haystack + strlen(haystack);

    while (1) {
        const char *p = strstr(haystack, needle);
        if (!p)
            return r;

        r = p;
        haystack = p + 1;
    }

    return r;
}

int
sol_util_get_rootdir(char *out, size_t size)
{
    char progname[PATH_MAX] = { 0 }, *prefix;
    const char *substr;
    int r;

    r = get_libname(progname, sizeof(progname));
    if (r < 0 || r >= (int)sizeof(progname)) {
        r = get_progname(progname, sizeof(progname));
        if (r < 0 || r >= (int)sizeof(progname))
            return r;
    }

    substr = strrstr(progname, PREFIX);
    if (!substr) {
        return -1;
    }

    prefix = strndupa(progname, strlen(progname) - strlen(substr));
    if (!prefix)
        return -1;

    return snprintf(out, size, "%s/", prefix);
}

int
sol_util_fd_set_flag(int fd, int flag)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -errno;

    flags |= flag;
    if (fcntl(fd, F_SETFL, flags) < 0)
        return -errno;

    return 0;
}
