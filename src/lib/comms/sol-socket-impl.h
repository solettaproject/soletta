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

#include "sol-network.h"
#include "sol-socket.h"

#ifndef SOL_NO_API_VERSION

#define SOL_SOCKET_OPTIONS_CHECK_API_VERSION(options_, ...) \
    if (SOL_UNLIKELY((options_)->api_version != \
        SOL_SOCKET_OPTIONS_API_VERSION)) { \
        SOL_ERR("Unexpected API version (socket options is %u, expected %u)", \
            (options_)->api_version, SOL_SOCKET_OPTIONS_API_VERSION); \
        errno = EINVAL; \
        return __VA_ARGS__; \
    }

#define SOL_SOCKET_OPTIONS_CHECK_SUB_API_VERSION(options, expected, ...) \
    do { \
        SOL_NULL_CHECK(options, __VA_ARGS__); \
        if (((const struct sol_socket_options *)options)->sub_api != (expected)) { \
            SOL_WRN("" # options "(%p)->sub_api(%hu) != " \
                "" # expected "(%hu)", \
                (options), \
                ((const struct sol_socket_options *)options)->sub_api, \
                (expected)); \
            errno = EINVAL; \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_SOCKET_TYPE_CHECK_API_VERSION(type_, ...) \
    if (SOL_UNLIKELY((type_)->api_version != \
        SOL_SOCKET_TYPE_API_VERSION)) { \
        SOL_ERR("Unexpected API version (socket type is %u, expected %u)", \
            (type_)->api_version, SOL_SOCKET_TYPE_API_VERSION); \
        return __VA_ARGS__; \
    }

#else

#define SOL_SOCKET_TYPE_CHECK_API_VERSION(type_, ...)
#define SOL_SOCKET_OPTIONS_CHECK_API_VERSION(options_, ...)
#define SOL_SOCKET_OPTIONS_CHECK_SUB_API_VERSION(options_, expected_, ...)

#endif

struct sol_socket *sol_socket_ip_default_new(const struct sol_socket_options *options);

#ifdef DTLS
struct sol_socket *sol_socket_default_dtls_new(const struct sol_socket_options *options);
#endif
