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

#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

// TODO abstract locks? see eina_lock.h
struct sol_worker_thread;
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
 * @param setup function to be called once from the @b worker thread,
 *        before all other thread functions. It's always called if
 *        sol_worker_thread_new() returns non-NULL. If the setup
 *        function returns false then the thread is terminated and no
 *        further functions will be called from the thread -- that is,
 *        only the @a finished may be called. May be @c NULL if
 *        nothing is to be done.
 *
 * @param cleanup function to be called once from the @b worker thread,
 *        after all other thread functions. It's always called if
 *        sol_worker_thread_new() returns non-NULL. If the @a setup
 *        function returns false, then this function will @b not be
 *        called! May be @c NULL if nothing is to be done.
 *
 * @param iterate function to be called repeatedly from the @b worker
 *        thread until it returns false or sol_worker_thread_cancel()
 *        is called from the main thread. If the @a setup function
 *        returns false, then this function will @b not be called!
 *        This function must @b not be @c NULL.
 *
 * @param cancel function to be called from the @b main thread before
 *        the worker thread is to be terminated. There is @b no
 *        locking, then if you touch sensitive resources shared with
 *        the thread be sure to handle locking to avoid
 *        race-conditions -- both @a setup, @a iterate or @a cleanup
 *        may be in executing when this function is called! May be @c
 *        NULL if nothing is to be done.
 *
 * @param finished function to be called from the @b main thread
 *        after the worker thread is finished. After this function is
 *        called the pointer to the worker thread is freed and should
 *        be considered invalid. This function is called both when
 *        the work is finished (@a iterate returns false) and when
 *        the thread is cancelled with
 *        sol_worker_thread_cancel()). May be @c NULL if nothing is to
 *        be done.
 *
 * @param feedback function to be called from the @b main thread
 *        after the worker thread calls
 *        sol_worker_thread_feedback(). May be @c NULL if nothing is
 *        to be done.
 *
 * @param data the context data to give to all functions.
 *
 * @return newly allocated worker thread handle on success or @c NULL
 * on errors.
 *
 * @see sol_worker_thread_cancel()
 * @see sol_worker_thread_feedback()
 * @see sol_idle_add()
 */
struct sol_worker_thread *sol_worker_thread_new(bool (*setup)(void *data), void (*cleanup)(void *data), bool (*iterate)(void *data), void (*cancel)(void *data), void (*finished)(void *data), void (*feedback)(void *data), const void *data);
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
