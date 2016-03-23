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

#include "sol-util-file.h"
#include "sol-util-internal.h"
#include "sol-mainloop.h"
#include "sol-log.h"

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

#define CHUNK_SIZE 4096

SOL_API int
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

SOL_API int
sol_util_write_file(const char *path, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = sol_util_vwrite_file(path, fmt, ap);
    va_end(ap);

    return ret;
}

SOL_API int
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

SOL_API int
sol_util_read_file(const char *path, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = sol_util_vread_file(path, fmt, ap);
    va_end(ap);

    return ret;
}

SOL_API ssize_t
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
            if (retry >= SOL_UTIL_MAX_READ_ATTEMPTS) {
                if (errno == EINTR || errno == EAGAIN) {
                    ret = 0; /* if we exceed maximum attempts, don't return error */
                }
                break;
            }

            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        } else if (ret == 0) {
            retry++;
            if (retry >= SOL_UTIL_MAX_READ_ATTEMPTS)
                break;
            continue;
        }

        retry = 0; //We only count consecutive failures
        bytes_read += (size_t)ret;
    } while (bytes_read < size);

    buffer->used += bytes_read;

    if (ret >= 0)
        ret = bytes_read;

    if (SOL_BUFFER_NEEDS_NUL_BYTE(buffer)) {
        int err;

        err = sol_buffer_ensure_nul_byte(buffer);
        SOL_INT_CHECK(err, < 0, err);
    }

    return ret;
}

SOL_API struct sol_buffer *
sol_util_load_file_fd_raw(const int fd)
{
    struct sol_buffer *buf;
    int r;

    buf = sol_buffer_new();
    SOL_NULL_CHECK(buf, NULL);

    buf->flags |= SOL_BUFFER_FLAGS_NO_NUL_BYTE;

    r = sol_util_load_file_fd_buffer(fd, buf);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return buf;

err_exit:
    sol_buffer_free(buf);
    return NULL;
}

SOL_API int
sol_util_load_file_fd_buffer(int fd, struct sol_buffer *buf)
{
    struct stat st;
    ssize_t ret;
    int r;

    SOL_INT_CHECK(fd, < 0, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);

    if (fstat(fd, &st) >= 0 && st.st_size) {
        ret = sol_util_fill_buffer(fd, buf, st.st_size);
    } else {
        do {
            ret = sol_util_fill_buffer(fd, buf, CHUNK_SIZE);
        } while (ret > 0);
    }

    r = (int)ret;
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API char *
sol_util_load_file_fd_string(int fd, size_t *size)
{
    int r;
    size_t size_read;
    char *data = NULL;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;

    r = sol_util_load_file_fd_buffer(fd, &buf);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_buffer_trim(&buf);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    data = sol_buffer_steal(&buf, &size_read);
    SOL_NULL_CHECK_GOTO(data, err);

    if (size)
        *size = size_read;
    return data;

err:
    if (size)
        *size = 0;
    return NULL;
}

SOL_API char *
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

SOL_API int
sol_util_load_file_buffer(const char *filename, struct sol_buffer *buf)
{
    int fd, r;

    SOL_NULL_CHECK(filename, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    r = -errno;
    SOL_INT_CHECK(fd, < 0, r);

    r = sol_util_load_file_fd_buffer(fd, buf);

    if (close(fd) < 0 && !r)
        r = -errno;
    return r;
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

SOL_API int
sol_util_get_rootdir(char *out, size_t size)
{
    char progname[PATH_MAX] = { 0 };
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

    r = snprintf(out, size, "%.*s/", (int)(strlen(progname) - strlen(substr)), progname);
    return (r < 0 || r >= (int)size) ? -ENOMEM : r;
}

SOL_API int
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

SOL_API bool
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
        SOL_INF("Could not open dir [%s] to iterate: %s", path,
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

static int
sync_dir_of(const char *new_path)
{
    /* Sync destination dir, so dir information is sure to be stored */
    char *tmp, *dir_name;
    int dir_fd;

    tmp = strndupa(new_path, PATH_MAX);
    dir_name = dirname(tmp);
    dir_fd = open(dir_name, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (dir_fd < 0) {
        SOL_WRN(
            "Could not open destination directory to ensure file information is stored: %s",
            sol_util_strerrora(errno));
        return -errno;
    }

    errno = 0;
    if (fsync(dir_fd) < 0) {
        SOL_WRN("Could not open ensure file information is stored: %s",
            sol_util_strerrora(errno));
    }

    close(dir_fd);

    return -errno;
}

SOL_API int
sol_util_move_file(const char *old_path, const char *new_path, mode_t mode)
{
    FILE *new, *old;
    char buf[CHUNK_SIZE];
    size_t size, w_size;
    int fd, r;

    /* First, try to rename */
    r = rename(old_path, new_path);
    if (r == 0) {
        if (chmod(new_path, mode) < 0) {
            SOL_WRN("Could not set mode %4o to file %s: %s", mode, new_path,
                sol_util_strerrora(errno));
            goto err_rename;
        }
        /* TODO What if syncing the dir fails? */
        if (sync_dir_of(new_path) < 0)
            goto err_rename;

        return 0;
    }

    /* If failed, try the hard way */
    errno = 0;

    old = fopen(old_path, "reb");
    SOL_NULL_CHECK(old, -errno);

    fd = open(new_path, O_WRONLY | O_TRUNC | O_CLOEXEC | O_CREAT, mode);
    r = -errno;
    SOL_INT_CHECK_GOTO(fd, < 0, err_new);

    new = fdopen(fd, "we");
    if (!new) {
        close(fd);
        goto err_new;
    }

    while ((size = fread(buf, 1, sizeof(buf), old))) {
        w_size = fwrite(buf, 1, size, new);
        if (w_size != size) {
            r = -EIO;
            goto err;
        }
    }
    if (ferror(old)) {
        r = -EIO;
        goto err;
    }

    if (fflush(new) != 0) {
        r = -errno;
        goto err;
    }

    if (fchmod(fd, mode) < 0) {
        SOL_WRN("Could not set mode %4o to file %s: %s", mode, new_path,
            sol_util_strerrora(errno));
        goto err;
    }

    if (fsync(fd) < 0) {
        SOL_WRN("Could not ensure file [%s] is synced to storage: %s",
            new_path, sol_util_strerrora(errno));
        goto err;
    }

    if (sync_dir_of(new_path) < 0)
        goto err;

    fclose(new);

    /* Remove old file */
    fclose(old);
    unlink(old_path);

    return 0;

err:
    fclose(new);
    unlink(new_path);
err_new:
    fclose(old);
err_rename:

    /* So errno is not influenced by fclose and unlink above*/
    errno = -r;

    return r;
}

SOL_API bool
sol_util_busy_wait_file(const char *path, uint64_t nanoseconds)
{
    struct stat st;
    struct timespec start = sol_util_timespec_get_current();

    while (stat(path, &st)) {
        struct timespec elapsed, now = sol_util_timespec_get_current();

        sol_util_timespec_sub(&now, &start, &elapsed);
        if ((uint64_t)elapsed.tv_sec >= (nanoseconds / SOL_NSEC_PER_SEC) &&
            (uint64_t)elapsed.tv_nsec >= (nanoseconds % SOL_NSEC_PER_SEC))
            return false;
    }

    return true;
}
