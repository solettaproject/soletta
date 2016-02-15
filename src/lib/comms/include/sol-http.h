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

#include <sol-arena.h>
#include <sol-buffer.h>
#include <sol-macros.h>
#include <sol-str-slice.h>
#include <sol-vector.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup HTTP HTTP
 * @ingroup Comms
 *
 * @brief API to handle Hypertext Transfer Protocol (HTTP).
 *
 * @{
 */

/**
 * @brief Type of HTTP method
 *
 * One of these must be chosen to make a request, with
 * sol_http_client_request() or sol_http_client_request_with_interface()
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
 * @brief Type of HTTP parameter
 *
 * It should be used to encode and decode parameters, with
 * sol_http_encode_params() and sol_http_decode_params().
 *
 * @note SOL_HTTP_PARAM_POST_FIELD and SOL_HTTP_PARAM_POST_DATA are both
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
    SOL_HTTP_PARAM_VERBOSE,
    SOL_HTTP_PARAM_FRAGMENT /* TODO: Should we keep this as sol_http_param_value ? */
};

/**
 * @brief Status codes as defined by HTTP protocol
 *
 * Most frequently used status are supported.
 */
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

/**
 * @brief Keep vector of HTTP parameters to be sent in a request.
 *
 * It's required to make requests, with
 * sol_http_client_request() or sol_http_client_request_with_interface()
 * or to create uris, with sol_http_create_uri() and variants.
 */
struct sol_http_params {
#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_PARAM_API_VERSION (1)
    uint16_t api_version;
#endif

    struct sol_vector params; /**< vector of parameters, struct @ref sol_http_param_value */
    struct sol_arena *arena; /**< arena with copied parameter slices */
};

/**
 * @brief Used to define a HTTP parameter
 *
 * A parameter is defined by it's type
 * (one of enum @ref sol_http_param_type) and it's value.
 * It may be a key-value parameter, authentication (user-password),
 * or data.
 */
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
            struct sol_str_slice key;
            struct sol_str_slice value;
            struct sol_str_slice filename;
        } data;
    } value;
};

/**
 * @brief Handle for a HTTP response
 *
 * A response is composed by a response code, that may be one of
 * enum @ref sol_http_status_code, a vector of parameters, url,
 * definition of content type, like "text" or "application/json",
 * and the response content itself.
 */
struct sol_http_response {
#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_RESPONSE_API_VERSION (1)
    uint16_t api_version;
#endif

    const char *content_type;
    const char *url;
    struct sol_buffer content;
    struct sol_http_params param;
    int response_code;
};

/**
 * @brief Handle for a HTTP URL
 *
 * Uniform Resource Locator conforms to the following syntax:
 *
 * scheme:[//[user:password@]host[:port]][/]path[?query][#fragment]
 */
struct sol_http_url {
    struct sol_str_slice scheme;
    struct sol_str_slice user;
    struct sol_str_slice password;
    struct sol_str_slice host;
    struct sol_str_slice path;
    struct sol_str_slice query;
    struct sol_str_slice fragment;
    uint32_t port; /**< If set to 0 it'll be ignored */
};

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @ref sol_http_response
 * has the expected API version or return.
 *
 * In case it's a wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_HTTP_RESPONSE_CHECK_API_VERSION(response_, ...) \
    if (SOL_UNLIKELY((response_)->api_version != \
        SOL_HTTP_RESPONSE_API_VERSION)) { \
        SOL_ERR("Unexpected API version (response is %" PRIu16 ", expected %" PRIu16 ")", \
            (response_)->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_HTTP_RESPONSE_CHECK_API_VERSION(response_, ...)
#endif

/**
 * @brief Macro used to check if a struct @ref sol_http_response
 * is valid (different from @c NULL) and has the expected API version or return.
 *
 * In case it's @c NULL or a wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_HTTP_RESPONSE_CHECK_API(response_, ...) \
    do { \
        if (SOL_UNLIKELY(!(response_))) { \
            SOL_WRN("Error while reaching service."); \
            return __VA_ARGS__; \
        } \
        SOL_HTTP_RESPONSE_CHECK_API_VERSION((response_), __VA_ARGS__) \
    } while (0)

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @ref sol_http_response
 * has the expected API version or goto a label.
 *
 * In case it's a wrong version, it'll go to @a label.
 */
#define SOL_HTTP_RESPONSE_CHECK_API_VERSION_GOTO(response_, label) \
    if (SOL_UNLIKELY((response_)->api_version != \
        SOL_HTTP_RESPONSE_API_VERSION)) { \
        SOL_ERR("Unexpected API version (response is %" PRIu16 ", expected %" PRIu16 ")", \
            (response_)->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
        goto label; \
    }
#else
#define SOL_HTTP_RESPONSE_CHECK_API_VERSION_GOTO(response_, label)
#endif

/**
 * @brief Macro used to check if a struct @ref sol_http_response
 * is valid (different from @c NULL) and has the expected API version
 * or goto a label.
 *
 * In case it's @c NULL or a wrong version, it'll go to @a label.
 */
#define SOL_HTTP_RESPONSE_CHECK_API_GOTO(response_, label) \
    do { \
        if (SOL_UNLIKELY(!(response_))) { \
            SOL_WRN("Error while reaching service."); \
            goto label; \
        } \
        SOL_HTTP_RESPONSE_CHECK_API_VERSION_GOTO((response_), label) \
    } while (0)

/**
 * @brief Macro used to initialize a struct @ref sol_http_params with
 * empty vector.
 *
 * @code
 * struct sol_http_params params = SOL_HTTP_REQUEST_PARAMS_INIT;
 * @endcode
 *
 * @see sol_http_params_init()
 */
#define SOL_HTTP_REQUEST_PARAMS_INIT \
    (struct sol_http_params) { \
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_PARAM_API_VERSION, ) \
        .params = SOL_VECTOR_INIT(struct sol_http_param_value), \
        .arena = NULL \
    }

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @ref sol_http_params
 * has the expected API version.
 *
 * In case it's a wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_HTTP_PARAMS_CHECK_API_VERSION(params_, ...) \
    if (SOL_UNLIKELY((params_)->api_version != \
        SOL_HTTP_PARAM_API_VERSION)) { \
        SOL_ERR("Unexpected API version (response is %" PRIu16 ", expected %" PRIu16 ")", \
            (params_)->api_version, SOL_HTTP_PARAM_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_HTTP_PARAMS_CHECK_API_VERSION(params_, ...)
#endif

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @ref sol_http_params
 * has the expected API version.
 *
 * In case it's a wrong version, it'll go to @a label.
 */
#define SOL_HTTP_PARAMS_CHECK_API_VERSION_GOTO(params_, label_) \
    if (SOL_UNLIKELY((params_)->api_version != \
        SOL_HTTP_PARAM_API_VERSION)) { \
        SOL_ERR("Unexpected API version (params is %" PRIu16 ", expected %" PRIu16 ")", \
            (params_)->api_version, SOL_HTTP_PARAM_API_VERSION); \
        goto label_; \
    }
#else
#define SOL_HTTP_PARAMS_CHECK_API_VERSION_GOTO(params_, label_)
#endif

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * key-value and type passed as argument.
 */
#define SOL_HTTP_REQUEST_PARAM_KEY_VALUE(type_, key_, value_) \
    (struct sol_http_param_value) { \
        .type = type_, \
        .value = { \
            .key_value = { \
                .key = sol_str_slice_from_str((key_ ? : "")), \
                .value = sol_str_slice_from_str((value_ ? : "")) \
            } \
        } \
    }

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * a boolean and type passed as argument.
 */
#define SOL_HTTP_REQUEST_PARAM_BOOLEAN(type_, setting_) \
    (struct sol_http_param_value) { \
        .type = type_, \
        .value = { .boolean = { .value = (setting_) } } \
    }

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_COOKIE and key-value.
 */
#define SOL_HTTP_REQUEST_PARAM_COOKIE(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_COOKIE, key_, value_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_HEADER, a header and content.
 */
#define SOL_HTTP_REQUEST_PARAM_HEADER(header_, content_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_HEADER, header_, content_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_AUTH_BASIC, username and password.
 */
#define SOL_HTTP_REQUEST_PARAM_AUTH_BASIC(username_, password_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_AUTH_BASIC, \
        .value = { \
            .auth = { \
                .user = sol_str_slice_from_str((username_ ? : "")), \
                .password = sol_str_slice_from_str((password_ ? : "")) \
            } \
        } \
    }

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_QUERY_PARAM and key-value.
 */
#define SOL_HTTP_REQUEST_PARAM_QUERY(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_QUERY_PARAM, key_, value_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_POST_FIELD and key-value.
 */
#define SOL_HTTP_REQUEST_PARAM_POST_FIELD(key_, value_) \
    SOL_HTTP_REQUEST_PARAM_KEY_VALUE(SOL_HTTP_PARAM_POST_FIELD, key_, value_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_ALLOW_REDIR and @c true or @c false as value.
 */
#define SOL_HTTP_REQUEST_PARAM_ALLOW_REDIR(setting_) \
    SOL_HTTP_REQUEST_PARAM_BOOLEAN(SOL_HTTP_PARAM_ALLOW_REDIR, setting_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_VERBOSE and @c true or @c false as value.
 */
#define SOL_HTTP_REQUEST_PARAM_VERBOSE(setting_) \
    SOL_HTTP_REQUEST_PARAM_BOOLEAN(SOL_HTTP_PARAM_VERBOSE, setting_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_TIMEOUT and timeout value.
 */
#define SOL_HTTP_REQUEST_PARAM_TIMEOUT(setting_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_TIMEOUT, \
        .value = { \
            .integer = { .value = (setting_) } \
        } \
    }


/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_POST_DATA and the contents of @c filename_.
 */
#define SOL_HTTP_REQUEST_PARAM_POST_DATA_FILE(key_, filename_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_POST_DATA, \
        .value = { \
            .data = { \
                .key = sol_str_slice_from_str((key_ ? : "")), \
                .value = SOL_STR_SLICE_EMPTY, \
                .filename = sol_str_slice_from_str((filename_ ? : "")) \
            } \
        } \
    }

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_POST_DATA and the given value.
 */
#define SOL_HTTP_REQUEST_PARAM_POST_DATA_CONTENTS(key_, value_) \
    (struct sol_http_param_value) { \
        .type = SOL_HTTP_PARAM_POST_DATA, \
        .value = { \
            .data = { \
                .key = sol_str_slice_from_str((key_ ? : "")), \
                .value = (value_), \
                .filename = SOL_STR_SLICE_EMPTY, \
            } \
        } \
    }

/**
 * @brief Macro used to iterate over a vector of HTTP parameters.
 *
 * @code
 * struct sol_http_request *request = X;
 * struct sol_http_param_value *value;
 * uint16_t idx;
 *
 * SOL_HTTP_PARAMS_FOREACH_IDX (sol_http_request_get_params(request),
 *      value, idx) {
 *          // do something with value
 *      }
 * @endcode
 */
#define SOL_HTTP_PARAMS_FOREACH_IDX(param, itrvar, idx) \
    for (idx = 0; \
        param && idx < (param)->params.len && (itrvar = (struct sol_http_param_value *)sol_vector_get(&(param)->params, idx), true); \
        idx++)

/**
 * @brief Initialize HTTP parameters struct with an empty vector
 *
 * @param params Parameters handle to be initialized.
 *
 * @see SOL_HTTP_REQUEST_PARAMS_INIT
 */
static inline void
sol_http_params_init(struct sol_http_params *params)
{
    sol_vector_init(&params->params, sizeof(struct sol_http_param_value));
    params->arena = NULL;
    SOL_SET_API_VERSION(params->api_version = SOL_HTTP_PARAM_API_VERSION; )
}

/**
 * @brief Add a new parameter to HTTP parameters vector
 *
 * @param params Parameters handle.
 * @param value Parameter to be added to the vector.
 *
 * @note Difference between sol_http_param_add_copy() and sol_http_param_add()
 * is that the first will keep a copy of any string slices.
 *
 * @return @c true on success and @c false on error.
 */
bool sol_http_param_add(struct sol_http_params *params,
    struct sol_http_param_value value) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Add a new parameter to HTTP parameters vector copying strings
 *
 * @param params Parameters handle.
 * @param value Parameter to be added to the vector.
 *
 * @note Difference between sol_http_param_add_copy() and sol_http_param_add()
 * is that the first will keep a copy of any string slices.
 *
 * @return @c true on success and @c false on error.
 */
bool sol_http_param_add_copy(struct sol_http_params *params, struct sol_http_param_value value);

/**
 * @brief Clear vector of HTTP parameters
 *
 * It clear parameters vector and also any eventual copy of slices
 * done with sol_http_param_add_copy()
 *
 * @param params Parameters handle.
 */
void sol_http_params_clear(struct sol_http_params *params);

/**
 * @brief Encodes an URL string.
 *
 * If the string was not encoded this function will simple copy
 * the slices content to the buffer and set
 * @c SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED.
 *
 * @param buf The buffer that will hold the encoded string.
 * @param value A slice to be encoded.
 *
 * @note if it's required to keep or change the buffer contents
 * @ref sol_buffer_steal_or_copy() should be used.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_encode_slice(struct sol_buffer *buf, const struct sol_str_slice value);

/**
 * @brief Decodes an URL string.
 *
 * If the string was not decoded this function will simple copy
 * the slices contents to the buffer and set
 * @c SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED.
 *
 * @param buf The buffer that will hold the decoded string.
 * @param value A slice to be decoded.
 *
 * @note if it's required to keep or change the buffer contents
 * @ref sol_buffer_steal_or_copy() should be used.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_decode_slice(struct sol_buffer *buf, const struct sol_str_slice value);

/**
 * @brief Creates an URI based on struct sol_http_url and its params
 *
 * @param uri_out The created URI - it should be freed using free().
 * @param url The url parameters.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_create_uri(char **uri_out, const struct sol_http_url url, const struct sol_http_params *params);

/**
 * @brief A simpler version of sol_http_create_uri().
 *
 *
 * @param uri The created URI - it should be freed using free().
 * @param base_uri The base uri to be used.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_create_simple_uri(char **uri, const struct sol_str_slice base_uri, const struct sol_http_params *params);

/**
 * @brief Encodes http parameters of a given type.
 *
 * @param buf Where the encoded parameters will be stored as string.
 * @param type The parameter that should be encoded.
 * @param params The parameters to be encoded.
 *
 * @return 0 on success, negative number on error.
 *
 * @note The buf must be initialized in order to use this function.
 */
int sol_http_encode_params(struct sol_buffer *buf, enum sol_http_param_type type, const struct sol_http_params *params);

/**
 * @brief Decodes http parameters of a given type.
 *
 * @param params_slice Where the decoded parameters will be stored as string.
 * @param type The parameter that should be decoded.
 * @param params The parameters to be decoded.
 *
 * @return 0 on success, negative number on error.
 *
 * @note params_slice and params must be valid pointers.
 */
int sol_http_decode_params(const struct sol_str_slice params_slice, enum sol_http_param_type type, struct sol_http_params *params);

/**
 * @brief Split an URI.
 *
 * This function will receive a complete URI and split and store
 * its values in the sol_http_url struct.
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
 * @brief An wrapper of over sol_http_create_simple_uri()
 *
 * @param uri The created URI - it should be freed using free().
 * @param base_url The base uri to be used.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
static inline int
sol_http_create_simple_uri_from_str(char **uri, const char *base_url, const struct sol_http_params *params)
{
    return sol_http_create_simple_uri(uri, sol_str_slice_from_str(base_url ? base_url : ""), params);
}

/**
 * @brief Split query into parameters.
 *
 * This function will receive a query and split and store its values in the
 * @c sol_http_params struct.
 *
 * @param query A query to be splitted.
 * @param params Where the query paramters will be stored.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_split_query(const char *query, struct sol_http_params *params);

/**
 * @brief Split post/field string into parameters.
 *
 * This function will receive a string and split and store its values in the
 * @c sol_http_params.
 *
 * @param query A string to be splitted.
 * @param params Where the post/field parameters will be stored.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_split_post_field(const char *query, struct sol_http_params *params);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
