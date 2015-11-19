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
#include <sol-network.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief HTTP server
 * Library to make possible run an HTTP server to deliver and
 * set values from others components.
 */

/**
 * @defgroup HTTP_SERVER HTTP Server
 * @ingroup HTTP
 *
 * @{
 */

struct sol_http_server;
struct sol_http_request;

/**
 * Creates a HTTP server, binding on all interfaces in the specified @p port.
 * With the returned handle it's possible to register paths using
 * @c sol_http_server_register_handler
 * and dirs with @c sol_http_server_add_dir.
 *
 * @note Only one instance of sol_http_server is possible per port.
 * Try to run a second instance in the
 * same port will result in fail.
 *
 * @param port The port where the server will bind.
 *
 * @return a handle to the server on success, otherwise @c NULL is returned.
 */
struct sol_http_server *sol_http_server_new(uint16_t port);

/**
 * Destroy the @p server instance.
 *
 * @param server The value got with @c sol_http_server_new
 */
void sol_http_server_del(struct sol_http_server *server);

/**
 * Register a handler for a specific path.
 *
 * @param server The value got with @c sol_http_server_new
 * @param path The path where the handler will serve, it means, when a request comes in this path the
 * callback set will be called and a response should be sent back
 * @param request_cb The callback for the request received on the specified path
 * @param data The user data passed in the callback
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_register_handler(struct sol_http_server *server, const char *path,
    int (*request_cb)(void *data, struct sol_http_request *request),
    const void *data);

/**
 * Removes a handler registered with @c sol_http_server_register_handler
 *
 * @param server The value got with @c sol_http_server_new
 * @param path The same path given on @c sol_http_server_register_handler
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_unregister_handler(struct sol_http_server *server, const char *path);

/**
 * Add a root dir where the server will look for static files to serve.
 *
 * @note: The http server will look first for a handler when a request comes,
 * if no valid handler is found it will try to find the file in the
 * root dirs set. The response will be sent as soon as a file matches
 * with the request.
 *
 * @param server The value got with @c sol_http_server_new
 * @param basename The base path of the requests where the server will look for files on @c rootdir
 * @param rootdir The dir where the server will look for static files
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_add_dir(struct sol_http_server *server, const char *basename, const char *rootdir);

/**
 * Removes a dir registered with @c sol_http_server_add_dir
 *
 * @param server The value got with @c sol_http_server_new
 * @param basename The same basename given on @c sol_http_server_add_dir
 * @param rootdir The same rootdir given on @c sol_http_server_add_dir
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_remove_dir(struct sol_http_server *server, const char *basename, const char *rootdir);

#ifdef FEATURE_FILESYSTEM

/**
 * Add a  page for a specific error code
 *
 * @param server The value got with @c sol_http_server_new
 * @param error The error code which @page_path will be served
 * @param page The path to a html file
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_set_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error, const char *page);

/**
 * Removes a default error page registered with @c sol_http_server_add_default_error_page
 *
 * @param server The value got with @c sol_http_server_new
 * @param error The same error given on @c sol_http_server_add_default_error_page
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_remove_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error);

#endif /*FEATURE_FILESYSTEM*/

/**
 * Set the last time the specified path had its value modified. It'll make the server
 * return automatically a response with the code 304 (not modified)
 * when the request contains the header
 * If-Since-Modified greater than the value given in this function.
 *
 * @note: This is specific per @p server/@p path.
 *
 * @param server The value got with @c sol_http_server_new
 * @param path The same path given on @c sol_http_server_register_handler
 * @param modified The time it was modified
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_set_last_modified(struct sol_http_server *server, const char *path, time_t modified);

/**
 * Send the response to request given in the callback registered on @c sol_http_server_register_handler.
 * After this call, @a request should not be used anymore.
 *
 * @param request The request given on the callback of @c sol_http_server_register_handler
 * @param response The response for the request containing the data and parameters (e.g http headers)
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_server_send_response(struct sol_http_request *request, struct sol_http_response *response);

/**
 * Gets the URL from a given request.
 *
 * @param request The request which the URL is wanted.
 *
 * @return the URL on success, @c NULL otherwise.
 */
const char *sol_http_request_get_url(const struct sol_http_request *request);

/**
 * Gets the parameters from a given request.
 *
 * @param request The request which the URL is wanted.
 *
 * @return the parameters on success, @c NULL otherwise.
 */
const struct sol_http_params *sol_http_request_get_params(const struct sol_http_request *request);

/**
 * Gets the method (GET, POST, ...) from a given request.
 *
 * @param request The request which the URL is wanted.
 *
 * @return the method on success, @c SOL_HTTP_METHOD_INVALID otherwise.
 */
enum sol_http_method sol_http_request_get_method(const struct sol_http_request *request);

/**
 * Gets the address of the interface that request comes.
 *
 * @param request The request which the URL is wanted.
 * @param address Will be filled with the interface address
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_http_request_get_interface_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
