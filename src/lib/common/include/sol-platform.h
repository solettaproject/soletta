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

#pragma once

#include <stdlib.h>

#include "sol-vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta platform interaction.
 */

/**
 * @defgroup Platform Platform
 *
 * @brief The Platform API is about target states and services
 *
 * It is specially useful when Soletta is used in a PID 1 process.
 *
 * @{
 */

/**
 * @brief Retrieves the name of the board on which Soletta is running.
 *
 * @return String containing the board name on success, @c NULL otherwise.
 *
 * @note: Check the Board Detection documentation for more information
 * about how the name is found.
 */
const char *sol_platform_get_board_name(void);

/**
 * @brief Retrieves the machine-id present in the file system as a string.
 *
 * The returned string is assured to be a valid null terminated string with
 * the 16 bytes-long (128 bits) UUID encoded as hexadecimal ASCII characters.
 *
 * @note: If the environment variable SOL_MACHINE_ID is set and is
 * properly formatted as a UUID string, its value is returned by this call.
 *
 * @return On success, it returns the machine id string, that must not be
 * modified. On error, it returns @c NULL.
 */
const char *sol_platform_get_machine_id(void);

/**
 * @brief Retrieves the machine-id present in the file system as a byte array.
 *
 * The returned byte array is assured to be a 16 bytes-long (128 bits) array
 * with the machine UUID.
 *
 * @note: If the environment variable SOL_MACHINE_ID is set and is
 * properly formatted as a UUID byte array, its value is returned by this call.
 *
 * @return On success, it returns the machine id as a byte array, that must not
 * be modified. On error, it returns @c NULL.
 */
const uint8_t *sol_platform_get_machine_id_as_bytes(void);

/**
 * @brief Retrieves, in @a number, the platform's main board serial
 * number/identifier.
 *
 * @note: If the environment variable SOL_SERIAL_NUMBER is set, its
 * value is returned by this call.
 *
 * @return On success, it returns the serial number string, that must not be
 * modified. On error, it returns @c NULL.
 */
const char *sol_platform_get_serial_number(void);

/**
 * @brief Retrieves the version of Soletta that is running.
 *
 * @return On success, it returns the version string, that must not be
 * modified. On error, it returns @c NULL.
 */
const char *sol_platform_get_sw_version(void);

/**
 * @brief Retrieves the operating system's version that Soletta is running
 * on top of.
 *
 * @return On success, it returns the version string. This string should
 * not be freed after usage. On error, it returns @c NULL.
 */
const char *sol_platform_get_os_version(void);

/**
 * @brief List of platform states.
 */
enum sol_platform_state {
    SOL_PLATFORM_STATE_INITIALIZING, /**< @brief Initializing */
    SOL_PLATFORM_STATE_RUNNING, /**< @brief Running */
    SOL_PLATFORM_STATE_DEGRADED, /**< @brief Degraded */
    SOL_PLATFORM_STATE_MAINTENANCE, /**< @brief Maintenance */
    SOL_PLATFORM_STATE_STOPPING, /**< @brief Stopping */
    SOL_PLATFORM_STATE_UNKNOWN = -1 /**< @brief Unknown */
};

/**
 * @brief Retrieves the current platform state.
 *
 * @return Platform current state
 *
 * @see enum sol_platform_state
 */
int sol_platform_get_state(void);

/**
 * @brief Adds a state monitor.
 *
 * Whenever the platform state changes, @c cb is called receiving the new state
 * and @c data.
 *
 * @param cb Callback
 * @param data Callback data
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_add_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data);

/**
 * @brief Removes a state monitor.
 *
 * @param cb Callback to be removed
 * @param data Callback data
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_del_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data);

/**
 * @brief List of service states.
 */
enum sol_platform_service_state {
    SOL_PLATFORM_SERVICE_STATE_ACTIVE, /**< @brief Active */
    SOL_PLATFORM_SERVICE_STATE_RELOADING, /**< @brief Reloading */
    SOL_PLATFORM_SERVICE_STATE_INACTIVE, /**< @brief Inactive */
    SOL_PLATFORM_SERVICE_STATE_FAILED, /**< @brief Failed */
    SOL_PLATFORM_SERVICE_STATE_ACTIVATING, /**< @brief Activating */
    SOL_PLATFORM_SERVICE_STATE_DEACTIVATING, /**< @brief Deactivating */
    SOL_PLATFORM_SERVICE_STATE_UNKNOWN = -1 /**< @brief Unknown */
};

/**
 * @brief A locale category.
 */
enum sol_platform_locale_category {
    SOL_PLATFORM_LOCALE_LANGUAGE,
    SOL_PLATFORM_LOCALE_ADDRESS,
    SOL_PLATFORM_LOCALE_COLLATE,
    SOL_PLATFORM_LOCALE_CTYPE,
    SOL_PLATFORM_LOCALE_IDENTIFICATION,
    SOL_PLATFORM_LOCALE_MEASUREMENT,
    SOL_PLATFORM_LOCALE_MESSAGES,
    SOL_PLATFORM_LOCALE_MONETARY,
    SOL_PLATFORM_LOCALE_NAME,
    SOL_PLATFORM_LOCALE_NUMERIC,
    SOL_PLATFORM_LOCALE_PAPER,
    SOL_PLATFORM_LOCALE_TELEPHONE,
    SOL_PLATFORM_LOCALE_TIME,
    SOL_PLATFORM_LOCALE_UNKNOWN = -1
};

/**
 * @brief Retrieves the state of a given service.
 *
 * @param service Service to be queried
 *
 * @return Service state
 *
 * @see enum sol_platform_service_state
 */
enum sol_platform_service_state sol_platform_get_service_state(const char *service);

/**
 * @brief Adds a service monitor.
 *
 * @c service will be monitored and whenever it's state changes, @c cb will be called
 * receiving the new state and the provided @c data.
 *
 * @param cb Callback
 * @param service Service name
 * @param data Callback data
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_add_service_monitor(void (*cb)(void *data, const char *service,
    enum sol_platform_service_state state),
    const char *service,
    const void *data);

/**
 * @brief Removes a service monitor.
 *
 * @param cb Callback to be removed
 * @param service Service name from which @c cb should be removed
 * @param data Callback data
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_del_service_monitor(void (*cb)(void *data, const char *service,
    enum sol_platform_service_state state),
    const char *service,
    const void *data);

/**
 * @brief Starts a given service.
 *
 * @param service Service name
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_start_service(const char *service);

/**
 * @brief Stops a given service.
 *
 * @param service Service name
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_stop_service(const char *service);

/**
 * @brief Restarts a given service.
 *
 * @param service Service name
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_restart_service(const char *service);

#define SOL_PLATFORM_TARGET_DEFAULT    "default" /**< @brief Default target string */
#define SOL_PLATFORM_TARGET_RESCUE     "rescue" /**< @brief Rescue target string */
#define SOL_PLATFORM_TARGET_EMERGENCY  "emergency" /**< @brief Emergency target string */
#define SOL_PLATFORM_TARGET_POWER_OFF   "poweroff" /**< @brief Power-off target string */
#define SOL_PLATFORM_TARGET_REBOOT     "reboot" /**< @brief Reboot target string */
#define SOL_PLATFORM_TARGET_SUSPEND    "suspend" /**< @brief Suspend target string */

/**
 * @brief Set the platform target.
 *
 * @param target Target name
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_platform_set_target(const char *target);

/**
 * @brief List mount points mounted by us on hotplug events.
 *
 * @param vector Initialized sol_vector used to store the resulting list
 *
 * @return 0 on success, negative errno otherwise
 */
int sol_platform_get_mount_points(struct sol_ptr_vector *vector);

/**
 * @brief Umount a @c mpoint
 *
 * @param mpoint The mount point to be unmounted
 * @param cb Callback to be called when unmount operation finishes
 * @param data Context data to be provided to @c async_cb function
 *
 * @return 0 on success, negative errno otherwise
 */
int sol_platform_unmount(const char *mpoint, void (*cb)(void *data, const char *mpoint, int error), const void *data);

/**
 * @brief Gets the hostname.
 *
 * @return The hostname or @c NULL on error.
 *
 * @see sol_platform_set_hostname()
 */
const char *sol_platform_get_hostname(void);

/**
 * @brief Changes the hostname to @c name.
 *
 * @param name The new hostname.
 *
 * @return @c 0 on success, negative errno otherwise.
 *
 * @see sol_platform_get_hostname()
 */
int sol_platform_set_hostname(const char *name);

/**
 * @brief Adds a hostname monitor.
 *
 * If the hostname changes @c cb will be called.
 *
 * @param cb The callback that will inform the new hostname.
 * @param data The data to the callback.
 * @return @c 0 on success, negative errno otherwise.
 *
 * @see sol_platform_del_hostname_monitor()
 */
int sol_platform_add_hostname_monitor(void (*cb)(void *data, const char *hostname), const void *data);

/**
 * @brief Remove a hostname monitor.
 *
 * @param cb The registered callback.
 * @param data The data to the callback.
 * @return @c 0 on success, negative errno otherwise.
 *
 *
 * @see sol_platform_add_hostname_monitor()
 */
int sol_platform_del_hostname_monitor(void (*cb)(void *data, const char *hostname), const void *data);

/**
 * @brief Set the system wide time.
 *
 * @param timestamp The new system_clock
 * @return @c 0 on succes, negative errno otherwise
 *
 * @note The returned value is the number of seconds since 1970-01-01 00:00:00 +0000 (UTC)
 *
 * @see sol_platform_get_system_clock()
 */
int sol_platform_set_system_clock(int64_t timestamp);

/**
 * @brief Get the current system time.
 *
 * @return the system time on success, negative errno on error
 *
 * @note The returned value is the number of seconds since 1970-01-01 00:00:00 +0000 (UTC)
 *
 * @see sol_platform_set_system_clock()
 */
int64_t sol_platform_get_system_clock(void);

/**
 * @brief Add a callback to monitor system clock changes.
 *
 * @param cb A callback to be called when the system clock changes
 * @param data The data to @c cb
 * @return @c 0 on success, negative errno otherwise
 *
 * @note The registered callback will not be called every second (a.k.a: this is not a timer!), it will only
 * be called if the system clock is adjusted. If one needs a timer use sol_timeout_add()
 * @see sol_platform_del_system_clock_monitor()
 */
int sol_platform_add_system_clock_monitor(void (*cb)(void *data, int64_t timestamp), const void *data);

/**
 * @brief Delete a register system_clock monitor.
 *
 * @param cb The previous registered callback
 * @param data The data to @c cb
 * @return @c 0 on success, negative errno otherwise
 *
 * @see sol_platform_add_system_clock_monitor()
 */
int sol_platform_del_system_clock_monitor(void (*cb)(void *data, int64_t timestamp), const void *data);

/**
 * @brief Set the system timezone.
 *
 * @param tzone The new timezone. (Example: America/Sao_Paulo)
 *
 * @return @c 0 on success, negative errno on error
 *
 * @note The new timezone must be avaible at /usr/share/zoneinfo/
 *
 * @see sol_platform_get_timezone()
 */
int sol_platform_set_timezone(const char *tzone);

/**
 * @brief Get the current timezone.
 *
 * @return The timezone or @c NULL on error
 *
 * @see sol_platform_set_timezone()
 */
const char *sol_platform_get_timezone(void);

/**
 * @brief Add a timezone monitor.
 *
 * @param cb A callback to be called when the timezone changes
 * @param data The data to @c cb
 *
 * @return @c 0 on success, negative errno otherwise
 */
int sol_platform_add_timezone_monitor(void (*cb)(void *data, const char *timezone), const void *data);

/**
 * @brief Remove a timezone monitor.
 *
 * @param cb The previous registered callback
 * @param data The data to @c cb
 *
 * @return @c 0 on success, negative errno otherwise
 */
int sol_platform_del_timezone_monitor(void (*cb)(void *data, const char *timezone), const void *data);

/**
 * @brief Set locale for a category.
 *
 * This function will change the system wide locale for a given category.
 * The already running proceses might not be aware that the locale has changed.
 *
 * @param category The category to set the new locale
 * @param locale The locale string (Example: en_US.UTF-8)
 *
 * @return 0 on success, negative errno on error
 *
 * @note This function only saves the new locale category in the disk, in order to use it in
 * the current process, one must call sol_platform_apply_locale() after using sol_platform_set_locale()
 *
 * @see sol_platform_get_locale()
 * @see sol_platform_apply_locale()
 */
int sol_platform_set_locale(enum sol_platform_locale_category category, const char *locale);

/**
 * @brief Get the current locale of a given category.
 *
 * @param category The category which one wants to know the locale
 * @return The locale value on success, @c NULL otherwise
 *
 * @see sol_platform_set_locale()
 */
const char *sol_platform_get_locale(enum sol_platform_locale_category category);

/**
 * @brief Add a locale monitor.
 *
 * @param cb A callback to be called when the locale changes
 * @param data The data to @c cb
 *
 * @return 0 on success, negative errno otherwise.
 *
 * @note If an error happens while the locale is being monitored the @c cb
 * will be called and @c category will be set to #SOL_PLATFORM_SERVICE_STATE_UNKNOWN and @c locale to @c NULL.
 */
int sol_platform_add_locale_monitor(void (*cb)(void *data, enum sol_platform_locale_category category, const char *locale), const void *data);

/**
 * @brief Remove a locale monitor.
 *
 * @param cb The previous registered callback
 * @param data The data to @c cb
 *
 * @return 0 on success, negative errno otherwise
 */
int sol_platform_del_locale_monitor(void (*cb)(void *data, enum sol_platform_locale_category category, const char *locale), const void *data);

/**
 * @brief Apply the locale category to the process.
 *
 * This function sets the current locale of the given category to the process,
 * in order to set a new locale value to a category use sol_platform_set_locale().
 *
 * @param category The category to set the process locale
 * @return 0 on success, negative errno otherwise
 * @see sol_platform_set_locale()
 */
int sol_platform_apply_locale(enum sol_platform_locale_category category);

/**
 * Get current app name.
 *
 * Generate current app name from sol_argv[0]. If argv is not set, app name
 * will be soletta.
 *
 * @return The current app name.
 */
struct sol_str_slice sol_platform_get_appname(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
