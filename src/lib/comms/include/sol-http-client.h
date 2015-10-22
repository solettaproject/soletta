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

#include <sol-http.h>
#include <sol-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief HTTP client
 * Library to perform HTTP(s) requests. Buffers whole response in memory,
 * so it's more suitable to remote API calls rather than file transfer.
 */

/**
 * @defgroup HTTP HTTP
 * @ingroup Comms
 *
 * @{
 */


struct sol_http_client_connection;

struct sol_http_client_connection *sol_http_client_request(enum sol_http_method method,
    const char *base_uri, const struct sol_http_param *params,
    void (*cb)(void *data, const struct sol_http_client_connection *connection,
    struct sol_http_response *response),
    const void *data) SOL_ATTR_NONNULL(2, 4) SOL_ATTR_WARN_UNUSED_RESULT;

struct sol_http_client_connection *sol_http_client_request_with_data(enum sol_http_method method,
    struct sol_blob *blob,
    const char *base_uri, const struct sol_http_param *params,
    void (*cb)(void *data, const struct sol_http_client_connection *connection,
    struct sol_http_response *response),
    const void *data) SOL_ATTR_NONNULL(3, 5) SOL_ATTR_WARN_UNUSED_RESULT;

void sol_http_client_connection_cancel(struct sol_http_client_connection *pending);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
