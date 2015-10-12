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
#include <stdint.h>
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
 * @{
 */

struct sol_message_digest;

/**
 * The message digest configuration to use when creating a new handle.
 *
 * @see sol_message_digest_new()
 */
struct sol_message_digest_config {
#define SOL_MESSAGE_DIGEST_CONFIG_API_VERSION (1)
    /**
     * api_version must match SOL_MESSAGE_DIGEST_CONFIG_API_VERSION
     * at runtime.
     */
    uint16_t api_version;
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
     *
     * @note it is safe to delete the message digest handle from
     *       inside this callback.
     */
    void (*on_feed_done)(void *data, struct sol_message_digest *handle, struct sol_blob *input);
    /**
     * The context data to give to all callbacks.
     */
    const void *data;
};

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
 * digest is calculated and delivered by calling @on_digest_ready()
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
 * @return 0 on success, -errno otherwise. If error, then the input reference
 *         is not taken.
 */
int sol_message_digest_feed(struct sol_message_digest *handle, struct sol_blob *input, bool is_last);

#ifdef __cplusplus
}
#endif

