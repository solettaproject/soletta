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
#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "linux-micro-locale");

#include "sol-file-reader.h"
#include "sol-platform-linux-micro.h"
#include "sol-str-slice.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"

static const struct sol_str_slice locale_vars[] = {
    SOL_STR_SLICE_LITERAL("LANG"),
    SOL_STR_SLICE_LITERAL("LANGUAGE"),
    SOL_STR_SLICE_LITERAL("LC_CTYPE"),
    SOL_STR_SLICE_LITERAL("LC_NUMERIC"),
    SOL_STR_SLICE_LITERAL("LC_TIME"),
    SOL_STR_SLICE_LITERAL("LC_COLLATE"),
    SOL_STR_SLICE_LITERAL("LC_MONETARY"),
    SOL_STR_SLICE_LITERAL("LC_MESSAGES"),
    SOL_STR_SLICE_LITERAL("LC_PAPER"),
    SOL_STR_SLICE_LITERAL("LC_NAME"),
    SOL_STR_SLICE_LITERAL("LC_ADDRESS"),
    SOL_STR_SLICE_LITERAL("LC_TELEPHONE"),
    SOL_STR_SLICE_LITERAL("LC_MEASUREMENT"),
    SOL_STR_SLICE_LITERAL("LC_IDENTIFICATION"),
};

static void
parse_var(const char *start, size_t len)
{
    const struct sol_str_slice *itr;
    const char *sep = memchr(start, '=', len);
    const char *name = start;
    const char *value = sep + 1;
    size_t namelen, valuelen;

    if (!sep)
        return;

    namelen = sep - start;
    valuelen = len - (value - start);
    if (namelen < 1 || valuelen < 1)
        return;

    while (isblank(value[0])) {
        value++;
        valuelen--;
        if (valuelen == 0)
            return;
    }
    while (isblank(value[valuelen - 1])) {
        valuelen--;
        if (valuelen == 0)
            return;
    }
    if (valuelen > 2 && value[0] == '"' && value[valuelen - 1] == '"') {
        value++;
        valuelen -= 2;
    }

    for (itr = locale_vars; itr < locale_vars + sol_util_array_size(locale_vars); itr++) {
        if (itr->len == namelen && memcmp(itr->data, name, namelen) == 0) {
            char *v = strndupa(value, valuelen);
            SOL_DBG("set locale var %s=%s", itr->data, v);
            setenv(itr->data, v, true);
            return;
        }
    }

    SOL_WRN("Unknown locale var: %.*s", (unsigned)len, start);
}

static void
parse_kcmdline_entry(const char *start, size_t len)
{
    const char prefix[] = "locale.";
    const size_t prefixlen = strlen(prefix);

    if (len < prefixlen)
        return;

    if (memcmp(start, prefix, prefixlen) != 0)
        return;

    start += prefixlen;
    len -= prefixlen;
    parse_var(start, len);
}

static int
load_kcmdline(void)
{
    char buf[4096] = {};
    const char *p, *end, *start;
    int err;

    err = sol_util_read_file("/proc/cmdline", "%4095[^\n]", buf);
    if (err < 1)
        return err;

    start = buf;
    end = start + strlen(buf);
    for (p = start; p < end; p++) {
        if (isblank(*p) && start < p) {
            parse_kcmdline_entry(start, p - start);
            start = p + 1;
        }
    }
    if (start < end)
        parse_kcmdline_entry(start, end - start);

    return 0;
}

static void
parse_conf_entry(const char *start, size_t len)
{
    while (len > 0 && isblank(*start)) {
        start++;
        len--;
    }
    if (len == 0)
        return;
    if (*start == '#')
        return;

    parse_var(start, len);
}

static int
load_conf(void)
{
    struct sol_file_reader *reader;
    struct sol_str_slice file;
    const char *p, *end, *start;

    reader = sol_file_reader_open("/etc/locale.conf");
    if (!reader && errno == ENOENT) {
        errno = 0;
        return 0;
    }
    SOL_NULL_CHECK(reader, -errno);

    file = sol_file_reader_get_all(reader);
    start = file.data;
    end = start + file.len;
    for (p = start; p < end; p++) {
        if (*p == '\n' && start < p) {
            parse_conf_entry(start, p - start);
            start = p + 1;
        }
    }
    if (start < end)
        parse_conf_entry(start, end - start);

    sol_file_reader_close(reader);
    return 0;
}

static int
locale_start(const struct sol_platform_linux_micro_module *mod, const char *service)
{
    int err;

    err = load_kcmdline();
    SOL_INT_CHECK_GOTO(err, < 0, error);

    err = load_conf();
    SOL_INT_CHECK_GOTO(err, < 0, error);

    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_ACTIVE);
    return 0;

error:
    sol_platform_linux_micro_inform_service_state(service, SOL_PLATFORM_SERVICE_STATE_FAILED);
    return err;
}

static int
locale_init(const struct sol_platform_linux_micro_module *module, const char *service)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

SOL_PLATFORM_LINUX_MICRO_MODULE(LOCALE,
    .name = "locale",
    .init = locale_init,
    .start = locale_start,
    );
