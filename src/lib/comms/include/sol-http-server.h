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
 * @defgroup HTTP
 * @ingroup Comms
 *
 * @{
 */

struct sol_http_server;
struct sol_http_request;

struct sol_http_server *sol_http_server_new(uint16_t port);
void sol_http_server_del(struct sol_http_server *server);
int sol_http_server_register_handler(struct sol_http_server *server, const char *path,
    int (*request_cb)(void *data, struct sol_http_request *request),
    const void *data);
int sol_http_server_unregister_handler(struct sol_http_server *server, const char *path);

/* The http server will look first for a handler when a request come,
 * if any valid handler is found it will try to find the file in the
 * root dirs set. The response will be sent as soon as a file matches
 * with the request.
 */
int sol_http_server_add_dir(struct sol_http_server *server, const char *rootdir);
int sol_http_server_remove_dir(struct sol_http_server *server, const char *rootdir);
int sol_http_server_set_last_modified(struct sol_http_server *server, const char *path, time_t modified);

int sol_http_server_send_response(struct sol_http_request *request, struct sol_http_response *response);
const char *sol_http_request_get_url(const struct sol_http_request *request);
const struct sol_http_param *sol_http_request_get_params(const struct sol_http_request *request);
enum sol_http_method sol_http_request_get_method(const struct sol_http_request *request);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
