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

#include "sol-common-buildopts.h"

#ifndef HAVE_DECL_STRNDUPA
#include <alloca.h>

#define __strndupa_internal__(str_, len_, var_) \
    ({ \
        size_t var_ ## len = strnlen((str_), (len_)); \
        char *var_ = alloca((var_ ## len) + 1); \
        var_[var_ ## len] = '\0'; \
        memcpy(var_, (str_), var_ ## len); \
    })
#define strndupa(str_, len_)     __strndupa_internal__(str_, len_, __tmp__ ## __LINE__)
#endif

#ifndef HAVE_DECL_STRDUPA
#define strdupa(str)            strndupa(str, strlen(str))
#endif

#ifndef HAVE_DECL_MEMMEM
#include <string.h>

static inline void *
memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen)
{
    const char *ptr = haystack;
    const char *end = ptr + (haystacklen - needlelen);

    if (needlelen > haystacklen)
        return NULL;

    for (; ptr <= end; ptr++) {
        if (!memcmp(ptr, needle, needlelen))
            return (void *)ptr;
    }

    return NULL;
}
#endif

#ifndef HAVE_DECL_MEMRCHR
#include <string.h>

static inline void *
memrchr(const void *haystack, int c, size_t n)
{
    const char *ptr = (const char *)haystack + n;

    for (; ptr != haystack; --ptr) {
        if (*ptr == c)
            return (void *)ptr;
    }

    return NULL;
}
#endif

#if !defined(HAVE_PIPE2) && defined(HAVE_PIPE) && defined(FEATURE_FILESYSTEM)
#include <fcntl.h>
#include <unistd.h>
#include <sol-util-file.h>
#ifndef O_CLOEXEC
#error "I need O_CLOEXEC to work!"
#endif

static inline int
pipe2(int pipefd[2], int flags)
{
    int ret = pipe(pipefd);

    if (ret < 0)
        return ret;

    if (flags & O_NONBLOCK) {
        ret = sol_util_fd_set_flag(pipefd[0], O_NONBLOCK);
        if (ret >= 0)
            ret = sol_util_fd_set_flag(pipefd[1], O_NONBLOCK);
        if (ret < 0)
            goto err;
    }

    if (flags & O_CLOEXEC) {
        ret = fcntl(pipefd[0], F_GETFD);
        if (ret >= 0)
            ret = fcntl(pipefd[0], F_SETFD, ret | FD_CLOEXEC);
        if (ret >= 0)
            ret = fcntl(pipefd[1], F_GETFD);
        if (ret >= 0)
            ret = fcntl(pipefd[1], F_SETFD, ret | FD_CLOEXEC);
        if (ret < 0)
            goto err;
    }

    return 0;

err:
    {
        int save_errno = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        errno = save_errno;
    }
    return -1;
}
#endif

#if !defined(HAVE_GET_CURRENT_DIR_NAME) && defined(HAVE_UNIX)
#include <errno.h>
#include <limits.h>         /* for PATH_MAX */
#include <stdlib.h>
#include <unistd.h>

static inline char *
get_current_dir_name(void)
{
    size_t len = 0;
    char *buffer = NULL;

    while (1) {
        len += PATH_MAX;
        char *nb = (char *)realloc(buffer, len);

        if (nb == NULL)
            goto err_free;
        nb = getcwd(buffer, len);
        if (nb == NULL) {
            if (errno == ERANGE)
                continue;
            goto err_free;
        }
        return buffer;
    }

err_free:
    free(buffer);
    return NULL;
}

#endif

#ifndef HAVE_DECL_IFLA_INET6_MAX
#define IFLA_INET6_UNSPEC 0
#define IFLA_INET6_FLAGS 1
#define IFLA_INET6_CONF 2
#define IFLA_INET6_STATS 3
#define IFLA_INET6_MCAST 4
#define IFLA_INET6_CACHEINFO 5
#define IFLA_INET6_ICMP6STATS 6
#define IFLA_INET6_TOKEN 7
#define IFLA_INET6_ADDR_GEN_MODE 8
#define __IFLA_INET6_MAX 9
#define IFLA_INET6_MAX (__IFLA_INET6_MAX - 1)
#elif !defined(HAVE_DECL_IFLA_INET6_ADDR_GEN_MODE)
#define IFLA_INET6_ADDR_GEN_MODE 8
#endif

#ifndef HAVE_DECL_IFLA_INET6_ADDR_GEN_MODE
#define IN6_ADDR_GEN_MODE_EUI64 0
#define IN6_ADDR_GEN_MODE_NONE 1
#endif

#ifndef I2C_RDRW_IOCTL_MAX_MSGS
#define I2C_RDRW_IOCTL_MAX_MSGS 42
#endif

#if !defined(RB_SW_SUSPEND) && defined(SOL_PLATFORM_LINUX)
#include <linux/reboot.h>
#define RB_SW_SUSPEND LINUX_REBOOT_CMD_SW_SUSPEND /* for uclibc */
#endif

#ifdef __cplusplus
}
#endif
