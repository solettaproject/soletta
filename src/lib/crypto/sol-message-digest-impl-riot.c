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

#include "hashes/sha256.h"
#include "hashes/md5.h"

#include "sol-message-digest-common.h"
#include "sol-crypto.h"
#include "sol-str-table.h"

struct sol_message_digest_info {
    size_t digest_size;
    size_t context_size;
    void (*init)(struct sol_message_digest *handle);
    struct sol_message_digest_common_ops ops;
};

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

static void
_sol_message_digest_sha256_init(struct sol_message_digest *handle)
{
    sha256_context_t *ctx = sol_message_digest_common_get_context(handle);

    sha256_init(ctx);
}

static ssize_t
_sol_message_digest_sha256_feed(struct sol_message_digest *handle, const void *mem, size_t len, bool is_last)
{
    sha256_context_t *ctx = sol_message_digest_common_get_context(handle);

    sha256_update(ctx, mem, len);
    return len;
}

static ssize_t
_sol_message_digest_sha256_read_digest(struct sol_message_digest *handle, void *mem, size_t len)
{
    sha256_context_t *ctx = sol_message_digest_common_get_context(handle);

    if (len < SHA256_DIGEST_LENGTH)
        return -EINVAL;

    sha256_final(ctx, mem);
    return len;
}

static void
_sol_message_digest_sha256_cleanup(struct sol_message_digest *handle)
{
}

static const struct sol_message_digest_info _sha256_info = {
    .digest_size = SHA256_DIGEST_LENGTH,
    .context_size = sizeof(sha256_context_t),
    .init = _sol_message_digest_sha256_init,
    .ops = {
        .feed = _sol_message_digest_sha256_feed,
        .read_digest = _sol_message_digest_sha256_read_digest,
        .cleanup = _sol_message_digest_sha256_cleanup
    }
};

static void
_sol_message_digest_md5_init(struct sol_message_digest *handle)
{
    md5_ctx_t *ctx = sol_message_digest_common_get_context(handle);

    md5_init(ctx);
}

static ssize_t
_sol_message_digest_md5_feed(struct sol_message_digest *handle, const void *mem, size_t len, bool is_last)
{
    md5_ctx_t *ctx = sol_message_digest_common_get_context(handle);

    md5_update(ctx, mem, len);
    return len;
}

static ssize_t
_sol_message_digest_md5_read_digest(struct sol_message_digest *handle, void *mem, size_t len)
{
    md5_ctx_t *ctx = sol_message_digest_common_get_context(handle);

    if (len < MD5_DIGEST_LENGTH)
        return -EINVAL;

    md5_final(ctx, mem);
    return len;
}

static void
_sol_message_digest_md5_cleanup(struct sol_message_digest *handle)
{
}

static const struct sol_message_digest_info _md5_info = {
    .digest_size = MD5_DIGEST_LENGTH,
    .context_size = sizeof(md5_ctx_t),
    .init = _sol_message_digest_md5_init,
    .ops = {
        .feed = _sol_message_digest_md5_feed,
        .read_digest = _sol_message_digest_md5_read_digest,
        .cleanup = _sol_message_digest_md5_cleanup
    }
};

static const struct sol_str_table_ptr _available_digests[] = {
    SOL_STR_TABLE_PTR_ITEM("sha256", &_sha256_info),
    SOL_STR_TABLE_PTR_ITEM("md5", &_md5_info),
    { }
};

SOL_API struct sol_message_digest *
sol_message_digest_new(const struct sol_message_digest_config *config)
{
    const struct sol_message_digest_info *dinfo;
    struct sol_message_digest *handle;
    struct sol_message_digest_common_new_params params;
    struct sol_str_slice algorithm;

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

    algorithm = sol_str_slice_from_str(config->algorithm);
    if (!sol_str_table_ptr_lookup(_available_digests, algorithm, &dinfo)) {
        SOL_WRN("failed to get digest algorithm \"%s\".",
            config->algorithm);
        return NULL;
    }

    params.config = config;
    params.ops = &dinfo->ops;
    params.context_size = dinfo->context_size;
    params.digest_size = dinfo->digest_size;
    params.context_template = NULL;

    handle = sol_message_digest_common_new(params);
    SOL_NULL_CHECK(handle, NULL);

    dinfo->init(handle);

    return handle;
}
