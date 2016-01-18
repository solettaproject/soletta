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
 * @brief These routines are used for Soletta logging.
 */

/**
 * @defgroup Log Logging
 *
 * @{
 */

/**
 * @defgroup Colors Colors
 *
 * @brief ANSI/VT100 colors as understood by most consoles and terminals.
 *
 * @{
 */
#define SOL_LOG_COLOR_LIGHTRED    "\033[31;1m" /**< @brief Light Red color code */
#define SOL_LOG_COLOR_RED          "\033[31m" /**< @brief Red color code */
#define SOL_LOG_COLOR_LIGHTBLUE    "\033[34;1m" /**< @brief Light Blue color code */
#define SOL_LOG_COLOR_BLUE         "\033[34m" /**< @brief Blue color code */
#define SOL_LOG_COLOR_GREEN        "\033[32;1m" /**< @brief Green color code */
#define SOL_LOG_COLOR_YELLOW       "\033[33;1m" /**< @brief Yellow color code */
#define SOL_LOG_COLOR_ORANGE       "\033[0;33m" /**< @brief Orange color code */
#define SOL_LOG_COLOR_WHITE        "\033[37;1m" /**< @brief White color code */
#define SOL_LOG_COLOR_LIGHTMAGENTA "\033[35;1m" /**< @brief Light Magenta color code */
#define SOL_LOG_COLOR_MAGENTA      "\033[35m" /**< @brief Magenta color code */
#define SOL_LOG_COLOR_LIGHTCYAN    "\033[36;1m" /**< @brief Light Cyan color code */
#define SOL_LOG_COLOR_CYAN         "\033[36m" /**< @brief Cyan color code */
#define SOL_LOG_COLOR_RESET        "\033[0m" /**< @brief Code to Reset the color to default */
#define SOL_LOG_COLOR_HIGH         "\033[1m" /**< @brief Highlight code */
/**
 * @}
 */

/**
 * @brief Convenience macro to check for @c NULL pointer.
 *
 * This macro logs a warning message and returns if the pointer @c ptr
 * happens to be @c NULL.
 *
 * @param ptr Pointer to check
 * @param ... Optional return value
 */
#define SOL_NULL_CHECK(ptr, ...) \
    do { \
        if (SOL_UNLIKELY(!(ptr))) { \
            SOL_WRN("%s == NULL", # ptr); \
            return __VA_ARGS__; \
        } \
    } while (0)

/**
 * @brief Convenience macro to check for @c NULL pointer and jump to a given label.
 *
 * This macro logs a warning message and jumps to @c label if the pointer @c ptr
 * happens to be @c NULL.
 *
 * @param ptr Pointer to check
 * @param label @c goto label
 * @param ... Optional return value
 */
#define SOL_NULL_CHECK_GOTO(ptr, label) \
    do { \
        if (SOL_UNLIKELY(!(ptr))) { \
            SOL_WRN("%s == NULL", # ptr); \
            goto label; \
        } \
    } while (0)

/**
 * @brief Similar to @ref SOL_NULL_CHECK but allowing for a custom warning message.
 *
 * @param ptr Pointer to check
 * @param ret Value to return
 * @param fmt A standard 'printf()' format string
 * @param ... The arguments for @c fmt
 *
 * @see SOL_NULL_CHECK
 */
#define SOL_NULL_CHECK_MSG(ptr, ret, fmt, ...) \
    do { \
        if (SOL_UNLIKELY(!(ptr))) { \
            SOL_WRN(fmt, ## __VA_ARGS__); \
            return ret; \
        } \
    } while (0)

/**
 * @brief Similar to @ref SOL_NULL_CHECK_GOTO but allowing for a custom warning message.
 *
 * @param ptr Pointer to check
 * @param label @c goto label
 * @param fmt A standard 'printf()' format string
 * @param ... The arguments for @c fmt
 *
 * @see SOL_NULL_CHECK_GOTO
 */
#define SOL_NULL_CHECK_MSG_GOTO(ptr, label, fmt, ...) \
    do { \
        if (SOL_UNLIKELY(!(ptr))) { \
            SOL_WRN(fmt, ## __VA_ARGS__); \
            goto label; \
        } \
    } while (0)

/**
 * @brief Auxiliary macro intended to be used by @ref SOL_INT_CHECK to format it's output.
 *
 * @param var Integer checked by @ref SOL_INT_CHECK
 */
#define _SOL_INT_CHECK_FMT(var) \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), int), \
    "" # var " (%d) %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), long), \
    "" # var " (%ld) %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), size_t), \
    "" # var " (%zu) %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), unsigned), \
    "" # var " (%u) %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), uint64_t), \
    "" # var " (%" PRIu64 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), uint32_t), \
    "" # var " (%" PRIu32 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), uint16_t), \
    "" # var " (%" PRIu16 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), uint8_t), \
    "" # var " (%" PRIu8 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), int64_t), \
    "" # var " (%" PRId64 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), int32_t), \
    "" # var " (%" PRId32 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), int16_t), \
    "" # var " (%" PRId16 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), int8_t), \
    "" # var " (%" PRId8 ") %s", \
    __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(var), ssize_t), \
    "" # var " (%zd) %s", \
    (void)0)))))))))))))

/**
 * @brief Safety-check macro to check if integer @c var against @c exp.
 *
 * This macro logs a warning message and returns if the integer @c var
 * satisfies the expression @c exp.
 *
 * @param var Integer to be check
 * @param exp Safety-check expression
 * @param ... Optional return value
 */
#define SOL_INT_CHECK(var, exp, ...) \
    do { \
        if (SOL_UNLIKELY((var)exp)) { \
            SOL_WRN(_SOL_INT_CHECK_FMT(var), var, # exp); \
            return __VA_ARGS__; \
        } \
    } while (0)

/**
 * @brief Similar to @ref SOL_INT_CHECK but jumping to @c label instead of returning.
 *
 * This macro logs a warning message and jumps to @c label if the integer @c var
 * satisfies the expression @c exp.
 *
 * @param var Integer to be check
 * @param exp Safety-check expression
 * @param label @c goto label
 */
#define SOL_INT_CHECK_GOTO(var, exp, label) \
    do { \
        if (SOL_UNLIKELY((var)exp)) { \
            SOL_WRN(_SOL_INT_CHECK_FMT(var), var, # exp); \
            goto label; \
        } \
    } while (0)

/**
 * @brief Safety-check macro to check the expression @c exp.
 *
 * This macro logs a warning message and returns if the expression @c exp
 * is @c true.
 *
 * @param exp Safety-check expression
 * @param ... Optional return value
 */
#define SOL_EXP_CHECK(exp, ...) \
    do { \
        if (SOL_UNLIKELY((exp))) { \
            SOL_WRN("(%s) is true", # exp); \
            return __VA_ARGS__; \
        } \
    } while (0)

/**
 * @brief Similar to @ref SOL_EXP_CHECK but jumping to @c label instead of returning.
 *
 * This macro logs a warning message and jumps to @c label if the expression @c exp
 * is @c true.
 *
 * @param exp Safety-check expression
 * @param label @c goto label
 */
#define SOL_EXP_CHECK_GOTO(exp, label) \
    do { \
        if (SOL_UNLIKELY((exp))) { \
            SOL_WRN("(%s) is true", # exp); \
            goto label; \
        } \
    } while (0)

/**
 * @brief Available logging levels.
 *
 * Levels are use to identify the severity of the issue related to a given log message.
 */
enum sol_log_level {
    SOL_LOG_LEVEL_CRITICAL = 0, /**< @brief Critical */
    SOL_LOG_LEVEL_ERROR, /**< @brief Error */
    SOL_LOG_LEVEL_WARNING, /**< @brief Warning */
    SOL_LOG_LEVEL_INFO, /**< @brief Informational */
    SOL_LOG_LEVEL_DEBUG /**< @brief Debug */
};

/**
 * @brief Structure containing the attributes of the domain used for logging.
 */
struct sol_log_domain {
    const char *color; /**< @brief Color to be used */
    const char *name; /**< @brief Domain name */
    uint8_t level; /**< @brief Maximum level to log for this domain */
};

/**
 * @brief Global logging domain.
 *
 * Log domain is a way to provide a scope or category to messages that can
 * be used for filtering in addition to log levels.
 */
extern struct sol_log_domain *sol_log_global_domain;

/**
 * @fn void sol_log_domain_init_level(struct sol_log_domain *domain)
 *
 * @brief Initialize domain log level based on system configuration.
 *
 * The system configuration may be environment variables like @c
 * $SOL_LOG_LEVEL=NUMBER (or sol_log_set_level()) to apply for all or @c
 * $SOL_LOG_LEVELS=\<domain1_name\>:\<domain1_level\>,\<domainN_name\>:\<domainN_level\>
 * to give each domain its specific level.
 *
 * @param domain the structure to fill @c level with system configuration value.
 *
 * @see sol_log_set_level()
 */
#ifdef SOL_LOG_ENABLED
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
 * @brief Defines the default log domain that is used by SOL_LOG(),
 * SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() and SOL_DBG().
 *
 * If not set prior to inclusion of sol-log.h, then
 * sol_log_global_domain is used.
 *
 * It can be used to provide custom log domains for nodes and modules.
 */
#define SOL_LOG_DOMAIN sol_log_global_domain
#endif

/**
 * @def SOL_LOG_LEVEL_MAXIMUM
 *
 * @brief Ensures a maximum log level.
 *
 * If defined, this level will be ensured before sol_log_print() is
 * called by SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() and
 * SOL_DBG(), which will avoid calling sol_log_print() at all and since
 * it is comparing two constants the compiler will optimize out the
 * block, effectively removing such code (and associated string) from
 * the output binary.
 *
 * One should check using SOL_LOG_LEVEL_POSSIBLE().
 */
#if 0
#define SOL_LOG_LEVEL_MAXIMUM SOL_LOG_LEVEL_WARNING
#endif

/**
 * @def SOL_LOG_LEVEL_POSSIBLE(level)
 *
 * @brief Check if log level is possible.
 *
 * If #SOL_LOG_LEVEL_MAXIMUM is set, it should be less or equal to it,
 * otherwise it is always impossible.
 */
#ifdef SOL_LOG_ENABLED
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
 * @brief Logs to #SOL_LOG_DOMAIN using the given level and
 * format message.
 *
 * Also uses the current source file, line and function.
 *
 * @see SOL_CRI(), SOL_ERR(), SOL_WRN(), SOL_INF() and SOL_DBG().
 */
#define SOL_LOG(level, fmt, ...) \
    do { \
        if (SOL_LOG_LEVEL_POSSIBLE(level)) { \
            sol_log_print(SOL_LOG_DOMAIN, level, \
                __FILE__, __PRETTY_FUNCTION__, __LINE__, \
                fmt, ## __VA_ARGS__); \
        } \
    } while (0)

/**
 * @def SOL_CRI(fmt, ...)
 *
 * @brief Logs a message with @c critical level.
 *
 * Logs a critical message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_ERR(), SOL_WRN(), SOL_INF() and SOL_DBG().
 */
#define SOL_CRI(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_CRITICAL, fmt, ## __VA_ARGS__)

/**
 * @def SOL_ERR(fmt, ...)
 *
 * @brief Logs a message with @c error level.
 *
 * Logs an error message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_WRN(), SOL_INF() and SOL_DBG().
 */
#define SOL_ERR(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_ERROR, fmt, ## __VA_ARGS__)

/**
 * @def SOL_WRN(fmt, ...)
 *
 * @brief Logs a message with @c warning level.
 *
 * Logs a warning message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_INF() and SOL_DBG().
 */
#define SOL_WRN(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_WARNING, fmt, ## __VA_ARGS__)

/**
 * @def SOL_INF(fmt, ...)
 *
 * @brief Logs a message with @c informational level.
 *
 * Logs an informational message to the #SOL_LOG_DOMAIN
 * using the given format message, as well as using current source
 * file, line and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN() and SOL_DBG().
 */
#define SOL_INF(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_INFO, fmt, ## __VA_ARGS__)

/**
 * @def SOL_DBG(fmt, ...)
 *
 * @brief Logs a message with @c debug level.
 *
 * Logs a debug message to the #SOL_LOG_DOMAIN using the
 * given format message, as well as using current source file, line
 * and function.
 *
 * @see SOL_LOG(), SOL_CRI(), SOL_ERR(), SOL_WRN() and SOL_INF().
 */
#define SOL_DBG(fmt, ...) SOL_LOG(SOL_LOG_LEVEL_DEBUG, fmt, ## __VA_ARGS__)

/**
 * @fn void sol_log_print(const struct sol_log_domain *domain, uint8_t message_level,
 * const char *file, const char *function, int line, const char *format, ...)
 *
 * @brief Print out a message in a given domain and level.
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

/**
 * @fn void sol_log_vprint(const struct sol_log_domain *domain, uint8_t message_level,
 * const char *file, const char *function, int line, const char *format, va_list args)
 *
 * @brief Similar to @ref sol_log_print, but called with @c va_list instead of a variable number of arguments.
 *
 * @param domain where the message belongs to.
 * @param message_level the level of the message such as #SOL_LOG_LEVEL_ERROR.
 * @param file the source file name that originated the message.
 * @param function the function that originated the message.
 * @param line the source file line number that originated the message.
 * @param format printf(3) format string for the extra arguments. It
 *        does not need trailing "\n" as this will be enforced.
 * @param args Variables list.
 */
#ifdef SOL_LOG_ENABLED
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

/**
 * @brief Set the function to print out log messages.
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
#ifdef SOL_LOG_ENABLED
void sol_log_set_print_function(void (*print)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args), const void *data);
#else
static inline void
sol_log_set_print_function(void (*print)(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args), const void *data)
{
}
#endif

/**
 * @brief Standard logging function that send to standard error output.
 *
 * This function must exist in every platform and is the default if no
 * custom function is set.
 *
 * @see sol_log_set_print_function()
 */
#ifdef SOL_LOG_ENABLED
void sol_log_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_stderr(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
}
#endif

#ifdef SOL_PLATFORM_LINUX
/**
 * @fn void sol_log_print_function_file(void *data, const struct sol_log_domain *domain,
 * uint8_t message_level, const char *file, const char *function, * int line, const char *format,
 * va_list args)
 *
 * @brief Log to a file.
 *
 * The first parameter must be a pointer to FILE* previously opened
 * with fopen(), it should be set as the @c "data" parameter of
 * sol_log_set_print_function().
 *
 * @see sol_log_set_print_function()
 */
#ifdef SOL_LOG_ENABLED
void sol_log_print_function_file(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_file(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *file, const char *function, int line, const char *format, va_list args)
{
}
#endif
#endif

#ifdef SOL_PLATFORM_LINUX
/**
 * @fn void sol_log_print_function_syslog(void *data, const struct sol_log_domain *domain,
 * uint8_t message_level, const char *syslog, const char *function, int line, const char *format,
 * va_list args)

 * @brief Log to syslog.
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
#ifdef SOL_LOG_ENABLED
void sol_log_print_function_syslog(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *syslog, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_syslog(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *syslog, const char *function, int line, const char *format, va_list args)
{
}
#endif
#endif

#ifdef SOL_PLATFORM_LINUX
/**
 * @fn void sol_log_print_function_journal(void *data, const struct sol_log_domain *domain,
 * uint8_t message_level, const char *journal, const char *function, int line, const char *format,
 * va_list args)
 *
 * @brief Log to systemd's journal.
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
#ifdef SOL_LOG_ENABLED
void sol_log_print_function_journal(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *journal, const char *function, int line, const char *format, va_list args);
#else
static inline void
sol_log_print_function_journal(void *data, const struct sol_log_domain *domain, uint8_t message_level, const char *journal, const char *function, int line, const char *format, va_list args)
{
}
#endif
#endif

/**
 * @fn void sol_log_level_to_str(uint8_t level, char *buf, size_t buflen)
 *
 * @brief Convenience function to convert the logging @c level to string.
 *
 * @param level Logging level
 * @param buf Where to write the string
 * @param buflen Buffer size
 */

/**
 * @fn const char *sol_log_get_level_color(uint8_t level)
 *
 * @brief Get the color code used for the given logging level @c level.
 *
 * @param level Logging level
 *
 * @return Color code string
 */

/**
 * @fn uint8_t sol_log_get_abort_level(void)
 *
 * @brief Get the logging level that triggers the program to abort.
 *
 * @return The logging level
 */

/**
 * @fn uint8_t sol_log_get_level(void)
 *
 * @brief Get the maximum log level allowed.
 *
 * @return The logging level
 */

/**
 * @fn bool sol_log_get_show_colors(void)
 *
 * @brief Get if color output is enabled or not.
 *
 * @return @c true if enabled, @c false otherwise
 */

/**
 * @fn bool sol_log_get_show_file(void)
 *
 * @brief Get if showing source file's name is enabled or not.
 *
 * @return @c true if enabled, @c false otherwise
 */

/**
 * @fn bool sol_log_get_show_function(void)
 *
 * @brief Get if showing function's name is enabled or not.
 *
 * @return @c true if enabled, @c false otherwise
 */

/**
 * @fn bool sol_log_get_show_line(void)
 *
 * @brief Get if showing the line number is enabled or not.
 *
 * @return @c true if enabled, @c false otherwise
 */

/**
 * @fn void sol_log_set_abort_level(uint8_t level)
 *
 * @brief Set the logging level that should trigger the program to abort.
 *
 * @param level Logging level
 */

/**
 * @fn void sol_log_set_level(uint8_t level)
 *
 * @brief Set the global domain maximum level to @c level.
 *
 * @param level Logging level
 */

/**
 * @fn void sol_log_set_show_colors(bool enabled)
 *
 * @brief Enable/Disables the use of colors in logging messages.
 *
 * @param enabled Enables color if @c true, disables if @c false
 */

/**
 * @fn void sol_log_set_show_file(bool enabled)
 *
 * @brief Enable/Disables the output of source file's name in logging messages.
 *
 * @param enabled Enables file's name output if @c true, disables if @c false
 */

/**
 * @fn void sol_log_set_show_function(bool enabled)
 *
 * @brief Enable/Disables the output of function's name containing the logging messages.
 *
 * @param enabled Enables function's name output if @c true, disables if @c false
 */

/**
 * @fn void sol_log_set_show_line(bool enabled)
 *
 * @brief Enable/Disables the output of the line number in logging messages.
 *
 * @param enabled Enables line number output if @c true, disables if @c false
 */

#ifdef SOL_LOG_ENABLED
/*
 * Those implementing custom logging functions may use the following getters
 */
void sol_log_level_to_str(uint8_t level, char *buf, size_t buflen);
const char *sol_log_get_level_color(uint8_t level);
uint8_t sol_log_get_abort_level(void);
uint8_t sol_log_get_level(void);
bool sol_log_get_show_colors(void);
bool sol_log_get_show_file(void);
bool sol_log_get_show_function(void);
bool sol_log_get_show_line(void);

/*
 * To force some logging setting independent of platform initializations,
 * use the following setters
 */
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
