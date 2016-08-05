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

#include <sol-http.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief HTTP client
 *
 * API to perform HTTP(s) requests. It will buffer whole responses in
 * memory, so it's more suitable to perform remote API calls than file
 * transfers.
 */

/**
 * @defgroup HTTP_CLIENT HTTP Client
 * @ingroup HTTP
 *
 * @brief API to perform HTTP(s) requests.
 *
 * It will buffer whole responses in memory, so it's more suitable to
 * perform remote API calls than file transfers.
 *
 * @{
 */


/**
 * @typedef sol_http_client_connection
 * @brief Opaque handler for an HTTP client connection.
 *
 * It's created when a request is made with
 * sol_http_client_request() or
 * sol_http_client_request_with_interface().
 *
 * A connection may be canceled with sol_http_client_connection_cancel().
 */
struct sol_http_client_connection;
typedef struct sol_http_client_connection sol_http_client_connection;

/**
 * @brief The HTTP request interface to use when creating a new request.
 *
 * It allows one to have more control over the request, notifying when
 * data comes or when data should be sent.
 *
 * @see sol_http_client_request_with_interface()
 * @note HTTP client follows the Soletta stream design pattern, which can be found here: @ref streams
 */
typedef struct sol_http_request_interface {
#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_REQUEST_INTERFACE_API_VERSION (1)
    /**
     * api_version must match SOL_HTTP_REQUEST_INTERFACE_API_VERSION
     * at runtime.
     */
    uint16_t api_version;
#endif
    /**
     * This callback is called whenever data comes. The number of bytes consumed (the value
     * returned by this callback) will be removed from buffer.
     *
     * It should return the number of bytes taken care of in case of success, any negative
     * value will abort the trasnfer.
     *
     * The parameters are:
     *
     * @li @c user_data the context data given in @c sol_http_client_request_with_interface
     * @li @c connection the connection returned in @c sol_http_client_request_with_interface
     * @li @c buffer the data received
     *
     * @note it is allowed to cancel the connection handle from
     *       inside this callback.
     */
    ssize_t (*on_data)(void *user_data, struct sol_http_client_connection *connection,
        const struct sol_buffer *buffer);
    /**
     * This callback is called data should be written, it's commonly used for @c POST.
     * When it's used, it's @b MANDATORY either the header @c Content-Length with the correct
     * size or the header @c Transfer-Encoding with the value @b chunked on
     * @c sol_http_client_request_with_interface.
     *
     * It should return the number of bytes written into buffer on success, any negative
     * value will abort the trasnfer.
     *
     *
     * The parameters are:
     *
     * @li @c user_data the context data given in @c sol_http_client_request_with_interface
     * @li @c connection the connection returned in @c sol_http_client_request_with_interface
     * @li @c buffer the buffer where the data should be written, the buffer's capacity indicates
     * the amount of data that should be set.
     *
     * @note it is allowed to cancel the connection handle from
     *       inside this callback.
     */
    ssize_t (*on_send)(void *user_data, struct sol_http_client_connection *connection,
        struct sol_buffer *buffer);
    /**
     * This callback is called when the request finishes, the result of request is available on
     * @c response. @see sol_http_response
     *
     * @li @c user_data the context data given in @c sol_http_client_request_with_interface
     * @li @c connection the connection returned in @c sol_http_client_request_with_interface
     * @li @c response the result of the request
     *
     * @note it is allowed to cancel the connection handle from
     *       inside this callback.
     */
    void (*on_response)(void *user_data, struct sol_http_client_connection *connection,
        struct sol_http_response *response);

    /**
     * The size in bytes of the receiving data buffer. @c 0 means unlimited buffer size (It will always grow).
     *
     * @see sol_http_request_interface::on_data
     */
    size_t data_buffer_size;
} sol_http_request_interface;

/**
 * @brief Create a request for the specified URL using the given method. The result of
 * the request is obtained in @c cb.
 *
 * One should check the response code on @c sol_http_response to check
 * if the request returned success or some error. @see sol_http_status_code.
 *
 * @code{.c}
 *
 * static void
 * response_cb(void *userdata, struct sol_http_client_connection *connection,
 *     struct sol_http_response *response)
 * {
 *     uint16_t idx;
 *     struct sol_http_param_value *value;
 *
 *     // Check if the response is correct
 *     if (response->response_code != SOL_HTTP_STATUS_OK) {
 *         fprintf(stderr, "Finished with error, response code: %d\n",
 *             response->response_code);
 *         return;
 *     }
 *
 *     // Printing the response parameters ...
 *     SOL_HTTP_PARAMS_FOREACH_IDX (&response->param, value, idx) {
 *         switch (value->type) {
 *         case SOL_HTTP_PARAM_COOKIE:
 *             printf("[COOKIE] %.*s : %.*s\n",
 *                 SOL_STR_SLICE_PRINT(value->value.key_value.key),
 *                 SOL_STR_SLICE_PRINT(value->value.key_value.value));
 *             break;
 *         case SOL_HTTP_PARAM_HEADER:
 *             printf("[HEADER] %.*s : %.*s\n",
 *                 SOL_STR_SLICE_PRINT(value->value.key_value.key),
 *                 SOL_STR_SLICE_PRINT(value->value.key_value.value));
 *         default:
 *             break;
 *         }
 *     }
 *
 *     // Now let's show the response contents
 *     printf("%.*s\n", (int)response->content.used, (char *)response->content.data);
 * }
 *
 * int main(int argc, char *argv[]) {
 *     int r;
 *     struct sol_http_params params = SOL_HTTP_REQUEST_PARAMS_INIT;
 *     struct sol_http_client_connection *pending;
 *
 *     // Some code .....
 *
 *     // Allows redirect
 *     r = sol_http_params_add(&params,
 *         SOL_HTTP_REQUEST_PARAM_ALLOW_REDIR(true));
 *     SOL_INT_CHECK(r, < 0, r);
 *     pending = sol_http_client_request(SOL_HTTP_METHOD_GET,
 *          "http://www.github.com/solettaproject/soletta", &params, response_cb, NULL);
 *     if (!pending)
 *         fprintf(stderr, "ERROR: Failed to create the request\n");
 *    sol_http_params_clear(&params);
 *
 *    // more code.....
 *
 *    return 0;
 * }
 * @endcode
 *
 * @param method a valid HTTP method, e. g. SOL_HTTP_METHOD_GET or SOL_HTTP_METHOD_POST
 * @param url a string containing a valid URL.
 * @param params the parameters used on this request, e. g. headers,
 *        cookies and post fields.
 * @param cb callback that will be called with the response for this request.
 * @param data user data given as parameter on @c cb
 *
 * @return a pending connection on success, @c NULL on error.
 *
 * @see sol_http_client_connection_cancel
 */
struct sol_http_client_connection *sol_http_client_request(enum sol_http_method method,
    const char *url, const struct sol_http_params *params,
    void (*cb)(void *data, struct sol_http_client_connection *connection,
    struct sol_http_response *response),
    const void *data)
#ifndef DOXYGEN_RUN
SOL_ATTR_NON_NULL(2, 4) SOL_ATTR_WARN_UNUSED_RESULT
#endif
;

/**
 * @brief Create a request for the specified URL using the given method. The result of
 * the request is obtained in @c cb.
 *
 * One should check the response code on @c sol_http_response to check
 * if the request returned success or some error. @see sol_http_status_code.
 *
 * @param method a valid HTTP method, e. g. SOL_HTTP_METHOD_GET or SOL_HTTP_METHOD_POST
 * @param url a string containing a valid URL.
 * @param params the parameters used on this request, e. g. headers,
 *        cookies and post fields.
 * @param interface the interface with the callbacks used in the request. @see sol_http_request_interface
 * @param data user data given as parameter on @c cb
 *
 * @return a pending connection on success, @c NULL on error.
 *
 * @see sol_http_client_connection_cancel
 * @see sol_http_client_request
 */
struct sol_http_client_connection *sol_http_client_request_with_interface(enum sol_http_method method,
    const char *url, const struct sol_http_params *params,
    const struct sol_http_request_interface *interface,
    const void *data)
#ifndef DOXYGEN_RUN
SOL_ATTR_NON_NULL(2, 4) SOL_ATTR_WARN_UNUSED_RESULT
#endif
;

/**
 * @brief Cancel a pending request and release its resources.
 *
 * @param pending the object previously created with
 *        sol_http_client_request().
 */
void sol_http_client_connection_cancel(struct sol_http_client_connection *pending);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
