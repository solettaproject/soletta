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

#include <stdbool.h>
#include <stdint.h>

#include "sol-macros.h"
#include "sol-platform.h"
#include "sol-str-slice.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta platform Linux interaction.
 * @ingroup Platform
 */

/**
 * @brief Fork a new process and run the given @a on_fork callback on that process.
 *
 * This call will execute fork(), then wait for children to be ready
 * (internally uses a pipe() to synchronize), reset all signal
 * handlers to their default values and then call @a on_fork. This
 * user-provided function may then do whatever it wants and when it
 * returns the process will exit with @c EXIT_SUCCESS. The other way
 * to exit is to call sol_platform_linux_fork_run_exit().
 *
 * @note whenever the user wants to exit with a different code or
 *       prematurely, never call exit() directly, instead call
 *       sol_platform_linux_fork_run_exit() (or _exit()). The reason
 *       is that functions registered with atexit() or on_exit()
 *       shouldn't be executed in the children processes.
 *
 * When the child exits, the user-provided @a on_child_exit is called
 * and after this point the handle returned by
 * sol_platform_linux_fork_run() is invalidated (freed), thus
 * shouldn't be used any further.
 *
 * The main process may force the child process to stop using
 * sol_platform_linux_fork_run_stop(), that blocking call will send @c
 * SIGTERM and wait the child to exit using waitpid(), calling @a
 * on_child_exit before it returns.
 *
 * @param on_fork the function to call back after the child process is
 *        ready. The given data is the same pointer given as @a data.
 * @param on_child_exit the function to call back on the main
 *        (original) process when the child exits. The given data is
 *        the same pointer given as @a data, @c pid is the process
 *        identifier (as returned by
 *        sol_platform_linux_fork_run_get_pid()) and the status is the
 *        one returned by libc waitpid(), that means the macros such
 *        as WIFEXITED(), WEXITSTATUS(), WIFSIGNALED(), WTERMSIG() and
 *        similar from @c sys/wait.h should be used to interpret it.
 * @param data the context data to give to the callbacks @a on_fork
 *        and @a on_child_exit. It is not modified in any way by
 *        sol_platform_linux_fork_run functions.
 *
 * @return the handle to the child process or @c NULL on errors (and
 *         @c errno is set to the actual reason). The handle may be
 *         used to query the process identifier, to stop the process.
 *
 * @see sol_platform_linux_fork_run_stop()
 * @see sol_platform_linux_fork_run_get_pid()
 * @see sol_platform_linux_fork_run_exit()
 */
struct sol_platform_linux_fork_run *sol_platform_linux_fork_run(void (*on_fork)(void *data), void (*on_child_exit)(void *data, uint64_t pid, int status), const void *data);

/**
 * @brief Force the child process to stop running and wait for it.
 *
 * This is a blocking function that will kill the child process using
 * @c SIGTERM, then waiting the process to exit using waitpid().
 *
 * After the process exit, the @c on_child_exit is called and the
 * given @a handle is invalidated (freed) before this function
 * returns.
 *
 * If the user doesn't want the blocking behavior he may use
 * sol_platform_linux_fork_run_send_signal() using @c SIGTERM and wait
 * @c on_child_exit to be called when the process exits.
 *
 * @param handle a valid (non-NULL and alive) handle returned by
 *        sol_platform_linux_fork_run() to be terminated. After this
 *        function return, the handle is then invalidated.
 * @return 0 on success or -errno on failure.
 *
 * @see sol_platform_linux_fork_run_send_signal()
 */
int sol_platform_linux_fork_run_stop(struct sol_platform_linux_fork_run *handle);


/**
 * @brief Send a signal to the child process.
 *
 * This is a helper using sol_platform_linux_fork_run_get_pid() and
 * kill(). It may be useful to send control signals such as @c SIGUSR1
 * and @c SIGUSR2.
 *
 * @param handle a valid (non-NULL and alive) handle returned by
 *        sol_platform_linux_fork_run().
 * @param sig the signal number, such as @c SIGUSR1.
 * @return 0 on success or -errno on failure.
 */
int sol_platform_linux_fork_run_send_signal(struct sol_platform_linux_fork_run *handle, int sig);

/**
 * @brief The process identifier of this child.
 *
 * @param handle a valid (non-NULL and alive) handle returned by
 *        sol_platform_linux_fork_run().
 *
 * @return the processes identifier (PID) that can be used with
 *         syscalls such as kill(). On errors, UINT64_MAX is returned
 *         and errno is set to @c ENOENT.
 */
uint64_t sol_platform_linux_fork_run_get_pid(const struct sol_platform_linux_fork_run *handle);

/**
 * @brief Exit from a child process.
 *
 * This function is to be called from inside @c on_fork to exit the
 * child process using a specific status, such as @c EXIT_FAILURE or
 * @c EXIT_SUCCESS.
 *
 * Children process should never call exit() directly, rather use this
 * function or _exit().
 *
 * @param status the value to use to exit this process, it will be
 *        available to the main process via the @c on_child_exit @c
 *        status parameter, in that case use the @c sys/wait.h macros
 *        to get the actual value, such as WEXITSTATUS().
 */
void sol_platform_linux_fork_run_exit(int status) SOL_ATTR_NO_RETURN;

/**
 * @brief Mounts a device in a specific location.
 *
 * @param dev The device to be mounted
 * @param mpoint The location to mount the device
 * @param fstype The file system type
 * @param cb Callback used to inform that the device was pointed with the proper status.
 * @param data User data to @c cb
 *
 * @return 0 on success or -errno on error.
 */
int sol_platform_linux_mount(const char *dev, const char *mpoint, const char *fstype, void (*cb)(void *data, const char *mpoint, int status), const void *data);

/**
 * @brief Struct that contains information about an uevent.
 *
 * @see sol_platform_linux_uevent_subscribe()
 * @see sol_platform_linux_uevent_unsubscribe()
 */
typedef struct sol_uevent {
    struct sol_str_slice modalias; /**<  The alias */
    struct sol_str_slice action; /**< The uevent action */
    struct sol_str_slice subsystem; /**< The event subsystem*/
    struct sol_str_slice devtype; /**< The device type */
    struct sol_str_slice devname; /**< The device name */
} sol_uevent;

/**
 * @brief Subscribe to monitor linux's uevent events
 *
 * @param action The action desired to monitor - i.e add, remove etc
 * @param subsystem The subsystem of interest
 * @param cb The callback to be issued after event catch
 * @param data The context pointer to be provided to @c cb
 *
 * @return 0 on success, negative errno otherwise
 */
int sol_platform_linux_uevent_subscribe(const char *action, const char *subsystem, void (*cb)(void *data, struct sol_uevent *uevent), const void *data);

/**
 * @brief Unsubscribe @c uevent_cb() for @c action and @c subsystem events monitoring
 *
 * @param action The action we're monitoring and want to release the callback
 * @param subsystem The subsystem we're monitoring and want to release the callback
 * @param cb The callback itself
 * @param data The context pointer to be provided to @c cb
 *
 * @return 0 on success, negative errno otherwise
 */
int sol_platform_linux_uevent_unsubscribe(const char *action, const char *subsystem, void (*cb)(void *data, struct sol_uevent *uevent), const void *data);

#ifdef __cplusplus
}
#endif
