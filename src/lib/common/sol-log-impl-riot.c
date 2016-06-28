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

#ifdef THREADS
#include <mutex.h>
#include <thread.h>
#endif

#include "sol-log-impl.h"

#ifdef THREADS
static kernel_pid_t _main_thread;
static mutex_t _mutex;
#endif

int
sol_log_impl_init(void)
{
#ifdef THREADS
    _main_thread = thread_getpid();
    mutex_init(&_mutex);
#endif
    return 0;
}

void
sol_log_impl_shutdown(void)
{
#ifdef THREADS
    _main_thread = KERNEL_PID_UNDEF;
#endif
}

bool
sol_log_impl_lock(void)
{
#ifdef THREADS
    mutex_lock(&_mutex);
#endif
    return true;
}

void
sol_log_impl_unlock(void)
{
#ifdef THREADS
    mutex_unlock(&_mutex);
#endif
}

void
sol_log_impl_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    const char *name = domain->name ? domain->name : "";
    char level_str[4];
    size_t len;
    int errno_bkp = errno;

#ifdef THREADS
    kernel_pid_t thread;
#endif

    sol_log_level_to_str(message_level, level_str, sizeof(level_str));

#ifdef THREADS
    thread = thread_getpid();
    if (thread != _main_thread)
        fprintf(stderr, "T%" PRId16 " ", thread);
#endif

    if (_show_file && _show_function && _show_line) {
        fprintf(stderr, "%s:%s %s:%d %s() ",
            level_str, name, file, line, function);
    } else {
        fprintf(stderr, "%s:%s ", level_str, name);

        if (_show_file)
            fputs(file, stderr);
        if (_show_file && _show_line)
            fputc(':', stderr);
        if (_show_line)
            fprintf(stderr, "%d", line);

        if (_show_file || _show_line)
            fputc(' ', stderr);

        if (_show_function)
            fprintf(stderr, "%s() ", function);
    }

    errno = errno_bkp;
    vfprintf(stderr, format, args);

    len = strlen(format);
    if (len > 0 && format[len - 1] != '\n')
        fputc('\n', stderr);
    fflush(stderr);
}
