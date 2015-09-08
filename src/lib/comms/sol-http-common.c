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

#include "sol-http.h"
#include "sol-log.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_API bool
sol_http_param_add(struct sol_http_param *params,
    struct sol_http_param_value value)
{
    struct sol_http_param_value *ptr;

    SOL_NULL_CHECK(params, -EINVAL);

    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return false;
    }

    ptr = sol_vector_append(&params->params);
    if (!ptr) {
        SOL_WRN("Could not append option to parameter vector");
        return false;
    }

    memcpy(ptr, &value, sizeof(value));
    return true;
}

SOL_API void
sol_http_param_free(struct sol_http_param *params)
{
    SOL_NULL_CHECK(params);

    if (params->api_version != SOL_HTTP_PARAM_API_VERSION) {
        SOL_ERR("API version mistmatch; expected %u, got %u",
            SOL_HTTP_PARAM_API_VERSION, params->api_version);
        return;
    }
    sol_vector_clear(&params->params);
}
