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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "message-digest");

#include "sol-crypto.h"
#include "sol-message-digest.h"

int
sol_message_digest_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    return 0;
}

void
sol_message_digest_shutdown(void)
{
}

SOL_API struct sol_message_digest *
sol_message_digest_new(const struct sol_message_digest_config *config)
{
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

    SOL_WRN("not implemented.");
    errno = ENOTSUP;
    return NULL;
}

SOL_API void
sol_message_digest_del(struct sol_message_digest *handle)
{
    SOL_NULL_CHECK(handle);
    SOL_WRN("not implemented.");
    errno = ENOTSUP;
}

SOL_API int
sol_message_digest_feed(struct sol_message_digest *handle, struct sol_blob *input, bool is_last)
{
    SOL_NULL_CHECK(handle, -EINVAL);
    SOL_NULL_CHECK(input, -EINVAL);
    SOL_WRN("not implemented.");
    return -ENOTSUP;
}
