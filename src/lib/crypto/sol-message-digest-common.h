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

#define SOL_LOG_DOMAIN &_sol_message_digest_common_log_domain
extern struct sol_log_domain _sol_message_digest_common_log_domain;
#include "sol-log-internal.h"

#include "sol-message-digest.h"

int sol_message_digest_common_init(void);
void sol_message_digest_common_shutdown(void);

/**
 * Operations to use with the common message digest implementation.
 *
 * @internal
 */
struct sol_message_digest_common_ops {
    /* no api version as this struct is not exported */
    /**
     * Feed the algorithm with more data (@c mem of @c len bytes).
     *
     * This function is called from a thread if defined(PTHREAD) &&
     * defined(WORKER_THREAD), and in such case care may be needed
     * depending on the platform.
     *
     * It is guaranteed that the same handle is not called from
     * different worker thread, both @c feed and @c read_digest are
     * called from the same worker thread, while @c cleanup is called
     * from the main thread once the worker thread is already
     * finalized.
     *
     * If this function returns less then the requested amount of
     * bytes (@c len), then it is called again with a new @c mem
     * adapted (offset) to the remaining location and reduced @c len,
     * so it would do partial feeds.
     *
     * @param handle the message digest handle feeding the algorithm.
     * @param mem the pointer to memory to be feed.
     * @param len the size in bytes of memory to be feed.
     * @param is_last if this is the last chunk to be feed.
     *
     * @return number of bytes fed or -errno.
     */
    ssize_t (*feed)(struct sol_message_digest *handle, const void *mem, size_t len, bool is_last);
    /**
     * Read the digest from the message.
     *
     * This function is called from a thread if defined(PTHREAD) &&
     * defined(WORKER_THREAD), and in such case care may be needed
     * depending on the platform.
     *
     * It is guaranteed that the same handle is not called from
     * different worker thread, both @c feed and @c read_digest are
     * called from the same worker thread, while @c cleanup is called
     * from the main thread once the worker thread is already
     * finalized.
     *
     * If this function returns less then the requested amount of
     * bytes (@c len), then it is called again with a new @c mem
     * adapted (offset) to the remaining location and reduced @c len,
     * so it would do partial reads. The initial call will always be
     * enough to hold the whole digest as specified in @c digest_size;
     *
     * @param handle the message digest handle reading the hash.
     * @param mem the pointer to memory to store the digest.
     * @param len the size in bytes of memory to store the digest.
     * @return number of bytes fed or -errno.
     */
    ssize_t (*read_digest)(struct sol_message_digest *handle, void *mem, size_t len);
    /**
     * Cleanup any remaining resources before the handle is deleted.
     *
     * This functions is called from the main thread.
     */
    void (*cleanup)(struct sol_message_digest *handle);
};


/**
 * parameters to sol_message_digest_common_new(), used to avoid lots
 * of parameters with similar types (size_t), that could be confusing
 * and lead to mistakes.
 *
 * @internal
 */
struct sol_message_digest_common_new_params {
    /* no api version as this struct is not exported */
    /**
     * The handle given to sol_message_digest_new().
     *
     * It must be sanitized before calling this function, this is
     * considered safe since most users of this function will already
     * need to check parameters such as algorithm before calling, thus
     * it is not replicated in here.
     */
    const struct sol_message_digest_config *config;
    /**
     * The operations to feed and read the digest.
     *
     * @b NO copy is done, a reference is kept to it and thus it must
     * be valid during the lifecycle of the handle (until @c cleanup()
     * is called)
     */
    const struct sol_message_digest_common_ops *ops;
    /**
     * The algorithm specific context as a template, it will be copied
     * using @c memcpy() @c context_size. No references to the given
     * pointer are kept.
     *
     * If @c NULL, nothing is copied, but the memory is allocated
     * anyway.
     *
     * The actual context may be retrieved with
     * sol_message_digest_common_get_context().
     */
    const void *context_template;
    /**
     * The algorithm-specific context as an external handle (i.e. life
     * cycle not managed by Soletta). If set, context_template will be
     * ignored and context_size will be set to sizeof(void *) forcibly
     * in order to accomodate this pointer exactly. context_free()
     * must be set when this one is non-NULL.
     *
     * The actual context may be retrieved with
     * sol_message_digest_common_get_context().
     */
    const void *context_handle;
    /**
     * Free external context handle at exit.
     *
     * This function is called from the main thread.
     */
    void (*context_free)(void *context_handle);
    /**
     * Size in bytes of @c context_template, to copy with @c memcpy().
     */
    size_t context_size;
    /**
     * Size in bytes of the resulting digest.
     */
    size_t digest_size;
};

/**
 * This function creates the base handle.
 *
 * If it fails, then @c params.ops->cleanup() is @b NOT called, one
 * may need to do extra cleanups if needed.
 *
 * @param params the set of parameters is specified as a struct to
 *        avoid mistakes. Se its documentation for each parameter
 *        purpose and behavior.
 *
 * @internal
 */
struct sol_message_digest *sol_message_digest_common_new(const struct sol_message_digest_common_new_params params);
void *sol_message_digest_common_get_context(const struct sol_message_digest *handle);

