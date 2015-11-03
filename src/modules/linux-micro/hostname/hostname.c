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

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-hostname");

#include "sol-platform-linux-micro.h"
#include "sol-file-reader.h"
#include "sol-util.h"

static int
hostname_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    struct sol_file_reader *reader;
    struct sol_str_slice str;
    const char *s, *p, *end;
    int err = 0;

    reader = sol_file_reader_open("/etc/hostname");
    SOL_NULL_CHECK_MSG(reader, -errno, "could not read /etc/hostname");

    str = sol_file_reader_get_all(reader);
    s = p = str.data;
    end = s + str.len;

    for (; s < end; s++) {
        if (!isblank(*s))
            break;
    }

    for (p = end - 1; p > s; p--) {
        if (!isblank(*p))
            break;
    }

    if (s >= p) {
        SOL_WRN("no hostname in /etc/hostname");
        err = -ENOENT;
    } else if (sethostname(s, p - s) < 0) {
        SOL_WRN("could not set hostname: %s", sol_util_strerrora(errno));
        err = -errno;
    }

    sol_file_reader_close(reader);

    if (err == 0)
        sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    else
        sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_FAILED);

    return err;
}

static int
hostname_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(HOSTNAME,
    .name = "hostname",
    .init = hostname_init,
    .start = hostname_start,
    );
