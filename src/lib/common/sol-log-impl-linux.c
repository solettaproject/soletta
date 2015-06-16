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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-journal.h>
#endif

#include "sol-str-table.h"
#include "sol-log-impl.h"

static pid_t _main_pid;
static struct sol_str_table *_env_levels = NULL;
static char *_env_levels_str = NULL;

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
static pthread_t _main_thread;
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static bool
_level_str_parse(const char *buf, size_t buflen, uint8_t *storage)
{
    int16_t found;
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("CRI",      SOL_LOG_LEVEL_CRITICAL),
        SOL_STR_TABLE_ITEM("CRIT",     SOL_LOG_LEVEL_CRITICAL),
        SOL_STR_TABLE_ITEM("CRITICAL", SOL_LOG_LEVEL_CRITICAL),
        SOL_STR_TABLE_ITEM("DBG",      SOL_LOG_LEVEL_DEBUG),
        SOL_STR_TABLE_ITEM("DEBUG",    SOL_LOG_LEVEL_DEBUG),
        SOL_STR_TABLE_ITEM("ERR",      SOL_LOG_LEVEL_ERROR),
        SOL_STR_TABLE_ITEM("ERROR",    SOL_LOG_LEVEL_ERROR),
        SOL_STR_TABLE_ITEM("INF",      SOL_LOG_LEVEL_INFO),
        SOL_STR_TABLE_ITEM("INFO",     SOL_LOG_LEVEL_INFO),
        SOL_STR_TABLE_ITEM("WARN",     SOL_LOG_LEVEL_WARNING),
        SOL_STR_TABLE_ITEM("WARNING",  SOL_LOG_LEVEL_WARNING),
        SOL_STR_TABLE_ITEM("WRN",      SOL_LOG_LEVEL_WARNING),
        { }
    };

    if (!sol_str_table_lookup(table, SOL_STR_SLICE_STR(buf, buflen), &found))
        return false;
    *storage = found;
    return true;
}

static bool
_int32_parse(const char *str, size_t size, int32_t *storage)
{
    int64_t value = 0;
    size_t i;
    bool negative = false;

    errno = 0;
    for (i = 0; i < size; i++) {
        const char c = str[i];
        if (i == 0 && c == '-') {
            negative = true;
            continue;
        }

        if (c < '0' || c > '9')
            break;
        value = (value * 10) + (c - '0');
        if (value > INT32_MAX) {
            errno = ERANGE;
            return false;
        }
    }
    if (i == 0) {
        errno = EINVAL;
        return false;
    }
    if (negative) {
        value = -value;
        if (value < INT32_MIN) {
            errno = ERANGE;
            return false;
        }
    }
    *storage = value;
    return true;
}

static bool
_level_parse(const char *str, size_t size, uint8_t *storage)
{
    int32_t i;
    uint8_t level = 0;

    if (size == 0)
        return false;

    if (_int32_parse(str, size, &i))
        level = i;
    else if (!_level_str_parse(str, size, &level))
        return false;

    *storage = level;
    return true;
}

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

static bool
_levels_parse(const char *str, size_t size)
{
    const char *str_end = str + size;
    unsigned int count = 0;
    struct sol_str_table *itr;
    const char *p, *e;

    for (p = str; p < str_end; p++) {
        if (*p == ',')
            count++;
    }
    if (p > str)
        count++;

    free(_env_levels);
    free(_env_levels_str);
    if (count == 0) {
        _env_levels = NULL;
        _env_levels_str = NULL;
        return false;
    }

    _env_levels = malloc((count + 1) * sizeof(*_env_levels));
    if (!_env_levels) {
        _env_levels_str = NULL;
        return false;
    }

    _env_levels_str = strndup(str, size);
    if (!_env_levels_str) {
        free(_env_levels);
        _env_levels = NULL;
        _env_levels_str = NULL;
        return false;
    }

    itr = _env_levels;
    e = NULL;
    str = _env_levels_str;
    str_end = str + size;
    for (p = str;; p++) {
        if (p < str_end && *p == ':')
            e = p;
        else if (p == str_end || *p == ',') {
            if (e && str + 1 < e && e + 1 < p) {
                uint8_t val;
                itr->key = str;
                itr->len = e - str;
                if (_level_parse(e + 1, p - e - 1, &val)) {
                    itr->val = val;
                    itr++;
                }
            }

            if (p == str_end)
                break;
            str = p + 1;
            e = NULL;
        }
    }

    if (itr == _env_levels) {
        free(_env_levels);
        free(_env_levels_str);
        _env_levels = NULL;
        _env_levels_str = NULL;
        return false;
    }

    itr->key = NULL;
    itr->len = 0;
    itr->val = 0;
    return true;
}

static void
_env_level_get(const char *envvar, uint8_t *storage)
{
    const char *s = getenv(envvar);

    if (!s) return;
    _level_parse(s, strlen(s), storage);
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
    _levels_parse(s, strlen(s));
}

static void
_env_levels_unload(void)
{
    free(_env_levels);
    free(_env_levels_str);
    _env_levels = NULL;
    _env_levels_str = NULL;
}

static void
_bool_parse_wrapper(const char *str, size_t size, void *data)
{
    _bool_parse(str, size, data);
}

static void
_level_parse_wrapper(const char *str, size_t size, void *data)
{
    _level_parse(str, size, data);
}

static void
_levels_parse_wrapper(const char *str, size_t size, void *data)
{
    _levels_parse(str, size);
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
    itr_end = itr + ARRAY_SIZE(specs);
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

#ifdef HAVE_PTHREAD_H
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

#ifdef HAVE_SYSTEMD
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
#ifdef HAVE_SYSTEMD
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
    _env_levels_unload();
    _main_pid = 0;
#ifdef HAVE_PTHREAD_H
    _main_thread = 0;
    pthread_mutex_destroy(&_mutex);
#endif
}

bool
sol_log_impl_lock(void)
{
#ifdef HAVE_PTHREAD_H
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
#ifdef HAVE_PTHREAD_H
    pthread_mutex_unlock(&_mutex);
#endif
}

void
sol_log_impl_domain_init_level(struct sol_log_domain *domain)
{
    int16_t level = _global_domain.level;

    if (_env_levels)
        sol_str_table_lookup(_env_levels,
            sol_str_slice_from_str(domain->name),
            &level);

    domain->level = level;
}

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
#ifdef HAVE_PTHREAD_H
    if (_main_thread != pthread_self())
        fprintf(stderr, "T%lu ", pthread_self());
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
#ifdef HAVE_PTHREAD_H
    if (_main_thread != pthread_self())
        fprintf(fp, "T:%lu ", pthread_self());
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
#ifdef HAVE_SYSTEMD
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
#ifdef HAVE_PTHREAD_H
        "THREAD=%" PRIu64, (uint64_t)pthread_self(),
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
