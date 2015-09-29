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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Solleta platform interaction.
 */

/**
 * @defgroup Platform Platform
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

const char *sol_platform_get_board_name(void);

/**
 * Retrieves, in @a id, the machine-id present in the file system. The
 * returned string is assured to be a valid, 16 bytes-long (128 bits)
 * UUID.
 *
 * @param id Where to store the found machine id. It's 33 bytes in lenght
 *           so it accomodates the maximum lenght case -- 2 * 16
 *           (chars) + 1 (\0)
 *
 * @note: If the environment variable SOL_MACHINE_ID is set and is
 * properly formatted as a UUID, its value is returned by this call.
 *
 * @return 0 on success, negative error code otherwise.
 */
int sol_platform_get_machine_id(char id[static 33]);

/**
 * Retrieves, in @a number, the platform's main board serial
 * number/identifier.
 *
 * @param number On success, it's set to returning serial number
 *               string. It must be freed after usage.
 *
 * @note: If the environment variable SOL_SERIAL_NUMBER is set, its
 * value is returned by this call.
 *
 * @return 0 on success, negative error code otherwise.
 */
int sol_platform_get_serial_number(char **number);

/**
 * Retrieves the version of Soletta that is running.
 *
 * @return On success, it returns the version string, that must not be
 * modified. On error, it returns @c NULL.
 */
const char *sol_platform_get_sw_version(void);

/**
 * Retrieves the operating system's version that Soletta is running
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
 * @}
 */

#ifdef __cplusplus
}
#endif
