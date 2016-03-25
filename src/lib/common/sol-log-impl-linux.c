/*
 * This file is part of the Soletta Project
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#ifdef PLATFORM_SYSTEMD
#include <systemd/sd-journal.h>
#endif

#include "sol-log-impl.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"

static pid_t _main_pid;

#ifdef PTHREAD
#include <pthread.h>
static pthread_t _main_thread;
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static bool
_bool_parse(const char *str, size_t size, bool *storage)
{
    if (size == 1) {
        if (str[0] == '0')
            *storage = false;
        else if (str[0] == '1')
            *storage = true;
        else
            return false;
    } else if ((size == sizeof("true") - 1 &&
        strncasecmp(str, "true", size) == 0) ||
        (size == sizeof("yes") - 1 &&
        strncasecmp(str, "yes", size) == 0)) {
        *storage = true;
    } else if ((size == sizeof("false") - 1 &&
        strncasecmp(str, "false", size) == 0) ||
        (size == sizeof("no") - 1 &&
        strncasecmp(str, "no", size) == 0)) {
        *storage = false;
    } else
        return false;

    return true;
}

static void
_env_level_get(const char *envvar, uint8_t *storage)
{
    const char *s = getenv(envvar);

    if (!s) return;
    sol_log_level_parse(s, strlen(s), storage);
}

static void
_env_bool_get(const char *envvar, bool *storage)
{
    const char *s = getenv(envvar);

    if (!s) return;
    _bool_parse(s, strlen(s), storage);
}

static void
_env_levels_load(void)
{
    const char *s = getenv("SOL_LOG_LEVELS");

    if (!s) return;
    sol_log_levels_parse(s, strlen(s));
}

static void
_bool_parse_wrapper(const char *str, size_t size, void *data)
{
    _bool_parse(str, size, data);
}

static void
_level_parse_wrapper(const char *str, size_t size, void *data)
{
    sol_log_level_parse(str, size, data);
}

static void
_levels_parse_wrapper(const char *str, size_t size, void *data)
{
    sol_log_levels_parse(str, size);
}

static void
_kcmdline_parse_var(const char *start, size_t len)
{
    static const struct spec {
        const char *prefix;
        size_t prefixlen;
        void *data;
        void (*parse)(const char *str, size_t size, void *data);
    } specs[] = {
#define SPEC(str, storage, parse)               \
    { str, sizeof(str) - 1, storage, parse \
    }
        SPEC("LEVELS", NULL, _levels_parse_wrapper),
        SPEC("LEVEL", &_global_domain.level, _level_parse_wrapper),
        SPEC("ABORT", &_abort_level, _level_parse_wrapper),
        SPEC("SHOW_COLORS", &_show_colors, _bool_parse_wrapper),
        SPEC("SHOW_FILE", &_show_file, _bool_parse_wrapper),
        SPEC("SHOW_FUNCTION", &_show_function, _bool_parse_wrapper),
        SPEC("SHOW_LINE", &_show_line, _bool_parse_wrapper),
#undef SPEC
    };
    const struct spec *itr, *itr_end;

    itr = specs;
    itr_end = itr + sol_util_array_size(specs);
    for (; itr < itr_end; itr++) {
        if (itr->prefixlen + 1 < len &&
            memcmp(itr->prefix, start, itr->prefixlen) == 0 &&
            start[itr->prefixlen] == '=') {
            itr->parse(start + (itr->prefixlen + 1),
                len - (itr->prefixlen + 1),
                itr->data);
            break;
        }
    }
}

static void
_kcmdline_parse_entry(const char *start, size_t len)
{
    const char prefix[] = "SOL_LOG_";
    const size_t prefixlen = strlen(prefix);

    if (len < prefixlen)
        return;

    if (memcmp(start, prefix, prefixlen) != 0)
        return;

    start += prefixlen;
    len -= prefixlen;
    _kcmdline_parse_var(start, len);
}

static int
_kcmdline_load(void)
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
            _kcmdline_parse_entry(start, p - start);
            start = p + 1;
        }
    }
    if (start < end)
        _kcmdline_parse_entry(start, end - start);

    return 0;
}

int
sol_log_impl_init(void)
{
    const char *func_name = getenv("SOL_LOG_PRINT_FUNCTION");

#ifdef HAVE_ISATTY
    if (isatty(STDOUT_FILENO)) {
        const char *term = getenv("TERM");
        if (term) {
            _show_colors = (streq(term, "xterm") ||
                streq(term, "xterm-color") ||
                streq(term, "xterm-256color") ||
                streq(term, "rxvt") ||
                streq(term, "rxvt-unicode") ||
                streq(term, "rxvt-unicode-256color") ||
                streq(term, "gnome") ||
                streq(term, "screen"));
        }
    }
#endif

    if (_main_pid == 0)
        _main_pid = getpid();

#ifdef PTHREAD
    if (_main_thread == 0)
        _main_thread = pthread_self();
#endif

    _env_levels_load();

    _env_level_get("SOL_LOG_LEVEL", &_global_domain.level);
    _env_level_get("SOL_LOG_ABORT", &_abort_level);

    _env_bool_get("SOL_LOG_SHOW_COLORS", &_show_colors);
    _env_bool_get("SOL_LOG_SHOW_FILE", &_show_file);
    _env_bool_get("SOL_LOG_SHOW_FUNCTION", &_show_function);
    _env_bool_get("SOL_LOG_SHOW_LINE", &_show_line);

    if (_main_pid == 1)
        _kcmdline_load();

#ifdef PLATFORM_SYSTEMD
    if (getenv("NOTIFY_SOCKET")) {
        _print_function = sol_log_print_function_journal;
        _print_function_data = NULL;
    }
#endif

    if (func_name) {
        if (streq(func_name, "stderr")) {
            _print_function = sol_log_print_function_stderr;
            _print_function_data = NULL;
        } else if (streq(func_name, "syslog")) {
            _print_function = sol_log_print_function_syslog;
            _print_function_data = NULL;
#ifdef PLATFORM_SYSTEMD
        } else if (streq(func_name, "journal")) {
            _print_function = sol_log_print_function_journal;
            _print_function_data = NULL;
#endif
        } else {
            fprintf(stderr, "ERROR: unsupported SOL_LOG_PRINT_FUNCTION=%s\n",
                func_name);
        }
    }

    return 0;
}

void
sol_log_impl_shutdown(void)
{
    _main_pid = 0;
#ifdef PTHREAD
    _main_thread = 0;
    pthread_mutex_destroy(&_mutex);
#endif
}

bool
sol_log_impl_lock(void)
{
#ifdef PTHREAD
    int error = pthread_mutex_lock(&_mutex);
    if (!error)
        return true;
    if (error == EDEADLK)
        fputs("ERROR: log would deadlock!\n", stderr);
    return false;
#else
    return true;
#endif
}

void
sol_log_impl_unlock(void)
{
#ifdef PTHREAD
    pthread_mutex_unlock(&_mutex);
#endif
}

SOL_ATTR_PRINTF(7, 0)
void
sol_log_impl_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    const char *name = domain->name ? domain->name : "";
    char level_str[4] = { 0 };
    size_t len;
    int errno_bkp = errno;

    sol_log_level_to_str(message_level, level_str, sizeof(level_str));

    if (_main_pid != getpid())
        fprintf(stderr, "P%u ", getpid());
#ifdef PTHREAD
    if (_main_thread != pthread_self())
        fprintf(stderr, "T%" PRIu64 " ", (uint64_t)(uintptr_t)pthread_self());
#endif

    if (_show_file && _show_function && _show_line) {
        if (!_show_colors) {
            fprintf(stderr, "%s:%s %s:%d %s() ",
                level_str, name, file, line, function);
        } else {
            const char *level_color = sol_log_get_level_color(message_level);
            const char *reset_color = SOL_LOG_COLOR_RESET;
            const char *address_color = SOL_LOG_COLOR_HIGH;
            fprintf(stderr, "%s%s%s:%s%s%s %s%s:%d %s()%s ",
                level_color, level_str, reset_color,
                domain->color ? domain->color : "", name, reset_color,
                address_color, file, line, function, reset_color);
        }
    } else {
        const char *level_color = "", *reset_color = "", *address_color = "",
        *domain_color = "";

        if (_show_colors) {
            level_color = sol_log_get_level_color(message_level);
            reset_color = SOL_LOG_COLOR_RESET;
            address_color = SOL_LOG_COLOR_HIGH;
            domain_color = domain->color ? domain->color : "";
        }

        fprintf(stderr, "%s%s%s:%s%s%s ",
            level_color, level_str, reset_color,
            domain_color, name, reset_color);

        if (_show_file || _show_line || _show_function)
            fputs(address_color, stderr);

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

        if (_show_file || _show_line || _show_function)
            fputs(reset_color, stderr);
    }

    errno = errno_bkp;
    vfprintf(stderr, format, args);

    len = strlen(format);
    if (len > 0 && format[len - 1] != '\n')
        fputc('\n', stderr);
    fflush(stderr);
}

SOL_API void
sol_log_print_function_file(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    FILE *fp = data;
    const char *name = domain->name ? domain->name : "";
    char level_str[4] = { 0 };
    size_t len;
    int errno_bkp = errno;

    sol_log_level_to_str(message_level, level_str, sizeof(level_str));

    if (_main_pid != getpid())
        fprintf(fp, "P:%u ", getpid());
#ifdef PTHREAD
    if (_main_thread != pthread_self())
        fprintf(fp, "T%" PRIu64 " ", (uint64_t)(uintptr_t)pthread_self());
#endif

    if (_show_file && _show_function && _show_line)
        fprintf(fp, "%s:%s %s:%d %s() ", level_str, name, file, line, function);
    else {
        fprintf(fp, "%s:%s ", level_str, name);

        if (_show_file)
            fputs(file, fp);
        if (_show_file && _show_line)
            fputc(':', fp);
        if (_show_line)
            fprintf(fp, "%d", line);

        if (_show_file || _show_line)
            fputc(' ', fp);

        if (_show_function)
            fprintf(fp, "%s() ", function);
    }

    errno = errno_bkp;
    vfprintf(fp, format, args);

    len = strlen(format);
    if (len > 0 && format[len - 1] != '\n')
        fputc('\n', fp);
    fflush(fp);
}

static int
_sol_log_level_to_syslog(uint8_t sol_level)
{
    if (sol_level == SOL_LOG_LEVEL_CRITICAL)
        return LOG_CRIT;
    else if (sol_level == SOL_LOG_LEVEL_ERROR)
        return LOG_ERR;
    else if (sol_level == SOL_LOG_LEVEL_WARNING)
        return LOG_WARNING;
    else if (sol_level == SOL_LOG_LEVEL_INFO)
        return LOG_INFO;
    else if (sol_level == SOL_LOG_LEVEL_DEBUG)
        return LOG_DEBUG;
    else
        return ((int)sol_level - SOL_LOG_LEVEL_DEBUG) + LOG_DEBUG;
}

SOL_API void
sol_log_print_function_syslog(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
    int level = _sol_log_level_to_syslog(message_level);

    vsyslog(level, format, args);
}

SOL_API void
sol_log_print_function_journal(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
#ifdef PLATFORM_SYSTEMD
    char *code_file = NULL;
    char *code_line = NULL;
    char *msg = NULL;
    int r, sd_level = _sol_log_level_to_syslog(message_level);

    r = asprintf(&code_file, "CODE_FILE=%s", file);
    if (r == -1)
        fprintf(stderr, "ERR: asprintf() CODE_FILE=%s failed\n", file);

    r = asprintf(&code_line, "CODE_LINE=%d", line);
    if (r == -1)
        fprintf(stderr, "ERR: asprintf() CODE_LINE=%d failed\n", line);

    r = vasprintf(&msg, format, args);
    if (r == -1)
        fprintf(stderr, "ERR: asprintf() %s failed\n", format);

    sd_journal_send_with_location(code_file, code_line, function,
        "PRIORITY=%i", sd_level,
        "MESSAGE=%s", msg ? msg : format,
#ifdef PTHREAD
        "THREAD=%" PRIu64, (uint64_t)(uintptr_t)pthread_self(),
#endif
        NULL);

    free(code_file);
    free(code_line);
    free(msg);
#else
    static bool once = false;
    if (!once) {
        once = true;
        fputs("ERROR: systemd support not compiled in, using syslog.\n", stderr);
    }
    sol_log_print_function_syslog(data, domain, message_level, file, function, line, format, args);
#endif
}
