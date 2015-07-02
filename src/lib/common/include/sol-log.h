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

#pragma once

#include "sol-common-buildopts.h"

#include "sol-macros.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Solleta logging.
 */

/**
 * @defgroup Log Logging
 *
 * @{
 */

/* ANSI/VT100 colors as understood by most consoles and terminals */
#define SOL_LOG_COLOR_LIGHTRED    "\033[31;1m"
#define SOL_LOG_COLOR_RED          "\033[31m"
#define SOL_LOG_COLOR_LIGHTBLUE    "\033[34;1m"
#define SOL_LOG_COLOR_BLUE         "\033[34m"
#define SOL_LOG_COLOR_GREEN        "\033[32;1m"
#define SOL_LOG_COLOR_YELLOW       "\033[33;1m"
#define SOL_LOG_COLOR_ORANGE       "\033[0;33m"
#define SOL_LOG_COLOR_WHITE        "\033[37;1m"
#define SOL_LOG_COLOR_LIGHTMAGENTA "\033[35;1m"
#define SOL_LOG_COLOR_MAGENTA      "\033[35m"
#define SOL_LOG_COLOR_LIGHTCYAN    "\033[36;1m"
#define SOL_LOG_COLOR_CYAN         "\033[36m"
#define SOL_LOG_COLOR_RESET        "\033[0m"
#define SOL_LOG_COLOR_HIGH         "\033[1m"

#define log_unlikely(x) __builtin_expect(!!(x), 0)

#define SOL_NULL_CHECK(ptr, ...)                 \
    do {                                        \
        if (log_unlikely(!(ptr))) {                 \
            SOL_WRN("" # ptr "== NULL");         \
            return __VA_ARGS__;                 \
        }                                       \
    } while (0)

#define SOL_NULL_CHECK_GOTO(ptr, label)          \
    do {                                        \
        if (log_unlikely(!(ptr))) {                 \
            SOL_WRN("" # ptr "== NULL");         \
            goto label;                         \
        }                                       \
    } while (0)

#define _SOL_INT_CHECK_FMT(var, exp)                                     \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), int),                \
    "" # var " (%d) " # exp,                                       \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), long),               \
    "" # var " (%ld) " # exp,                                      \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), size_t),             \
    "" # var " (%zu) " # exp,                                      \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), unsigned),           \
    "" # var " (%u) " # exp,                                       \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), uint64_t),           \
    "" # var " (%" PRIu64 ") " # exp,                              \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), uint32_t),           \
    "" # var " (%" PRIu32 ") " # exp,                              \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), uint16_t),           \
    "" # var " (%" PRIu16 ") " # exp,                              \
    __builtin_choose_expr(                                             \
    __builtin_types_compatible_p(typeof(var), uint8_t),            \
    "" # var " (%" PRIu8 ") " # exp,                               \
    (void)0))))))))

#define SOL_INT_CHECK(var, exp, ...)                     \
    do {                                                \
        if (log_unlikely((var)exp)) {                      \
            SOL_WRN(_SOL_INT_CHECK_FMT(var, exp), var);   \
            return __VA_ARGS__;                         \
        }                                               \
    } while (0)

#define SOL_INT_CHECK_GOTO(var, exp, label)              \
    do {                                                \
        if (log_unlikely((var)exp)) {                      \
            SOL_WRN(_SOL_INT_CHECK_FMT(var, exp), var);   \
            goto label;                                 \
        }                                               \
    } while (0)

#define SOL_EXP_CHECK(exp, ...)              \
    do {                                    \
        if (log_unlikely((exp))) {              \
            SOL_WRN("(" # exp ") is false"); \
            return __VA_ARGS__;             \
        }                                   \
    } while (0)                             \


enum sol_log_level {
    SOL_LOG_LEVEL_CRITICAL = 0,
    SOL_LOG_LEVEL_ERROR,
    SOL_LOG_LEVEL_WARNING,
    SOL_LOG_LEVEL_INFO,
    SOL_LOG_LEVEL_DEBUG
};

struct sol_log_domain {
    const char *color;
    const char *name;
    uint8_t level;
};

extern struct sol_log_domain *sol_log_global_domain;

#ifdef SOL_LOG_ENABLED
/**
 * Initialize domain log level based on system configuration.
 *
 * The system configuration may be environment variables like @c
 * $SOL_LOG_LEVEL=NUMBER (or sol_log_set_level()) to apply for all or @c
 * $SOL_LOG_LEVELS=<domain1_name>:<domain1_level>,<domainN_name>:<domainN_level>
 * to give each domain its specific level.
 *
 * @param domain the structure to fill @c level with system configuration value.
 *
 * @see sol_log_set_level()
 */
void sol_log_domain_init_level(struct sol_log_domain *domain);
#else
static inline void
sol_log_domain_init_level(struct sol_log_domain *domain)
{
}
#endif

#ifndef SOL_LOG_DOMAIN
/**
 * @def SOL_LOG_DOMAIN
 *
 * This macro defines the default log domain that is used by SOL_LOG(),
 * SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() and SOL_DBG().
 *
 * If not set prior to inclusion of sol-log.h, then
 * sol_log_global_domain is used.
 */
#define SOL_LOG_DOMAIN sol_log_global_domain
#endif

#if 0
/**
 * @def SOL_LOG_LEVEL_MAXIMUM
 *
 * If defined this level will be ensured before sol_log_print() is
 * called by SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() and
 * SOL_DBG(), which will avoid calling sol_log_print() at all and since
 * it is comparing two constants the compiler will optimize out the
 * block, effectively removing such code (and associated string) from
 * the output binary.
 *
 * One should check using SOL_LOG_LEVEL_POSSIBLE().
 */
#define SOL_LOG_LEVEL_MAXIMUM SOL_LOG_LEVEL_WARNING
#endif

#ifdef SOL_LOG_ENABLED
/**
 * @def SOL_LOG_LEVEL_POSSIBLE(level)
 *
 * Check if log level is possible, that is, if #SOL_LOG_LEVEL_MAXIMUM
 * is set, it should be less or equal to it, otherwise it is possible.
 */
#ifdef SOL_LOG_LEVEL_MAXIMUM
#define SOL_LOG_LEVEL_POSSIBLE(level) (level <= SOL_LOG_LEVEL_MAXIMUM)
#else
#define SOL_LOG_LEVEL_POSSIBLE(level) (1)
#endif
#else
#ifdef SOL_LOG_LEVEL_MAXIMUM
#undef SOL_LOG_LEVEL_MAXIMUM
#endif
#define SOL_LOG_LEVEL_MAXIMUM -1
#define SOL_LOG_LEVEL_POSSIBLE(level) (0)
#endif

/**
 * @def SOL_LOG(level, fmt, ...)
 *
 * This macro logs to the #SOL_LOG_DOMAIN using the given level and
 * format message, as well as using current source file, line and
 * function.
 *
 * @see SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() and SOL_DBG().
 */
#define SOL_LOG(level, fmt, ...)                                         \
    do {                                                                \
        if (SOL_LOG_LEVEL_POSSIBLE(level)) {                             \
            sol_log_print(SOL_LOG_DOMAIN, level,                          \
                __FILE__, __PRETTY_FUNCTION__, __LINE__,       \
                fmt, ## __VA_ARGS__);                          \
        }                                                               \
    } while (0)

/**
 * @def SOL_CRI(fmt, ...)
 *
 * This macro logs a critical message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_ERR(), SOL_WRN(), SOL_INF() and SOL_DBG().
 */
#define SOL_CRI(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_CRITICAL, fmt, ## __VA_ARGS__)

/**
 * @def SOL_ERR(fmt, ...)
 *
 * This macro logs an error message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_WRN(), SOL_INF() and SOL_DBG().
 */
#define SOL_ERR(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_ERROR, fmt, ## __VA_ARGS__)

/**
 * @def SOL_WRN(fmt, ...)
 *
 * This macro logs a warning message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_INF() and SOL_DBG().
 */
#define SOL_WRN(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_WARNING, fmt, ## __VA_ARGS__)

/**
 * @def SOL_INF(fmt, ...)
 *
 * This macro logs an informational message to the #SOL_LOG_DOMAIN
 * using the given format message, as well as using current source
 * file, line and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN() and SOL_DBG().
 */
#define SOL_INF(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_INFO, fmt, ## __VA_ARGS__)

/**
 * @def SOL_DBG(fmt, ...)
 *
 * This macro logs a debug message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN() and SOL_INF().
 */
#define SOL_DBG(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_DEBUG, fmt, ## __VA_ARGS__)

#ifdef SOL_LOG_ENABLED
/**
 * Print out a message in a given domain and level.
 *
 * This function will print out the given message only if the given
 * domain's level is greater or equal to the @a message_level. Then it
 * will call the current print function as set by
 * sol_log_set_print_function(), which defaults to
 * sol_log_print_function_stderr().
 *
 * Some environment variables affect the behavior:
 *  @li @c $SOL_LOG_ABORT=LEVEL if @a message_level is less or equal to
 *      @c LEVEL, then the program will abort execution with
 *      abort(3). Say @c $SOL_LOG_ABORT=ERROR, then it will abort on
 *      #SOL_LOG_LEVEL_CRITICAL or #SOL_LOG_LEVEL_ERROR.
 *  @li @c $SOL_LOG_SHOW_COLORS=[0|1] will disable or enable the color
 *      output in functions that support it such as
 *      sol_log_print_function_stderr(). Defaults to enabled if
 *      terminal supports it.
 * @li @c $SOL_LOG_SHOW_FILE=[0|1] will disable or enable the file name
 *      in output. Enabled by default.
 * @li @c $SOL_LOG_SHOW_FUNCTION=[0|1] will disable or enable the
 *      function name in output. Enabled by default.
 * @li @c $SOL_LOG_SHOW_LINE=[0|1] will disable or enable the line
 *       number in output. Enabled by default.
 *
 * @note use the SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() or
 *       SOL_DBG() macros instead of this one, it should be easier to
 *       deal with.
 *
 * @param domain where the message belongs to.
 * @param message_level the level of the message such as #SOL_LOG_LEVEL_ERROR.
 * @param file the source file name that originated the message.
 * @param function the function that originated the message.
 * @param line the source file line number that originated the message.
 * @param format printf(3) format string for the extra arguments. It
 *        does not need trailing "\n" as this will be enforced.
 *
 * @see sol_log_set_level()
 * @see sol_log_set_abort_level()
 */
void sol_log_print(const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, ...) SOL_ATTR_PRINTF(6, 7) SOL_ATTR_NOINSTRUMENT;
void sol_log_vprint(const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args) SOL_ATTR_NOINSTRUMENT;
#else
static inline void
sol_log_print(const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, ...)
{
}
static inline void
sol_log_vprint(const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
}
#endif

#ifdef SOL_LOG_ENABLED
/**
 * Set the function to print out log messages.
 *
 * The function will be called with sanitized values for @a domain, @a
 * file, @a function, @a format.
 *
 * The function is called with the first argument @c data being the
 * same pointer given to this function as @a data parameter.
 *
 * @note The function is called with a lock held if threads are
 *       enabled. Then you should not trigger sol_log functions from
 *       inside it!
 *
 * @param print the function to use to print out messages. If @c NULL,
 *        then sol_log_print_function_stderr() is used.
 * @param data the context to give back to @a print when it is called.
 */
void sol_log_set_print_function(void (*print)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args), const void *data);
#else
static inline void
sol_log_set_print_function(void (*print)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args), const void *data)
{
}
#endif

#ifdef SOL_LOG_ENABLED
/**
 * Standard logging function that send to standard error output.
 *
 * This function must exist in every platform and is the default if no
 * custom function is set.
 *
 * @see sol_log_set_print_function()
 */
void sol_log_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
}
#endif

#ifdef SOL_PLATFORM_LINUX
#ifdef SOL_LOG_ENABLED
/**
 * Log to a file.
 *
 * The first parameter must be a pointer to FILE* previously opened
 * with fopen(), it should be set as the @c "data" parameter of
 * sol_log_set_print_function().
 *
 * @see sol_log_set_print_function()
 */
void sol_log_print_function_file(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_file(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
}
#endif
#endif

#ifdef SOL_PLATFORM_LINUX
#ifdef SOL_LOG_ENABLED
/**
 * Log to syslog.
 *
 * This function will use vsyslog(), translate sol_log_level to
 * syslog's priority and send the message to the daemon.
 *
 * This function is used automatically if there is an environment
 * variable $SOL_LOG_PRINT_FUNCTION=syslog.
 *
 * @see sol_log_set_print_function()
 * @see sol_log_print_function_journal()
 */
void sol_log_print_function_syslog(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *syslog, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_syslog(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *syslog, const char *function, int line, const char *format, va_list args)
{
}
#endif
#endif

#ifdef SOL_PLATFORM_LINUX
#ifdef SOL_LOG_ENABLED
/**
 * Log to systemd's journal.
 *
 * This function will use sd_journal_send_with_location() to
 * communicate with systemd's journald daemon and it will use
 * journal's extended capabilities, such as logging the source file
 * name, line and function, as well as thread id (if pthread is
 * enabled).
 *
 * If systemd support was not compiled in, then it will use syslog.
 *
 * This function is used automatically if there is an environment
 * variable $NOTIFY_SOCKET or $SOL_LOG_PRINT_FUNCTION=journal.
 *
 * @see sol_log_set_print_function()
 * @see sol_log_print_function_syslog()
 */
void sol_log_print_function_journal(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *journal, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_journal(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *journal, const char *function, int line, const char *format, va_list args)
{
}
#endif
#endif

#ifdef SOL_LOG_ENABLED
/* those implementing custom logging functions may use the following getters */
void sol_log_level_to_str(uint8_t level, char *buf, size_t buflen);
const char *sol_log_get_level_color(uint8_t level);
uint8_t sol_log_get_abort_level(void);
uint8_t sol_log_get_level(void);
bool sol_log_get_show_colors(void);
bool sol_log_get_show_file(void);
bool sol_log_get_show_function(void);
bool sol_log_get_show_line(void);

/* to force some logging setting independent of platform initializations, use the following setters */
void sol_log_set_abort_level(uint8_t level);
void sol_log_set_level(uint8_t level);
void sol_log_set_show_colors(bool enabled);
void sol_log_set_show_file(bool enabled);
void sol_log_set_show_function(bool enabled);
void sol_log_set_show_line(bool enabled);
#else
static inline void
sol_log_level_to_str(uint8_t level, char *buf, size_t buflen)
{
}
static inline const char *
sol_log_get_level_color(uint8_t level)
{
    return "";
}
static inline uint8_t
sol_log_get_abort_level(void)
{
    return 0;
}
static inline uint8_t
sol_log_get_level(void)
{
    return 0;
}
static inline bool
sol_log_get_show_colors(void)
{
    return false;
}
static inline bool
sol_log_get_show_file(void)
{
    return false;
}
static inline bool
sol_log_get_show_function(void)
{
    return false;
}
static inline bool
sol_log_get_show_line(void)
{
    return false;
}
static inline void
sol_log_set_abort_level(uint8_t level)
{
}
static inline void
sol_log_set_level(uint8_t level)
{
}
static inline void
sol_log_set_show_colors(bool enabled)
{
}
static inline void
sol_log_set_show_file(bool enabled)
{
}
static inline void
sol_log_set_show_function(bool enabled)
{
}
static inline void
sol_log_set_show_line(bool enabled)
{
}
#endif

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
