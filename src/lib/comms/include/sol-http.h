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
#include <sol-str-slice.h>
#include <sol-arena.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    SOL_HTTP_METHOD_DELETE,
    SOL_HTTP_METHOD_PUT,
    SOL_HTTP_METHOD_CONNECT,
    SOL_HTTP_METHOD_OPTIONS,
    SOL_HTTP_METHOD_TRACE,
    SOL_HTTP_METHOD_PATCH,
    SOL_HTTP_METHOD_INVALID
};

/**
 * SOL_HTTP_PARAM_POST_FIELD and SOL_HTTP_PARAM_POST_DATA are both
 * used for setting the data of a POST request, but only one can
 * be used per request.
 */
enum sol_http_param_type {
    SOL_HTTP_PARAM_QUERY_PARAM,
    SOL_HTTP_PARAM_COOKIE,
    SOL_HTTP_PARAM_POST_FIELD,
    SOL_HTTP_PARAM_POST_DATA,
    SOL_HTTP_PARAM_HEADER,
    SOL_HTTP_PARAM_AUTH_BASIC,
    SOL_HTTP_PARAM_ALLOW_REDIR,
    SOL_HTTP_PARAM_TIMEOUT,
    SOL_HTTP_PARAM_VERBOSE
};

enum sol_http_status_code {
    SOL_HTTP_STATUS_OK = 200,
    SOL_HTTP_STATUS_FOUND = 302,
    SOL_HTTP_STATUS_SEE_OTHER = 303,
    SOL_HTTP_STATUS_NOT_MODIFIED = 304,
    SOL_HTTP_STATUS_BAD_REQUEST = 400,
    SOL_HTTP_STATUS_FORBIDDEN = 403,
    SOL_HTTP_STATUS_NOT_FOUND = 404,
    SOL_HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    SOL_HTTP_STATUS_NOT_IMPLEMENTED = 501
};

struct sol_http_param {
#define SOL_HTTP_PARAM_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved;

    struct sol_vector params;
    struct sol_arena *arena;
};

struct sol_http_param_value {
    enum sol_http_param_type type;
    union {
        struct {
            struct sol_str_slice key;
            struct sol_str_slice value;
        } key_value;
        struct {
            struct sol_str_slice user;
            struct sol_str_slice password;
        } auth;
        struct {
            bool value;
        } boolean;
        struct {
            int32_t value;
        } integer;
        struct {
            const struct sol_str_slice value;
        } data;
    } value;
};

struct sol_http_response {
#define SOL_HTTP_RESPONSE_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved;

    const char *content_type;
    const char *url;
    struct sol_buffer content;
    struct sol_http_param param;
    int response_code;
};

struct sol_http_url {
    struct sol_str_slice scheme;
    struct sol_str_slice user;
    struct sol_str_slice password;
    struct sol_str_slice host;
    struct sol_str_slice path;
    struct sol_str_slice query;
    struct sol_str_slice fragment;
    uint32_t port; //If == 0 ignore
};

#define SOL_HTTP_RESPONSE_CHECK_API(response_, ...)  \
    do { \
        if (unlikely(!response_)) { \
            SOL_WRN("Error while reaching service."); \
            return __VA_ARGS__; \
        } \
        if (unlikely(response_->api_version != \
            SOL_HTTP_RESPONSE_API_VERSION)) { \
            SOL_ERR("Unexpected API version (response is %u, expected %u)", \
                response->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_HTTP_RESPONSE_CHECK_API_GOTO(response_, label) \
    do { \
        if (unlikely(!response_)) { \
            SOL_WRN("Error while reaching service."); \
            goto label; \
        } \
        if (unlikely(response_->api_version != \
            SOL_HTTP_RESPONSE_API_VERSION)) { \
            SOL_ERR("Unexpected API version (response is %u, expected %u)", \
                response->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
            goto label; \
        } \
    } while (0)

#define SOL_HTTP_REQUEST_PARAM_INIT   \
    (struct sol_http_param) { \
        .api_version = SOL_HTTP_PARAM_API_VERSION, \
        .params = SOL_VECTOR_INIT(struct sol_http_param_value) \
    }

#define SOL_HTTP_REQUEST_PARAM_KEY_VALUE(type_, key_, value_) \
    (struct sol_http_param_value) { \
        .type = type_, \
        .value.key_value = { \
            .key = sol_str_slice_from_str((key_ ? : "")),        \
            .value = sol_str_slice_from_str((value_ ? : ""))        \
        } \
    }

#define SOL_HTTP_REQUEST_PARAM_BOOLEAN(type_, setting_) \
    (struct sol_http_param_value) { \
        .type = type_, \
        .value.boolean.value = (setting_) \
    }

#define SOL_HTTP_REQUEST_PARAM_COOKIE(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_COOKIE, key_, value_)

#define SOL_HTTP_REQUEST_PARAM_HEADER(header_, content_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_HEADER, header_, content_)

#define SOL_HTTP_REQUEST_PARAM_AUTH_BASIC(username_, password_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_AUTH_BASIC, \
        .value.auth = { \
            .user = sol_str_slice_from_str((username_ ? : "")),    \
            .password = sol_str_slice_from_str((password_ ? : ""))    \
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
        .value.integer.value = (setting_) \
    }

#define SOL_HTTP_REQUEST_PARAM_POST_DATA(data_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_POST_DATA, \
        .value.data.value = (data_) \
    }

#define SOL_HTTP_PARAM_FOREACH_IDX(param, itrvar, idx) \
    for (idx = 0; \
        param && idx < (param)->params.len && (itrvar = sol_vector_get(&(param)->params, idx), true); \
        idx++)

static inline void
sol_http_param_init(struct sol_http_param *params)
{
    *params = (struct sol_http_param) {
        .api_version = SOL_HTTP_PARAM_API_VERSION,
        .params = SOL_VECTOR_INIT(struct sol_http_param_value)
    };
}

bool sol_http_param_add(struct sol_http_param *params,
    struct sol_http_param_value value) SOL_ATTR_WARN_UNUSED_RESULT;
bool sol_http_param_add_copy(struct sol_http_param *params, struct sol_http_param_value value);
void sol_http_param_free(struct sol_http_param *params);

/**
 * Encodes an URL string.
 *
 * If the string was not encoded this function will simple copy
 * the slices content to the buffer and set @c SOL_BUFFER_FLAGS_MEMORY_NOT_OWNE.D
 *
 * @param buf The buffer that will hold the encoded string.
 * @param value A slice to be encoded.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_encode_slice(struct sol_buffer *buf, const struct sol_str_slice value);

/**
 * Decodes an URL string.
 *
 * If the string was not decoded this function will simple copy
 * the slices contents to the buffer and set @c SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED.
 *
 * @param buf The buffer that will hold the decoded string.
 * @param value A slice to be decoded.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_decode_slice(struct sol_buffer *buf, const struct sol_str_slice value);

/**
 *  Creates an URI based on struct sol_http_url and its params
 *
 * @param url_out The created URI - it should be freed using free().
 * @param url The url parameters.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_create_uri(char **uri_out, const struct sol_http_url url, const struct sol_http_param *params);

/**
 * A simpler version of sol_http_create_uri().
 *
 *
 * @param uri The created URI - it should be freed using free().
 * @param base_uri The base uri to be used.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_create_simple_uri(char **uri, const struct sol_str_slice base_uri, const struct sol_http_param *params);

/**
 * Encodes http parameters of a given type.
 *
 * @param buf Where the encoded parameters will be stored as string.
 * @param type The parameter that should be encoded.
 * @param params The parameters to be encoded.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_encode_params(struct sol_buffer *buf, enum sol_http_param_type type, const struct sol_http_param *params);

/**
 * Decodes http parameters of a given type.
 *
 * @param buf Where the decoded parameters will be stored as string.
 * @param type The parameter that should be decoded.
 * @param params The parameters to be decoded.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_decode_params(const struct sol_str_slice params_slice, enum sol_http_param_type type, struct sol_http_param *params);

/**
 * Split an URI.
 *
 * This function will receive a complete URI and split and store its values in the sol_http_url struct.
 *
 * @param full_uri An URI to be splitted.
 * @param url Where the URL parts will be stored.
 *
 *
 * @return 0 on success, negative number on error.
 * @note This function will not decoded the URI.
 */
int sol_http_split_uri(const struct sol_str_slice full_uri, struct sol_http_url *url);

/**
 * An wrapper of over sol_http_create_simple_uri()
 *
 * @param uri The created URI - it should be freed using free().
 * @param base_uri The base uri to be used.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
static inline int
sol_http_create_simple_uri_from_str(char **uri, const char *base_url, const struct sol_http_param *params)
{
    return sol_http_create_simple_uri(uri, sol_str_slice_from_str(base_url ? base_url : ""), params);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
