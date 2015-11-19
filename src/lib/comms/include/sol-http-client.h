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
 * @defgroup HTTP_CLIENT HTTP Client
 * @ingroup HTTP
 *
 * @{
 */


struct sol_http_client_connection;

/**
 * @brief The http request interface to use when creating a new request.
 *
 * It allows one have more control over the request, notifying when
 * data comes or when data should be sent.
 *
 * @see sol_http_client_request_with_interface()
 */
struct sol_http_request_interface {
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
     * @li @c userdata the context data given in @c sol_http_client_request_with_interface
     * @li @c connection the connection returned in @c sol_http_client_request_with_interface
     * @li @c buffer the data received
     *
     * @note it is @b NOT @b ALLOWED to cancel the connection handle from
     *       inside this callback.
     */
    ssize_t (*recv_cb)(void *userdata, const struct sol_http_client_connection *connection,
        struct sol_buffer *buffer);
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
     * @li @c userdata the context data given in @c sol_http_client_request_with_interface
     * @li @c connection the connection returned in @c sol_http_client_request_with_interface
     * @li @c buffer the buffer where the data should be written, the buffer's capacity indicates
     * the amount of data that should be set.
     *
     * @note it is @b NOT @b ALLOWED to cancel the connection handle from
     *       inside this callback.
     */
    ssize_t (*send_cb)(void *userdata, const struct sol_http_client_connection *connection,
        struct sol_buffer *buffer);
    /**
     * This callback is called when the request finishes, the result of request is available on
     * @c response. @see sol_http_response
     *
     * @li @c userdata the context data given in @c sol_http_client_request_with_interface
     * @li @c connection the connection returned in @c sol_http_client_request_with_interface
     * @li @c response the result of the request
     *
     * @note it is @b NOT @b ALLOWED to cancel the connection handle from
     *       inside this callback.
     */
    void (*response_cb)(void *userdata, const struct sol_http_client_connection *connection,
        struct sol_http_response *response);
};

/**
 * @brief Create a request for the specified url using the given method. The result of
 * the request is obtained in @c cb.
 *
 * One should check the response code on @c sol_http_response to check
 * if the request returned success or with some error. @see sol_http_status_code.
 *
 * @note It should never be called from response callback given in
 *       sol_http_client_request().
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
    void (*cb)(void *data, const struct sol_http_client_connection *connection,
    struct sol_http_response *response),
    const void *data) SOL_ATTR_NONNULL(2, 4) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * @brief Create a request for the specified url using the given method. The result of
 * the request is obtained in @c cb.
 *
 * One should check the response code on @c sol_http_response to check
 * if the request returned success or with some error. @see sol_http_status_code.
 *
 * @note It should never be called from response callback given in
 *       sol_http_client_request().
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
    const void *data) SOL_ATTR_NONNULL(2, 4) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * Cancel a pending request and release its resources.
 *
 * @note It should never be called from response callback given in
 *       sol_http_client_request().
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
