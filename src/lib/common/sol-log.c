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
#include <stdlib.h>

#include "sol-util-internal.h"
#include "sol-log.h"
#include "sol-macros.h"
#include "sol-log-impl.h"
#include "sol-str-table.h"

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

static struct sol_str_table *_env_levels = NULL;
static char *_env_levels_str = NULL;
static bool _inited = false;
#define SOL_LOG_INIT_CHECK(fmt, ...) \
    do { \
        if (SOL_UNLIKELY(!_inited)) { \
            fprintf(stderr, "CRITICAL:%s:%d:%s() " \
                "SOL_LOG used before initialization. "fmt "\n", \
                SOL_LOG_FILE, \
                __LINE__, \
                SOL_LOG_FUNCTION, \
                ## __VA_ARGS__); \
            sol_abort(); \
        } \
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
    free(_env_levels);
    free(_env_levels_str);
    _env_levels = NULL;
    _env_levels_str = NULL;
    _inited = false;
}

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

bool
sol_log_level_parse(const char *str, size_t size, uint8_t *storage)
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

bool
sol_log_levels_parse(const char *str, size_t size)
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
                if (sol_log_level_parse(e + 1, p - e - 1, &val)) {
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

SOL_API void
sol_log_init_level_global(const char *str, size_t length)
{
    sol_log_level_parse(str, length, &_global_domain.level);
}

SOL_API void
sol_log_init_levels(const char *str, size_t length)
{
    sol_log_levels_parse(str, length);
}

SOL_API void
sol_log_domain_init_level(struct sol_log_domain *domain)
{
    int16_t level;

    SOL_LOG_INIT_CHECK("domain=%p", domain);

    if (!domain || !domain->name)
        return;

    if (_env_levels) {
        level = sol_str_table_lookup_fallback(_env_levels,
            sol_str_slice_from_str(domain->name),
            _global_domain.level);
    } else
        level = _global_domain.level;

    domain->level = level;
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
        sol_abort();
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
        sol_abort();
        return;
    }
    _print_function((void *)_print_function_data,
        domain, message_level, file, function, line, format, args);
    sol_log_impl_unlock();

    if (message_level <= _abort_level)
        sol_abort();
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

    if (level < sol_util_array_size(level_names)) {
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
    if (level < sol_util_array_size(level_colors)) {
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
