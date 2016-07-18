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
#include <inttypes.h>

#include "sol-common-buildopts.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for worker threads manipulation under Soletta.
 */

/**
 * @defgroup WorkerThread Worker Thread
 *
 * @{
 */


/**
 * @typedef sol_worker_thread
 * @brief A worker thread handle
 * @see sol_worker_thread_new()
 */
// TODO abstract locks? see eina_lock.h
struct sol_worker_thread;
typedef struct sol_worker_thread sol_worker_thread;

/**
 * @brief Worker thread functions and context data configuration.
 */
typedef struct sol_worker_thread_config {
#ifndef SOL_NO_API_VERSION
#define SOL_WORKER_THREAD_CONFIG_API_VERSION (1)
    /** must match SOL_WORKER_THREAD_CONFIG_API_VERSION in runtime */
    uint16_t api_version;
#endif
    /** the context data to give to all functions. */
    const void *data;
    /**
     * function to be called once from the @b worker thread, before all
     * other thread functions. It's always called if sol_worker_thread_new()
     * returns non-NULL. If the setup function returns false then the thread
     * is terminated and no further functions will be called from the thread
     * -- that is, only the @a finished may be called. May be @c NULL if
     * nothing is to be done.
     */
    bool (*setup)(void *data);
    /**
     * function to be called once from the @b worker thread, after all other
     * thread functions. It's always called if sol_worker_thread_new()
     * returns non-NULL. If the @a setup function returns false, then this
     * function will @b not be called! May be @c NULL if nothing is to be done.
     */
    void (*cleanup)(void *data);
    /**
     * function to be called repeatedly from the @b worker thread until it
     * returns false or sol_worker_thread_cancel() is called from the main
     * thread. If the @a setup function returns false, then this function will
     * @b not be called! This function must @b not be @c NULL.
     */
    bool (*iterate)(void *data);
    /**
     * function to be called from the @b main thread before the worker thread
     * is to be terminated. There is @b no locking, then if you touch sensitive
     * resources shared with the thread be sure to handle locking to avoid
     * race-conditions -- both @a setup, @a iterate or @a cleanup may be in
     * executing when this function is called! May be @c NULL if nothing is
     * to be done.
     */
    void (*cancel)(void *data);
    /**
     * function to be called from the @b main thread after the worker thread
     * is finished. After this function is called the pointer to the worker
     * thread is freed and should be considered invalid. This function is
     * called both when the work is finished (@a iterate returns false) and
     * when the thread is cancelled with sol_worker_thread_cancel()). May be
     * @c NULL if nothing is to be done.
     */
    void (*finished)(void *data);
    /**
     * function to be called from the @b main thread after the worker thread
     * calls sol_worker_thread_feedback(). May be @c NULL if nothing is to be
     * done.
     */
    void (*feedback)(void *data);
} sol_worker_thread_config;

/**
 * Create and run a worker thread.
 *
 * Worker threads are meant to do processing that is hard to split and
 * play nice with cooperative workloads used by the main loop
 * (sol_idle_add(), sol_timeout_add() and sol_fd_add()). Usually this is
 * due blocking operating system calls or third party libraries that
 * don't allow work to be segmented.
 *
 * Worker threads shouldn't impact the main thread while they execute,
 * but this comes at the trade off of code complexity and
 * synchronization issues. If both the worker thread and the main
 * thread may operate on the same data simultaneously, then it may
 * result in partial reads and writes leading to inconsistent results
 * if locks are not properly done. The best approach is to have the
 * worker thread to operate on its own exclusive data and after it's
 * finished deliver that data to users from within @a finished
 * callback. If this pattern cannot be used, then employ locks to
 * segments of data that may result into race conditions.
 *
 * @note this function must be called from the @b main thread.
 *
 * @param config worker thread configuration with functions and context
 *        data to be used.
 *
 * @return newly allocated worker thread handle on success or @c NULL
 * on errors.
 *
 * @see sol_worker_thread_config
 * @see sol_worker_thread_cancel()
 * @see sol_worker_thread_feedback()
 * @see sol_idle_add()
 */
struct sol_worker_thread *sol_worker_thread_new(const struct sol_worker_thread_config *config);

/**
 * Cancel a worker thread.
 *
 * The cancel function will inform the thread it should stop working,
 * there is no preemptive cancellation -- both @c setup(), @c
 * iterate() and @c cleanup() (see sol_worker_thread_new()) will be
 * executed and take the time they need, meanwhile this function will
 * block waiting. After this function is called no further calls to @c
 * iterate() will be done.
 *
 * If sol_worker_thread_new() was provided with a @c cancel() function,
 * then that function will be called prior to any cancellation
 * work. You may use that function to schedule work cancellation in
 * more fine grained fashion in your own code.
 *
 * @note this function must be called from the @b main thread.
 *
 * @param thread a valid worker thread handle.
 *
 * @see sol_worker_thread_new()
 */
void sol_worker_thread_cancel(struct sol_worker_thread *thread);

/**
 * Check if a worker thread has been marked as cancelled.
 *
 * @note this function may be called from both @b main and @b worker thread.
 *
 * @param thread a valid worker thread handle.
 *
 * @return @c true if worker thread is marked cancelled or @c false otherwise.
 *
 * @see sol_worker_thread_cancel()
 */
bool sol_worker_thread_is_cancelled(const struct sol_worker_thread *thread);

/**
 * Schedule feedback from the worker to the main thread.
 *
 * This function will schedule a call to @c feedback() function given
 * to sol_worker_thread_new(). This call is not guaranteed to be
 * executed and multiple calls to sol_worker_thread_feedback() will not
 * queue, a single one will be done. If queuing is to be done, then do
 * it on your own using locks and a list/array in the @c data context.
 *
 * When @c feedback() is called from the main thread there is no
 * locking of data and the worker thread may be executing both @c
 * setup(), @c iterate() or @c cleanup().
 *
 * @note this function must be called from the @b worker thread.
 *
 * @param thread a valid worker thread handle.
 *
 * @see sol_worker_thread_new()
 */
void sol_worker_thread_feedback(struct sol_worker_thread *thread);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
