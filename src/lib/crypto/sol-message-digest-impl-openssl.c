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

#include <errno.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "sol-message-digest-common.h"
#include "sol-crypto.h"
#include "sol-util-internal.h"

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

    if (EVP_DigestInit_ex(ctx, md, NULL))
        return 0;

    return -EINVAL;
}

static void
_sol_message_digest_evp_reset(struct sol_message_digest *handle)
{
    EVP_MD_CTX *ctx = sol_message_digest_common_get_context(handle);

    EVP_MD_CTX_reset(ctx);
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
    .cleanup = _sol_message_digest_evp_reset
};

static int
_sol_message_digest_hmac_init(struct sol_message_digest *handle, const EVP_MD *md, const struct sol_str_slice key)
{
    HMAC_CTX *ctx = HMAC_CTX_new();

    if (!ctx)
        return -ENOMEM;

    if (HMAC_Init_ex(ctx, key.data, key.len, md, NULL))
        return 0;

    return -EINVAL;
}

static void
_sol_message_digest_hmac_reset(struct sol_message_digest *handle)
{
    HMAC_CTX *ctx = sol_message_digest_common_get_context(handle);

    HMAC_CTX_reset(ctx);
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
    .cleanup = _sol_message_digest_hmac_reset
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
        SOL_WRN("sol_message_digest_config->api_version=%" PRIu16 ", "
            "expected version is %" PRIu16 ".",
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

    md = EVP_get_digestbyname(config->algorithm);
    if (md) {
        params.context_handle = EVP_MD_CTX_new();
        params.context_free = (void (*)(void *))EVP_MD_CTX_free;
        init_fn = _sol_message_digest_evp_init;
        params.ops = &_sol_message_digest_evp_ops;
        SOL_DBG("using evp, md=%p, algorithm=\"%s\"", md, config->algorithm);
    } else if (streqn(config->algorithm, "hmac(", strlen("hmac("))) {
        const char *p = config->algorithm + strlen("hmac(");
        size_t len = strlen(p);
        params.context_handle = HMAC_CTX_new();
        params.context_free = (void (*)(void *))HMAC_CTX_free;
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
