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

    sol_log_level_to_str(message_level, level_str, sizeof(level_str));

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
