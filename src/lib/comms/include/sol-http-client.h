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

#include <sol-buffer.h>
#include <sol-vector.h>

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
 * @defgroup HTTP
 * @ingroup Comms
 *
 * @{
 */

enum sol_http_method {
    SOL_HTTP_METHOD_GET,
    SOL_HTTP_METHOD_POST,
    SOL_HTTP_METHOD_HEAD,
};

enum sol_http_param_type {
    SOL_HTTP_PARAM_QUERY_PARAM,
    SOL_HTTP_PARAM_COOKIE,
    SOL_HTTP_PARAM_POST_FIELD,
    SOL_HTTP_PARAM_HEADER,
    SOL_HTTP_PARAM_AUTH_BASIC,
    SOL_HTTP_PARAM_ALLOW_REDIR,
    SOL_HTTP_PARAM_TIMEOUT,
    SOL_HTTP_PARAM_VERBOSE
};

struct sol_http_param {
#define SOL_HTTP_PARAM_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved;

    struct sol_vector params;
};

struct sol_http_param_value {
#define SOL_HTTP_PARAM_VALUE_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved;

    enum sol_http_param_type type;
    union {
        struct {
            char *key;
            char *value;
        } key_value;
        struct {
            char *user;
            char *password;
        } auth;
        struct {
            bool value;
        } boolean;
        struct {
            int value;
        } integer;
    } value;
};

struct sol_http_response {
#define SOL_HTTP_RESPONSE_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved;

    const char *content_type;
    const char *url;
    struct sol_buffer content;
    int response_code;
};

#define SOL_HTTP_REQUEST_PARAM_KEY_VALUE(type_, key_, value_) \
    (struct sol_http_param_value) { \
        .type = type_, \
        .api_version = SOL_HTTP_PARAM_API_VERSION, \
        .value.key_value = { \
            .key = (char *)(key_), \
            .value = (char *)(value_) \
        } \
    }

#define SOL_HTTP_REQUEST_PARAM_BOOLEAN(type_, setting_) \
    (struct sol_http_param_value) { \
        .type = type_, \
        .api_version = SOL_HTTP_PARAM_API_VERSION, \
        .value.boolean.value = (setting_) \
    }

#define SOL_HTTP_REQUEST_PARAM_COOKIE(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_COOKIE, key_, value_)

#define SOL_HTTP_REQUEST_PARAM_HEADER(header_, content_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_HEADER, header_, content_)

#define SOL_HTTP_REQUEST_PARAM_AUTH_BASIC(username_, password_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_AUTH_BASIC, \
        .api_version = SOL_HTTP_PARAM_API_VERSION, \
        .value.auth = { \
            .user = (char *)(username_), \
            .password = (char *)(password_) \
        } \
    }

#define SOL_HTTP_REQUEST_PARAM_QUERY(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_QUERY_PARAM, key_, value_)

#define SOL_HTTP_REQUEST_PARAM_POST_FIELD(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_POST_FIELD, key_, value_)

#define SOL_HTTP_REQUEST_PARAM_ALLOW_REDIR(setting_) \
    SOL_HTTP_REQUEST_PARAM_BOOLEAN(SOL_HTTP_PARAM_ALLOW_REDIR, setting_)

#define SOL_HTTP_REQUEST_PARAM_VERBOSE(setting_) \
    SOL_HTTP_REQUEST_PARAM_BOOLEAN(SOL_HTTP_PARAM_VERBOSE, setting_)

#define SOL_HTTP_REQUEST_PARAM_TIMEOUT(setting_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_TIMEOUT, \
        .api_version = SOL_HTTP_PARAM_API_VERSION, \
        .value.integer.value = (setting_) \
    }

bool sol_http_init(void) SOL_ATTR_WARN_UNUSED_RESULT;
bool sol_http_shutdown(void);

static inline void
sol_http_param_init(struct sol_http_param *params)
{
    *params = (struct sol_http_param) {
        .api_version = SOL_HTTP_PARAM_API_VERSION,
        .params = (struct sol_vector)SOL_VECTOR_INIT(struct sol_http_param_value)
    };
}

bool sol_http_param_add(struct sol_http_param *params,
    struct sol_http_param_value value) SOL_ATTR_WARN_UNUSED_RESULT;
void sol_http_param_free(struct sol_http_param *params);

int sol_http_client_request(enum sol_http_method method,
    const char *base_uri, const struct sol_http_param *params,
    void (*cb)(void *data, struct sol_http_response *response),
    void *data) SOL_ATTR_NONNULL(2, 3, 4) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
