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

#include <errno.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "sol-message-digest-common.h"
#include "sol-crypto.h"
#include "sol-util.h"

static bool did_openssl_load_digests = false;

int
sol_message_digest_init(void)
{
    return sol_message_digest_common_init();
}

void
sol_message_digest_shutdown(void)
{
    sol_message_digest_common_shutdown();
}

static int
_sol_message_digest_evp_init(struct sol_message_digest *handle, const EVP_MD *md, const struct sol_str_slice key)
{
    EVP_MD_CTX *ctx = sol_message_digest_common_get_context(handle);

    if (EVP_DigestInit(ctx, md))
        return 0;

    return -EINVAL;
}

static void
_sol_message_digest_evp_cleanup(struct sol_message_digest *handle)
{
    EVP_MD_CTX *ctx = sol_message_digest_common_get_context(handle);

    EVP_MD_CTX_cleanup(ctx);
}

static ssize_t
_sol_message_digest_evp_feed(struct sol_message_digest *handle, const void *mem, size_t len, bool is_last)
{
    EVP_MD_CTX *ctx = sol_message_digest_common_get_context(handle);

    if (EVP_DigestUpdate(ctx, mem, len))
        return len;

    return -EIO;
}

static ssize_t
_sol_message_digest_evp_read_digest(struct sol_message_digest *handle, void *mem, size_t len)
{
    EVP_MD_CTX *ctx = sol_message_digest_common_get_context(handle);
    unsigned int rlen = len;

    if (EVP_DigestFinal_ex(ctx, mem, &rlen)) {
        if (rlen != len)
            SOL_WRN("Wanted %zd digest bytes, got %u", len, rlen);
        return rlen;
    }

    return -EIO;
}

static const struct sol_message_digest_common_ops _sol_message_digest_evp_ops = {
    .feed = _sol_message_digest_evp_feed,
    .read_digest = _sol_message_digest_evp_read_digest,
    .cleanup = _sol_message_digest_evp_cleanup
};

static int
_sol_message_digest_hmac_init(struct sol_message_digest *handle, const EVP_MD *md, const struct sol_str_slice key)
{
    HMAC_CTX *ctx = sol_message_digest_common_get_context(handle);

    if (HMAC_Init(ctx, key.data, key.len, md))
        return 0;

    return -EINVAL;
}

static void
_sol_message_digest_hmac_cleanup(struct sol_message_digest *handle)
{
    HMAC_CTX *ctx = sol_message_digest_common_get_context(handle);

    HMAC_CTX_cleanup(ctx);
}

static ssize_t
_sol_message_digest_hmac_feed(struct sol_message_digest *handle, const void *mem, size_t len, bool is_last)
{
    HMAC_CTX *ctx = sol_message_digest_common_get_context(handle);

    if (HMAC_Update(ctx, mem, len))
        return len;

    return -EIO;
}

static ssize_t
_sol_message_digest_hmac_read_digest(struct sol_message_digest *handle, void *mem, size_t len)
{
    HMAC_CTX *ctx = sol_message_digest_common_get_context(handle);
    unsigned int rlen = len;

    if (HMAC_Final(ctx, mem, &rlen)) {
        if (rlen != len)
            SOL_WRN("Wanted %zd digest bytes, got %u", len, rlen);
        return rlen;
    }

    return -EIO;
}

static const struct sol_message_digest_common_ops _sol_message_digest_hmac_ops = {
    .feed = _sol_message_digest_hmac_feed,
    .read_digest = _sol_message_digest_hmac_read_digest,
    .cleanup = _sol_message_digest_hmac_cleanup
};

SOL_API struct sol_message_digest *
sol_message_digest_new(const struct sol_message_digest_config *config)
{
    int (*init_fn)(struct sol_message_digest *, const EVP_MD *, const struct sol_str_slice);
    struct sol_message_digest_common_new_params params;
    const EVP_MD *md;
    struct sol_message_digest *handle;
    int errno_bkp;

    errno = EINVAL;
    SOL_NULL_CHECK(config, NULL);
    SOL_NULL_CHECK(config->on_digest_ready, NULL);
    SOL_NULL_CHECK(config->algorithm, NULL);

#ifndef SOL_NO_API_VERSION
    if (config->api_version != SOL_MESSAGE_DIGEST_CONFIG_API_VERSION) {
        SOL_WRN("sol_message_digest_config->api_version=%hu, "
            "expected version is %hu.",
            config->api_version, SOL_MESSAGE_DIGEST_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    if (!did_openssl_load_digests) {
        OpenSSL_add_all_digests();
        did_openssl_load_digests = true;
    }

    params.config = config;
    params.ops = NULL;
    params.context_template = NULL;

    md = EVP_get_digestbyname(config->algorithm);
    if (md) {
        init_fn = _sol_message_digest_evp_init;
        params.ops = &_sol_message_digest_evp_ops;
        params.context_size = sizeof(EVP_MD_CTX);
        SOL_DBG("using evp, md=%p, algorithm=\"%s\"", md, config->algorithm);
    } else if (streqn(config->algorithm, "hmac(", strlen("hmac("))) {
        const char *p = config->algorithm + strlen("hmac(");
        size_t len = strlen(p);
        if (len > 1 && p[len - 1] == ')') {
            char *mdname = strndupa(p, len - 1);
            md = EVP_get_digestbyname(mdname);
            if (!md) {
                SOL_WRN("failed to get digest algorithm \"%s\" for \"%s\".",
                    mdname, config->algorithm);
                return NULL;
            }
            init_fn = _sol_message_digest_hmac_init;
            params.ops = &_sol_message_digest_hmac_ops;
            params.context_size = sizeof(HMAC_CTX);
            SOL_DBG("using hmac, md=%p, algorithm=\"%s\"", md, mdname);
        }
    }

    if (!params.ops) {
        SOL_WRN("failed to get digest algorithm \"%s\".", config->algorithm);
        return NULL;
    }

    params.digest_size = EVP_MD_size(md);

    handle = sol_message_digest_common_new(params);
    SOL_NULL_CHECK(handle, NULL);

    errno = init_fn(handle, md, config->key);
    if (errno)
        goto error;

    return handle;

error:
    errno_bkp = errno;
    sol_message_digest_del(handle);
    errno = errno_bkp;
    return NULL;
}
