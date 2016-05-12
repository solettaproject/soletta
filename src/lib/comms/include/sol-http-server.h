/*
 * This file is part of the Soletta Project
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

#include <sol-common-buildopts.h>
#include <sol-certificate.h>
#include <sol-http.h>
#include <sol-network.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief HTTP server
 *
 * Library to make possible run an HTTP server to deliver and
 * set values from others components.
 */

/**
 * @defgroup HTTP_SERVER HTTP Server
 * @ingroup HTTP
 *
 * @brief Library to make possible run an HTTP server to deliver and
 * set values from others components.
 *
 * @{
 */

/**
 * @struct sol_http_server
 * @brief Opaque handler for a HTTP server instance.
 *
 * It's created with sol_http_server_new() and should be later
 * deleted with sol_http_server_del()
 */
struct sol_http_server;

/**
 * @struct sol_http_server_config
 * @brief
 *
 * It's created with sol_http_server_new() and should be later
 * deleted with sol_http_server_del()
 */
struct sol_http_server_config {
#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_SERVER_CONFIG_API_VERSION (1)  /**< compile time API version to be checked during runtime */
    /**
     * must match #SOL_HTTP_SERVER_CONFIG_API_VERSION at runtime.
     */
    uint16_t api_version;
#endif

    uint16_t port;
    struct {
        const struct sol_cert *cert;
        const struct sol_cert *key;
    } security;
};

/**
 * @struct sol_http_request
 * @brief Opaque handler for a request made to a HTTP server.
 *
 * Request properties can be queried with:
 * @li sol_http_request_get_url()
 * @li sol_http_request_get_params()
 * @li sol_http_request_get_method()
 * @li sol_http_request_get_interface_address()
 *
 * A response to a request can be send with sol_http_server_send_response()
 */
struct sol_http_request;

/**
 * @struct sol_http_progressive_response
 * @brief Opaque handler used for send data progressively.
 *
 * Progressive responses can be used to create server sent events.
 *
 * This response is create with sol_http_server_send_progressive_response()
 * and data can be given using sol_http_progressive_response_feed(),
 * to delete it use sol_http_progressive_response_del().
 */
struct sol_http_progressive_response;

/**
 * @brief Creates a HTTP server, binding on all interfaces
 * in the specified @a port.
 *
 * With the returned handle it's possible to register paths using
 * @c sol_http_server_register_handler()
 * and dirs with @c sol_http_server_add_dir().
 *
 * @note Only one instance of @c sol_http_server is possible per port.
 * Try to run a second instance in the
 * same port will result in fail.
 *
 * @param config The parameters to setup the server.
 *
 * @return a handle to the server on success, otherwise @c NULL is returned.
 *
 * @see sol_http_server_config
 */
struct sol_http_server *sol_http_server_new(const struct sol_http_server_config *config);

/**
 * @brief Destroy the @a server instance.
 *
 * @param server The value got with @c sol_http_server_new()
 */
void sol_http_server_del(struct sol_http_server *server);

/**
 * @brief Register a handler for a specific path.
 *
 * @param server The value got with @c sol_http_server_new()
 * @param path The path where the handler will serve, it means, when a request comes in this path the
 * callback set will be called and a response should be sent back
 * @param request_cb The callback for the request received on the specified path
 * @param data The user data passed in the callback
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_register_handler(struct sol_http_server *server, const char *path,
    int (*request_cb)(void *data, struct sol_http_request *request),
    const void *data);

/**
 * @brief Removes a handler registered with @c sol_http_server_register_handler()
 *
 * @param server The value got with @c sol_http_server_new
 * @param path The same path given on @c sol_http_server_register_handler()
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_unregister_handler(struct sol_http_server *server, const char *path);

/**
 * @brief Add a root dir where the server will look for static files to serve.
 *
 * @note: The http server will look first for a handler when a request comes,
 * if no valid handler is found it will try to find the file in the
 * root dirs set. The response will be sent as soon as a file matches
 * with the request.
 *
 * @param server The value got with @c sol_http_server_new()
 * @param basename The base path of the requests where the server will
 * look for files on @c rootdir
 * @param rootdir The dir where the server will look for static files
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_add_dir(struct sol_http_server *server, const char *basename, const char *rootdir);

/**
 * @brief Removes a dir registered with @c sol_http_server_add_dir()
 *
 * @param server The value got with @c sol_http_server_new()
 * @param basename The same basename given on @c sol_http_server_add_dir()
 * @param rootdir The same rootdir given on @c sol_http_server_add_dir()
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_remove_dir(struct sol_http_server *server, const char *basename, const char *rootdir);

#ifdef SOL_FEATURE_FILESYSTEM

/**
 * @brief Add a  page for a specific error code
 *
 * @param server The value got with @c sol_http_server_new()
 * @param error The error code which @page_path will be served
 * @param page The path to a html file
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_set_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error, const char *page);

/**
 * @brief Removes a default error page registered with
 * @c sol_http_server_add_default_error_page()
 *
 * @param server The value got with @c sol_http_server_new()
 * @param error The same error given on
 * @c sol_http_server_add_default_error_page()
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_remove_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error);

#endif /*SOL_FEATURE_FILESYSTEM*/

/**
 * @brief Set the last time the specified path had its value modified.
 *
 * It'll make the server
 * return automatically a response with the code 304 (not modified)
 * when the request contains the header
 * If-Since-Modified greater than the value given in this function.
 *
 * @note: This is specific per @a server/@a path.
 *
 * @param server The value got with @c sol_http_server_new()
 * @param path The same path given on @c sol_http_server_register_handler()
 * @param modified The time it was modified
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_http_server_set_last_modified(struct sol_http_server *server, const char *path, time_t modified);

/**
 * @brief Send the response to request given in the callback registered on
 * @c sol_http_server_register_handler().
 *
 * After this call, @a request should not be used anymore.
 *
 * @param request The request given on the callback of
 * @c sol_http_server_register_handler()
 * @param response The response for the request containing the data
 * and parameters (e.g http headers)
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_http_server_send_progressive_response()
 */
int sol_http_server_send_response(struct sol_http_request *request, struct sol_http_response *response);

/**
 * @brief Set the necessary headers to allow server sent events
 *
 * @param response The response to set the headers
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
static inline int
sol_http_response_set_sse_headers(struct sol_http_response *response)
{
    const struct { const char *k; const char *v; } *itr, headers[] = {
        { "Content-Type", "text/event-stream" },
        { "Connection", "keep-alive" },
        { "Cache-Control", "no-cache" },
        { NULL, NULL }
    };

    if (!response)
        return -EINVAL;

    for (itr = headers; itr->k != NULL; itr++) {
        if (!sol_http_param_add(&response->param,
            SOL_HTTP_REQUEST_PARAM_HEADER(itr->k, itr->v))) {
            return -ENOMEM;
        }
    }

    return 0;
}

/**
 * @brief Send the response and keep connection alive to request given in the
 * callback registered on sol_http_server_register_handler().
 *
 * After this call, the caller will be resposible to close the connection calling
 * sol_http_progressive_response_del(). @note all the necessary headers are set by this function.
 *
 * @param request The request given on the callback of
 * sol_http_server_register_handler().
 * @param response The response for the request containing the data
 * and parameters (e.g http headers)
 * @param on_close Callback called when the connection is closed.
 * @param cb_data The data pointer to be passed to @a cb.
 *
 * @return @c sol_http_progressive_response on success, @c NULL otherwise.
 *
 * @see sol_http_server_send_response()
 * @see sol_http_progressive_response_del()
 * @see sol_http_progressive_response_feed()
 */
struct sol_http_progressive_response *sol_http_server_send_progressive_response(struct sol_http_request *request,
    const struct sol_http_response *response,
    void (*on_close)(void *data, const struct sol_http_progressive_response *progressive),
    const void *cb_data);

/**
 * @brief Send data for the progressive response.
 *
 * @param progressive The progressive response created with
 * sol_http_server_send_progressive_response()
 * @param data The data to be sent.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_http_progressive_response_del()
 * @see sol_http_server_send_progressive_response()
 */
int sol_http_progressive_response_feed(struct sol_http_progressive_response *progressive,
    const struct sol_str_slice data);

/**
 * @brief Delete the progressive response.
 *
 * This function deletes the progressive response and its resources and closes
 * the connection. When the connection be closed the callback given on
 * sol_http_server_send_progressive_response() will be called.
 *
 * @param progressive The progressive response created with
 * sol_http_server_send_progressive_response()
 * @param graceful_del If @c true all pending data will be sent before
 * the connection be closed.
 *
 * @see sol_http_server_send_progressive_response()
 * @see sol_http_progressive_response_feed()
 */
void sol_http_progressive_response_del(struct sol_http_progressive_response *progressive, bool graceful_del);

/**
 * @brief Gets the URL from a given request.
 *
 * @param request The request which the URL is wanted.
 *
 * @return the URL on success, @c NULL otherwise.
 */
const char *sol_http_request_get_url(const struct sol_http_request *request);

/**
 * @brief Gets the parameters from a given request.
 *
 * @param request The request which the URL is wanted.
 *
 * @return the parameters on success, @c NULL otherwise.
 */
const struct sol_http_params *sol_http_request_get_params(const struct sol_http_request *request);

/**
 * @brief Gets the method (GET, POST, ...) from a given request.
 *
 * @param request The request which the URL is wanted.
 *
 * @return the method on success, @c SOL_HTTP_METHOD_INVALID otherwise.
 */
enum sol_http_method sol_http_request_get_method(const struct sol_http_request *request);

/**
 * @brief Gets the address of the interface that request comes.
 *
 * @param request The request.
 * @param address Will be filled with the interface address
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 * @see sol_http_request_get_client_address()
 */
int sol_http_request_get_interface_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address);

/**
 * @brief Gets the client address that made the request.
 *
 * @param request The request.
 * @param address Will be filled with the origin address.
 * @see sol_http_request_get_interface_address()
 */
int sol_http_request_get_client_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address);

/**
 * @brief Set the buffer size for a request.
 *
 * This function changes the default request's buffer size, it is
 * used to store the post data. The default value is 4096 bytes.
 *
 * @param server The handle got with @c sol_http_server_new()
 * @param buf_size The size of the buffer.
 *
 * @return 0 in success, a negative value otherwise.
 *
 * @see sol_http_server_get_buffer_size
 */
int sol_http_server_set_buffer_size(struct sol_http_server *server, size_t buf_size);

/**
 * @brief Get the request buffer's size.
 *
 * @param server The handle got with @c sol_http_server_new()
 * @param buf_size Variable to get the buffer's size.
 *
 * @return 0 in success, a negative value otherwise.
 *
 * @see sol_http_server_set_buffer_size()
 */
int sol_http_server_get_buffer_size(struct sol_http_server *server, size_t *buf_size);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
