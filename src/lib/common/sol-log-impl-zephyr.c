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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sol-log-impl.h"

int
sol_log_impl_init(void)
{
    return 0;
}

void
sol_log_impl_shutdown(void)
{
}

bool
sol_log_impl_lock(void)
{
    return true;
}

void
sol_log_impl_unlock(void)
{
}

void
sol_log_impl_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    const char *name = domain->name ? domain->name : "";
    char level_str[4];
    size_t len;
    int errno_bkp = errno;

    sol_log_level_to_str(message_level, level_str, sizeof(level_str));

    if (_show_file && _show_function && _show_line) {
        printf("%s:%s %s:%d %s() ",
            level_str, name, file, line, function);
    } else {
        printf("%s:%s ", level_str, name);

        if (_show_file)
            printf("%s", file);
        if (_show_file && _show_line)
            printf(":");
        if (_show_line)
            printf("%d", line);

        if (_show_file || _show_line)
            printf(" ");

        if (_show_function)
            printf("%s() ", function);
    }

    errno = errno_bkp;

    vprintf(format, args);

    len = strlen(format);
    if (len > 0 && format[len - 1] != '\n')
        printf("\n");
}
