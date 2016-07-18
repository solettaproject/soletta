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
    /**
     * Requests data from a specified resource
     */
    SOL_HTTP_METHOD_GET,
    /**
     * Submits data to be processed to a specified resource
     */
    SOL_HTTP_METHOD_POST,
    /**
     * Same as GET, but transfers the status line and
     * header section only
     */
    SOL_HTTP_METHOD_HEAD,
    /**
     * Removes all current representations of the target
     * resource given by a URI
     */
    SOL_HTTP_METHOD_DELETE,
    /**
     * Replaces all current representations of the target
     * resource with the uploaded content
     */
    SOL_HTTP_METHOD_PUT,
    /**
     * Establishes a tunnel to the server identified by a given URI
     */
    SOL_HTTP_METHOD_CONNECT,
    /**
     * Describes the communication options for the target resource
     */
    SOL_HTTP_METHOD_OPTIONS,
    /**
     * Performs a message loop-back test along the path to the
     * target resource
     */
    SOL_HTTP_METHOD_TRACE,
    /**
     * Used to update partial resources
     */
    SOL_HTTP_METHOD_PATCH,
    /**
     * client makes an HTTP request by using an HTTP method
     * that does not comply with the HTTP specifications
     */
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
    /**
     * The request has been accepted for processing
     */
    SOL_HTTP_STATUS_OK = 200,
    /**
     * Provides an URL in the location header field
     */
    SOL_HTTP_STATUS_FOUND = 302,
    /**
     * The response to the request can be found
     * under another URI using a GET method
     */
    SOL_HTTP_STATUS_SEE_OTHER = 303,
    /**
     * Indicates that the resource has not been
     * modified since the version specified by the request headers
     */
    SOL_HTTP_STATUS_NOT_MODIFIED = 304,
    /**
     * The server cannot or will not process the request
     * due to an apparent client error
     */
    SOL_HTTP_STATUS_BAD_REQUEST = 400,
    /**
     * The request was a valid request, but the server
     * is refusing to respond to it
     */
    SOL_HTTP_STATUS_FORBIDDEN = 403,
    /**
     * The requested resource could not be found
     */
    SOL_HTTP_STATUS_NOT_FOUND = 404,
    /**
     * A request method is not supported for the requested resource
     */
    SOL_HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    /**
     * A generic error message
     */
    SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    /**
     * The server either does not recognize the request method,
     * or it lacks the ability to fulfill the request
     */
    SOL_HTTP_STATUS_NOT_IMPLEMENTED = 501
};

/**
 * @brief Keep vector of HTTP parameters to be sent in a request.
 *
 * It's required to make requests, with
 * sol_http_client_request() or sol_http_client_request_with_interface()
 * or to create URIs, with sol_http_create_uri() and variants.
 */
typedef struct sol_http_params {
#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_PARAM_API_VERSION (1)
    uint16_t api_version;
#endif

    struct sol_vector params; /**< vector of parameters, struct @ref sol_http_param_value */
    struct sol_arena *arena; /**< arena with copied parameter slices */
} sol_http_params;

/**
 * @brief Used to rank content type priorities.
 *
 * @see sol_http_parse_content_type_priorities()
 * @see sol_http_content_type_priorities_array_clear()
 */
struct sol_http_content_type_priority {
    struct sol_str_slice content_type; /**< The content type itself. Example: @c "text/html" */
    struct sol_str_slice type; /**< The type. Example @c "text" */
    struct sol_str_slice sub_type; /**< The sub type. Example @c "html"*/
    struct sol_vector tokens; /**< An array of @ref sol_str_slice. For
                                 example, for the content type @c
                                 "text/html;level=1;level2", this
                                 array would contain @c "level=1" and
                                 @c "level=2" */
    double qvalue; /**< The qvalue for the content type */
    uint16_t index; /**< The original index as found in the content type/accept HTTP header */
};

/**
 * @brief Used to define an HTTP parameter
 *
 * A parameter is defined by its type
 * (one of enum @ref sol_http_param_type) and its value.
 * It may be a key-value parameter, authentication (user-password),
 * or data.
 */
typedef struct sol_http_param_value {
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
} sol_http_param_value;

/**
 * @brief Handle for an HTTP response
 *
 * A response is composed by a response code, that may be one of
 * enum @ref sol_http_status_code, a vector of parameters, URL,
 * definition of content type, like "text" or "application/json",
 * and the response content itself.
 */
typedef struct sol_http_response {
#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_RESPONSE_API_VERSION (1)
    uint16_t api_version;
#endif

    const char *content_type;
    const char *url;
    struct sol_buffer content;
    struct sol_http_params param;
    int response_code;
} sol_http_response;

/**
 * @brief Handle for an HTTP URL
 *
 * Uniform Resource Locator conforms to the following syntax:
 *
 * scheme:[//[user:password@]host[:port]][/]path[?query][\c \#fragment]
 */
typedef struct sol_http_url {
    struct sol_str_slice scheme;
    struct sol_str_slice user;
    struct sol_str_slice password;
    struct sol_str_slice host;
    struct sol_str_slice path;
    struct sol_str_slice query;
    struct sol_str_slice fragment;
    uint32_t port; /**< If set to 0 it'll be ignored */
} sol_http_url;

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
#define SOL_HTTP_REQUEST_PARAM_BOOL(type_, setting_) \
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
    SOL_HTTP_REQUEST_PARAM_BOOL(SOL_HTTP_PARAM_ALLOW_REDIR, setting_)

/**
 * @brief Macro to set a struct @ref sol_http_param_value with
 * type SOL_HTTP_PARAM_VERBOSE and @c true or @c false as value.
 */
#define SOL_HTTP_REQUEST_PARAM_VERBOSE(setting_) \
    SOL_HTTP_REQUEST_PARAM_BOOL(SOL_HTTP_PARAM_VERBOSE, setting_)

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
 * @note Difference between sol_http_params_add_copy() and sol_http_params_add()
 * is that the first will keep a copy of any string slices.
 *
 * @return @c true on success and @c false on error.
 */
int sol_http_params_add(struct sol_http_params *params,
    struct sol_http_param_value value)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Add a new parameter to HTTP parameters vector copying strings
 *
 * @param params Parameters handle.
 * @param value Parameter to be added to the vector.
 *
 * @note Difference between sol_http_params_add_copy() and sol_http_params_add()
 * is that the first will keep a copy of any string slices.
 *
 * @return @c 0 on success and @c -errno on error.
 */
int sol_http_params_add_copy(struct sol_http_params *params, struct sol_http_param_value value);

/**
 * @brief Clear vector of HTTP parameters
 *
 * It clear parameters vector and also any eventual copy of slices
 * done with sol_http_params_add_copy()
 *
 * @return @c 0 on success and @c -errno on error.
 *
 * @param params Parameters handle.
 */
void sol_http_params_clear(struct sol_http_params *params);

/**
 * @brief Encodes an URL string.
 *
 * If the string was not encoded this function will simply copy the
 * slice's content to the buffer and set @c
 * SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED on the buffer's flags.
 *
 * @param buf The buffer that will hold the encoded string (it will be initialized by this function)
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
 * If the string was not decoded this function will simply copy the
 * slice's contents to the buffer and set @c
 * SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED on the buffer's flags.
 *
 * @param buf The buffer that will hold the decoded string (it will be initialized by this function)
 * @param value A slice to be decoded.
 *
 * @note if it's required to keep or change the buffer contents
 * @ref sol_buffer_steal_or_copy() should be used.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_decode_slice(struct sol_buffer *buf, const struct sol_str_slice value);

/**
 * @brief Creates an URI based on struct @ref sol_http_url and its parameters
 *
 * @param buf Where the created URI should be appended (the buffer must be already initialized)
 * @param url The URL parameters.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_create_full_uri(struct sol_buffer *buf, const struct sol_http_url url, const struct sol_http_params *params);

/**
 * @brief A simpler version of sol_http_create_full_uri().
 *
 *
 * @param buf Where the created URI should be appended (the buffer must be already initialized)
 * @param base_uri The base URI to be used.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
int sol_http_create_uri(struct sol_buffer *buf, const struct sol_str_slice base_uri, const struct sol_http_params *params);

/**
 * @brief Encodes HTTP parameters of a given type.
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
 * @brief Decodes HTTP parameters of a given type.
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
 * @brief A wrapper on top of sol_http_create_uri()
 *
 * @param buf Where the created URI should be appended (the buffer must be already initialized)
 * @param base_url The base URI to be used.
 * @param params The query and cookies params.
 *
 * @return 0 on success, negative number on error.
 */
static inline int
sol_http_create_uri_from_str(struct sol_buffer *buf, const char *base_url, const struct sol_http_params *params)
{
    return sol_http_create_uri(buf, sol_str_slice_from_str(base_url ? base_url : ""), params);
}

/**
 * @brief Split query into parameters.
 *
 * This function will receive a query and split and store its values in the
 * @c sol_http_params struct.
 *
 * @param query A query to be splitted.
 * @param params Where the query parameters will be stored.
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
 * @brief Sort the content type slice based on its priorities
 *
 * For more information about how the content type is sorted check: https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.1
 *
 * @param content_type A content type slice. Example: @c "text/html, application/json;q=0.5"
 * @param priorities An array to store the content type prioriries (it will be initialized by this function)
 * @see sol_http_content_type_priorities_array_clear()
 * @see @ref sol_http_content_type_priority
 */
int sol_http_parse_content_type_priorities(const struct sol_str_slice content_type, struct sol_vector *priorities);

/**
 * @brief Clears the priorities array
 *
 * @param priorities An array of @ref sol_http_content_type_priority.
 * @see sol_http_parse_content_type_priorities()
 */
void sol_http_content_type_priorities_array_clear(struct sol_vector *priorities);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
