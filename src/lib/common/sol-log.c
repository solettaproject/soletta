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
#include <stdlib.h>

#include "sol-util.h"
#include "sol-log.h"
#include "sol-log-impl.h"

/* NOTE: these are not static since we share them with sol-log-impl-*.c
 * to make it simpler for those to change and check these variables
 * without the need to resort to getters and setters.
 *
 * It also enables the sol_log_impl_init() to reset these variables,
 * that is done before _inited is set to true.
 *
 * See sol-log-impl-linux.c for instance, it will parse from envvars
 * directly to these variables, otherwise it would need to use
 * intermediate variables.
 */
struct sol_log_domain _global_domain = {
    .color = SOL_LOG_COLOR_WHITE,
    .name = "",
    .level = SOL_LOG_LEVEL_WARNING
};
uint8_t _abort_level = SOL_LOG_LEVEL_CRITICAL;
bool _show_colors = false;
bool _show_file = true;
bool _show_function = true;
bool _show_line = true;
void (*_print_function)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args) = sol_log_print_function_stderr;
const void *_print_function_data = NULL;

static bool _inited = false;
#define SOL_LOG_INIT_CHECK(fmt, ...)                                     \
    do {                                                                \
        if (unlikely(!_inited)) {                                       \
            fprintf(stderr, "CRITICAL:%s:%d:%s() "                      \
                "SOL_LOG used before initialization. "fmt "\n",       \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, ## __VA_ARGS__); \
            abort();                                                    \
        }                                                               \
    } while (0)


SOL_API struct sol_log_domain *sol_log_global_domain = &_global_domain;

/* used in sol-mainloop.c */
int sol_log_init(void);
void sol_log_shutdown(void);

int
sol_log_init(void)
{
    int r = sol_log_impl_init();

    if (r == 0)
        _inited = true;
    return r;
}

void
sol_log_shutdown(void)
{
    sol_log_impl_shutdown();
    _inited = false;
}

SOL_API void
sol_log_domain_init_level(struct sol_log_domain *domain)
{
    SOL_LOG_INIT_CHECK("domain=%p", domain);

    if (!domain || !domain->name)
        return;

    sol_log_impl_domain_init_level(domain);
}

SOL_API void
sol_log_print(const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    sol_log_vprint(domain, message_level, file, function, line, format, ap);
    va_end(ap);
}

SOL_API void
sol_log_vprint(const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    int errno_bkp = errno;

    SOL_LOG_INIT_CHECK("domain=%p, file=%s, function=%s, line=%d, fomart=%s",
        domain, file, function, line, format);

    if (!domain) domain = &_global_domain;
    if (!file) file = "";
    if (!function) function = "";
    if (!format) {
        fprintf(stderr,
            "ERROR: sol_log_print() called with format == NULL "
            "from function=%s, file=%s, line=%d\n",
            function, file, line);
        abort();
        return;
    }

    if (message_level > domain->level)
        return;

    errno = errno_bkp;

    if (!sol_log_impl_lock()) {
        fprintf(stderr,
            "ERROR: sol_log_print() cannot lock "
            "from function=%s, file=%s, line=%d\n",
            function, file, line);
        abort();
        return;
    }
    _print_function((void *)_print_function_data,
        domain, message_level, file, function, line, format, args);
    sol_log_impl_unlock();

    if (message_level <= _abort_level)
        abort();
    errno = errno_bkp;
}

SOL_API void
sol_log_set_print_function(void (*cb)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args), const void *data)
{
    SOL_LOG_INIT_CHECK("cb=%p, data=%p", cb, data);

    if (cb) {
        _print_function = cb;
        _print_function_data = data;
    } else {
        _print_function = sol_log_print_function_stderr;
        _print_function_data = NULL;
    }
}

SOL_API void
sol_log_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    sol_log_impl_print_function_stderr(data, domain, message_level, file, function, line, format, args);
}

SOL_API void
sol_log_level_to_str(uint8_t level, char *buf, size_t buflen)
{
    static const char *level_names[] = {
        [SOL_LOG_LEVEL_CRITICAL] = "CRI",
        [SOL_LOG_LEVEL_ERROR] = "ERR",
        [SOL_LOG_LEVEL_WARNING] = "WRN",
        [SOL_LOG_LEVEL_INFO] = "INF",
        [SOL_LOG_LEVEL_DEBUG] = "DBG"
    };

    SOL_LOG_INIT_CHECK("level=%hhu, buf=%p, buflen=%zd", level, buf, buflen);
    if (buflen < 1)
        return;

    if (level < ARRAY_SIZE(level_names)) {
        if (level_names[level]) {
            strncpy(buf, level_names[level], buflen - 1);
            buf[buflen - 1] = '\0';
            return;
        }
    }

    snprintf(buf, buflen, "%03d", level);
}

SOL_API const char *
sol_log_get_level_color(uint8_t level)
{
    static const char *level_colors[] = {
        [SOL_LOG_LEVEL_CRITICAL] = SOL_LOG_COLOR_RED,
        [SOL_LOG_LEVEL_ERROR] = SOL_LOG_COLOR_LIGHTRED,
        [SOL_LOG_LEVEL_WARNING] = SOL_LOG_COLOR_ORANGE,
        [SOL_LOG_LEVEL_INFO] = SOL_LOG_COLOR_CYAN,
        [SOL_LOG_LEVEL_DEBUG] = SOL_LOG_COLOR_LIGHTBLUE
    };

    SOL_LOG_INIT_CHECK("level=%hhu", level);
    if (level < ARRAY_SIZE(level_colors)) {
        if (level_colors[level])
            return level_colors[level];
    }

    return SOL_LOG_COLOR_MAGENTA;
}

SOL_API void
sol_log_set_abort_level(uint8_t level)
{
    SOL_LOG_INIT_CHECK("level=%hhu", level);
    _abort_level = level;
}

SOL_API uint8_t
sol_log_get_abort_level(void)
{
    SOL_LOG_INIT_CHECK("");
    return _abort_level;
}

SOL_API void
sol_log_set_level(uint8_t level)
{
    SOL_LOG_INIT_CHECK("level=%hhu", level);
    _global_domain.level = level;
}

SOL_API uint8_t
sol_log_get_level(void)
{
    SOL_LOG_INIT_CHECK("");
    return _global_domain.level;
}

SOL_API void
sol_log_set_show_colors(bool enabled)
{
    SOL_LOG_INIT_CHECK("enabled=%hhu", enabled);
    _show_colors = enabled;
}

SOL_API bool
sol_log_get_show_colors(void)
{
    SOL_LOG_INIT_CHECK("");
    return _show_colors;
}

SOL_API void
sol_log_set_show_file(bool enabled)
{
    SOL_LOG_INIT_CHECK("enabled=%hhu", enabled);
    _show_file = enabled;
}

SOL_API bool
sol_log_get_show_file(void)
{
    SOL_LOG_INIT_CHECK("");
    return _show_file;
}

SOL_API void
sol_log_set_show_function(bool enabled)
{
    SOL_LOG_INIT_CHECK("enabled=%hhu", enabled);
    _show_function = enabled;
}

SOL_API bool
sol_log_get_show_function(void)
{
    SOL_LOG_INIT_CHECK("");
    return _show_function;
}

SOL_API void
sol_log_set_show_line(bool enabled)
{
    SOL_LOG_INIT_CHECK("enabled=%hhu", enabled);
    _show_line = enabled;
}

SOL_API bool
sol_log_get_show_line(void)
{
    SOL_LOG_INIT_CHECK("");
    return _show_line;
}
