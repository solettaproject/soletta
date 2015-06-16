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

#include "sol_config.h"

#if !HAVE_DECL_STRNDUPA
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

#if !defined(HAVE_ACCEPT4) || HAVE_ACCEPT4 == 0
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

static inline int
accept4(int sockfd, struct sockaddr *addr, socklen_t *len, int flags)
{
    int fl, fd = accept(sockfd, addr, len);

    if (fd < 0)
        return fd;

    fl = fcntl(fd, F_GETFD);
    if (fl == -1)
        goto err;

    fl |= flags;
    if (fcntl(fd, F_SETFD, fl) == -1)
        goto err;

    return fd;

err:
    close(fd);
    return -1;
}
#endif

#define EBADR       53  /* Invalid request descriptor */
