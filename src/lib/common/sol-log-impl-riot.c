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
sol_log_impl_domain_init_level(struct sol_log_domain *domain)
{
    domain->level = _global_domain.level;
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
