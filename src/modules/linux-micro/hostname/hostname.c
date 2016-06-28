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

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-hostname");

#include "sol-platform-linux-micro.h"
#include "sol-file-reader.h"
#include "sol-util-internal.h"

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
