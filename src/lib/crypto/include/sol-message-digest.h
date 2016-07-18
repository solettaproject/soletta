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
#include <sol-common-buildopts.h>
#include <sol-types.h>
#include <sol-str-slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used to calculate message digest.
 */

/**
 * @defgroup Crypto Cryptography And Signatures
 *
 * These routines are used for cryptography and signature of data
 * using Soletta's API.
 */

/**
 * @defgroup Message_Digest Message Digest (Hash)
 * @ingroup Crypto
 *
 * Message Digest algorithms will take a byte stream and
 * compute a hash that may be used to later validate the
 * identity. Even the smallest variation of the input data
 * will have an avalanche effect that drastically change the
 * output data.
 *
 * Wikipedia says (https://en.wikipedia.org/wiki/Cryptographic_hash_function):
 * The ideal cryptographic hash function has four main properties:
 *  - it is easy to compute the hash value for any given message
 *  - it is infeasible to generate a message from its hash
 *  - it is infeasible to modify a message without changing the hash
 *  - it is infeasible to find two different messages with the same hash.
 *
 * Common Message Digest algorithms are CRC32, MD5, SHA1,
 * SHA256 and SHA512. Most of these are already broken, such
 * as CRC32 and nowadays MD5 and even SHA1, then before
 * picking one for your application, check the one that is
 * more secure and hard to break, such as SHA512.
 *
 * Soletta provides a portable API for message digests, but it doesn't
 * implement any of them. The actual work is done by the engine underneath,
 * which will depend on the build configuration. On Linux, either the kernel's
 * crypo API or OpenSSL's crypto library are available to choose at build time.
 * On RIOT-OS, only the algorithms provided by the OS are implemented. Other
 * systems lack support currently.
 *
 * When choosing an algorithm, Soletta will pass it down to the engine selected
 * for use, and to keep applications portable, it was chosen to always follow
 * the names used by the Linux kernel. If they don't match on other implementations,
 * Soletta will translate accordingly, keeping those difference transparent
 * for developers.
 *
 * @{
 */

/**
 * @typedef sol_message_digest
 * @brief A handle for a message digest
 */
struct sol_message_digest;
typedef struct sol_message_digest sol_message_digest;

/**
 * The message digest configuration to use when creating a new handle.
 *
 * @note Message digest follows the Soletta stream design pattern, which can be found here: @ref streams
 * @see sol_message_digest_new()
 */
typedef struct sol_message_digest_config {
#ifndef SOL_NO_API_VERSION
#define SOL_MESSAGE_DIGEST_CONFIG_API_VERSION (1)
    /**
     * api_version must match SOL_MESSAGE_DIGEST_CONFIG_API_VERSION
     * at runtime.
     */
    uint16_t api_version;
#endif
    /**
     * Algorithm name.
     *
     * The name should match Linux kernel's names, such as:
     *
     * @li md5
     * @li sha1
     * @li hmac(sha1)
     * @li crc32
     *
     * This pointer must @b NOT be @c NULL.
     */
    const char *algorithm;
    /**
     * If provided (length > 0), then is used by the message digest
     * hash function.
     *
     * A slice is used so the key may contain null-bytes inside, the
     * whole slice length will be used.
     */
    struct sol_str_slice key;
    /**
     * The mandatory callback function to report digest is ready.
     *
     * This pointer must @b NOT be @c NULL.
     *
     * The parameters are:
     *
     * @li @c data the context data given with this configuration.
     * @li @c handle the handle used to push data.
     * @li @c output the resulting digest (hash). It is binary and can
     *     be encoded with tools such as
     *     sol_buffer_append_as_base16(). The blob is valid while the
     *     callback happens, if one wants it to live further increase
     *     its reference with sol_blob_ref().
     *
     * @note it is safe to delete the message digest handle from
     *       inside this callback.
     */
    void (*on_digest_ready)(void *data, struct sol_message_digest *handle, struct sol_blob *output);
    /**
     * The optional callback function to report digest consumed given data.
     *
     * This pointer may be @c NULL.
     *
     * This function may be used to report progress of the whole
     * process or to control a possibly lengthy and costly feed
     * pipeline, this way the pipeline will only feed data once its
     * needed.
     *
     * The parameters are:
     *
     * @li @c data the context data given with this configuration.
     * @li @c handle the handle used to push data.
     * @li @c input the input data originally pushed/fed. There is no
     *     need to sol_blob_unref() it is done automatically.
     * @li @c status 0 on success -errno on error.
     *
     * @note it is safe to delete the message digest handle from
     *       inside this callback.
     */
    void (*on_feed_done)(void *data, struct sol_message_digest *handle, struct sol_blob *input, int status);
    /**
     * The context data to give to all callbacks.
     */
    const void *data;

    /**
     * The feed buffer max size. The value @c 0 means unlimited data.
     * Since sol_message_digest_feed() works with blobs, no extra buffers will be allocated in order
     * to store @c feed_size bytes. All the blobs that are schedule to be written will be referenced
     * and the sum of all queued blobs must not be equal or exceed @c feed_size.
     * If it happens sol_message_digest_feed() will return @c -ENOSPC and one must start to control the
     * writing flow until @c on_feed_done is called.
     * @see sol_message_digest_feed()
     */
    size_t feed_size;
} sol_message_digest_config;

/**
 * Create a new handle to feed the message to digest.
 *
 * @param config the configuration (algorithm, callbacks) to use.
 *
 * @return a newly allocated handle on success or NULL on failure and
 *         errno is set. For instance if the algorithm is not
 *         supported, @c ENOTSUP if the algorithm is not supported.
 *
 * @see sol_message_digest_del()
 * @see sol_message_digest_feed()
 */
struct sol_message_digest *sol_message_digest_new(const struct sol_message_digest_config *config);

/**
 * Delete a message digest handle.
 *
 * @param handle the handle previously created with
 *        sol_message_digest_new().
 */
void sol_message_digest_del(struct sol_message_digest *handle);

/**
 * Feed message (data) to be digested (hashed).
 *
 * This is the core of the message digest as it will take chunks of
 * data (message) to process and then produce the final hash (digest)
 * at the end.
 *
 * The message digest implementation is asynchronous to allow to
 * offload computation to another unit such as hardware acceleration
 * or a thread, not blocking the main thread while it happens. Thus
 * the lifetime of input and output data must be clear, and struct
 * sol_blob is used to manage that.
 *
 * After a chunk is fed with this function, it is queued for
 * processing. Once that chunk is done, @c on_feed_done() is called
 * with that information. This may be used to feed more data.
 *
 * Once the last chunk is fed (@c is_last=true ), then the final
 * digest is calculated and delivered by calling @c on_digest_ready()
 * function provided via @c sol_message_digest_config.
 *
 * @param handle the handle previously created with
 *        sol_message_digest_new().
 * @param input the input data to be consumed. The blob will be
 *        referenced until data is processed and automatically
 *        unreferenced after the user callback
 *        @c config->on_feed_done() returns.
 * @param is_last indicates whenever this is the last input data chunk
 *        to be processed. Some algorithms operates on block size and
 *        must use padding if there is remaining data that is not of
 *        that size. After the last blob is processed,
 *        @c config->on_digest_ready() is called with the resulting digest
 *        as a blob.
 *
 * @return 0 on success, @c -ENOSPC if sol_message_digest_config::feed_size is not zero and there's
 * no more space left or -errno on error. If error or -ENOPSC, If error, then the input reference is not taken.
 * @note Message digest follows the Soletta stream design pattern, which can be found here: @ref streams
 */
int sol_message_digest_feed(struct sol_message_digest *handle, struct sol_blob *input, bool is_last);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

