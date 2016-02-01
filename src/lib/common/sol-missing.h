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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_DECL_STRNDUPA
#include <alloca.h>

#define __strndupa_internal__(str_, len_, var_)       \
    ({                                                  \
        size_t var_ ## len = strnlen((str_), (len_));   \
        char *var_ = alloca((var_ ## len) + 1);         \
        var_[var_ ## len] = '\0';                       \
        memcpy(var_, (str_), var_ ## len);              \
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

#if !defined(HAVE_PIPE2) && defined(HAVE_PIPE)
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

#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX
#endif

#ifdef __cplusplus
}
#endif
