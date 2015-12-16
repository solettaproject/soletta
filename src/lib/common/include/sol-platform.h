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
 * @brief These routines are used for Soletta platform interaction.
 *
 * @{
 */

#define CHUNK_READ_SIZE 1024
/* allow reading loop to take up to this amount of bytes, then stop
 * the chunk reading and allow mainloop to run again. This keeps
 * memory usage low.
 */
#define CHUNK_READ_MAX (10 * (CHUNK_READ_SIZE))
/* allow reading/writing loop to take up to this nanoseconds, then stop the
 * chunk reading and allow mainloop to run again. This keeps
 * interactivity.
 */
#define CHUNK_MAX_TIME_NS (20 * (NSEC_PER_MSEC))

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
 * @brief Retrieves, in @a id, the machine-id present in the file system.
 *
 * The returned string is assured to be a valid, 16 bytes-long (128 bits) UUID.
 *
 * @note: If the environment variable SOL_MACHINE_ID is set and is
 * properly formatted as a UUID, its value is returned by this call.
 *
 * @return On success, it returns the machine id string, that must not be
 * modified. On error, it returns @c NULL.
 */
const char *sol_platform_get_machine_id(void);

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

enum sol_platform_state {
    SOL_PLATFORM_STATE_INITIALIZING,
    SOL_PLATFORM_STATE_RUNNING,
    SOL_PLATFORM_STATE_DEGRADED,
    SOL_PLATFORM_STATE_MAINTENANCE,
    SOL_PLATFORM_STATE_STOPPING,
    SOL_PLATFORM_STATE_UNKNOWN = -1
};

int sol_platform_get_state(void);

int sol_platform_add_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data);
int sol_platform_del_state_monitor(void (*cb)(void *data,
    enum sol_platform_state state),
    const void *data);


enum sol_platform_service_state {
    SOL_PLATFORM_SERVICE_STATE_ACTIVE,
    SOL_PLATFORM_SERVICE_STATE_RELOADING,
    SOL_PLATFORM_SERVICE_STATE_INACTIVE,
    SOL_PLATFORM_SERVICE_STATE_FAILED,
    SOL_PLATFORM_SERVICE_STATE_ACTIVATING,
    SOL_PLATFORM_SERVICE_STATE_DEACTIVATING,
    SOL_PLATFORM_SERVICE_STATE_UNKNOWN = -1
};

enum sol_platform_service_state sol_platform_get_service_state(const char *service);

int sol_platform_add_service_monitor(void (*cb)(void *data, const char *service,
    enum sol_platform_service_state state),
    const char *service,
    const void *data);
int sol_platform_del_service_monitor(void (*cb)(void *data, const char *service,
    enum sol_platform_service_state state),
    const char *service,
    const void *data);

int sol_platform_start_service(const char *service);
int sol_platform_stop_service(const char *service);
int sol_platform_restart_service(const char *service);

#define SOL_PLATFORM_TARGET_DEFAULT    "default"
#define SOL_PLATFORM_TARGET_RESCUE     "rescue"
#define SOL_PLATFORM_TARGET_EMERGENCY  "emergency"
#define SOL_PLATFORM_TARGET_POWEROFF   "poweroff"
#define SOL_PLATFORM_TARGET_REBOOT     "reboot"
#define SOL_PLATFORM_TARGET_SUSPEND    "suspend"

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
 * @brief Set the system wide time
 *
 * @param timestamp The new system_clock.
 * @return @c 0 on succes, negative errno otherwise.
 *
 * @note The @c timestamp is relative to 1970-01-01 00:00:00 +0000 (UTC).
 * @see sol_platform_get_system_clock()
 */
int sol_platform_set_system_clock(int64_t timestamp);

/**
 * @brief Get the current system time
 *
 * @return the system time on success, negative errno on error.
 *
 * @note The returned value is the number of seconds since 1970-01-01 00:00:00 +0000 (UTC).
 *
 * @see sol_platform_set_system_clock()
 */
int64_t sol_platform_get_system_clock(void);

/**
 * @brief Add a callback to monitor system clock changes.
 *
 * @param cb A callback to be called when the system clock changes.
 * @param data The data to @c cb.
 * @return @c 0 on success, negative errno otherwise.
 *
 * @note The registered callback will not be called every second (a.k.a: this is not a timer!), it will only
 * be called if the system clock is adjusted. If one needs a timer use sol_timeout_add()
 * @see sol_platform_del_system_clock_monitor()
 */
int sol_platform_add_system_clock_monitor(void (*cb)(void *data, int64_t timestamp), const void *data);

/**
 * @brief Delete a register system_clock monitor
 *
 * @param cb The previous registered callback.
 * @param data The data to @c cb.
 * @return @c 0 on success, negative errno otherwise.
 *
 * @see sol_platform_add_system_clock_monitor()
 */
int sol_platform_del_system_clock_monitor(void (*cb)(void *data, int64_t timestamp), const void *data);

/**
 * @brief Set the system timezone
 *
 * @param timezone The new timezone. (Example: America/Sao Paulo)
 *
 * @return @c 0 on success, negative errno on error.
 *
 * @note The new timezone must be avaible at /usr/share/zoneinfo/
 *
 * @see sol_platform_get_timezone()
 */
int sol_platform_set_timezone(const char *timezone);

/**
 * @brief Get the current timezone
 *
 * @return The timezone or @c NULL on error
 *
 * @see sol_platform_set_timezone()
 */
const char *sol_platform_get_timezone(void);

/**
 * @brief Add a timezone monitor.
 *
 * @param cb A callback to be called when the timezone changes.
 * @param data The data to @c cb.
 *
 * @return @c 0 on success, negative errno otherwise.
 */
int sol_platform_add_timezone_monitor(void (*cb)(void *data, const char *timezone), const void *data);

/**
 * @brief Remove a timezone monitor.
 *
 * @param cb The previous registered callback.
 * @param data The data to @c cb.
 *
 * @return @c 0 on success, negative errno otherwise.
 */
int sol_platform_del_timezone_monitor(void (*cb)(void *data, const char *timezone), const void *data);


/**
 * @}
 */

#ifdef __cplusplus
}
#endif
