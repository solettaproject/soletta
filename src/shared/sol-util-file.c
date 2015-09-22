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
#include "sol-util.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
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
    size_t bytes_read = 0, s;
    unsigned int retry = 0;
    ssize_t ret;

    SOL_NULL_CHECK(buffer, -EINVAL);

    if (sol_util_size_add(buffer->used, size, &s) < 0)
        return -EOVERFLOW;

    ret = sol_buffer_ensure(buffer, s);
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
    struct sol_buffer *buffer;

    if (fd < 0)
        return NULL;

    buffer = sol_buffer_new();
    SOL_NULL_CHECK(buffer, NULL);

    if (fstat(fd, &st) >= 0 && st.st_size) {
        ret = sol_util_fill_buffer(fd, buffer, st.st_size);
    } else {
        do {
            ret = sol_util_fill_buffer(fd, buffer, CHUNK_SIZE);
        } while (ret > 0);
    }

    if (ret < 0)
        goto err;

    if (sol_buffer_trim(buffer) < 0)
        goto err;

    return buffer;

err:
    sol_buffer_free(buffer);

    return NULL;
}

char *
sol_util_load_file_fd_string(int fd, size_t *size)
{
    int saved_errno;
    size_t size_read;
    char *data = NULL;
    struct sol_buffer *buffer = NULL;

    buffer = sol_util_load_file_raw(fd);
    if (!buffer) {
        data = strdup("");
        size_read = 1;
    } else {
        if (*((char *)sol_buffer_at_end(buffer) - 1) != '\0') {
            if (buffer->used >= SIZE_MAX - 1 ||
                sol_buffer_ensure(buffer, buffer->used + 1) < 0)
                goto err;
            *((char *)buffer->data + buffer->used) = '\0';
            buffer->used++;
        }
        data = sol_buffer_steal(buffer, &size_read);
        if (!data)
            goto err;
    }

    sol_buffer_free(buffer);
    if (size)
        *size = size_read;
    return data;

err:
    saved_errno = errno;
    free(data);
    sol_buffer_free(buffer);
    errno = saved_errno;
    if (size)
        *size = 0;
    return NULL;
}

char *
sol_util_load_file_string(const char *filename, size_t *size)
{
    int fd, saved_errno;
    char *ret;

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (size)
            *size = 0;
        return NULL;
    }

    ret = sol_util_load_file_fd_string(fd, size);

    if (!ret)
        saved_errno = errno;
    close(fd);
    if (!ret)
        errno = saved_errno;

    return ret;
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
    char path[PATH_MAX];
    Dl_info info;
    int r;

    r = dladdr(sol_init, &info);
    if (!r)
        return -EINVAL;

    if (!info.dli_saddr) {
        SOL_WRN("No symbol 'sol_init' found");
        return -EINVAL;
    }

    /* It may be a symlink, resolve it. dirname() may modify it
     * afterwards. */
    SOL_NULL_CHECK(realpath(info.dli_fname, path), -errno);

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

bool
sol_util_iterate_dir(const char *path, bool (*iterate_dir_cb)(void *data, const char *dir_path, struct dirent *ent), const void *data)
{
    DIR *dir;
    struct dirent *ent, *res;
    int success;
    long name_max;
    size_t len;
    bool result = false;

    SOL_NULL_CHECK(path, false);
    SOL_NULL_CHECK(iterate_dir_cb, false);

    /* See readdir_r(3) */
    name_max = pathconf(path, _PC_NAME_MAX);
    if (name_max == -1)
        name_max = 255;
    len = offsetof(struct dirent, d_name) + name_max + 1;
    ent = malloc(len);
    SOL_NULL_CHECK(ent, false);

    dir = opendir(path);
    if (!dir) {
        SOL_WRN("Could not open dir [%s] to iterate: %s", path,
            sol_util_strerrora(errno));
        free(ent);
        return false;
    }

    success = readdir_r(dir, ent, &res);
    while (success == 0 && res) {
        if (iterate_dir_cb((void *)data, path, res)) {
            result = true;
            break;
        }

        success = readdir_r(dir, ent, &res);
    }

    free(ent);
    closedir(dir);

    return result;
}
