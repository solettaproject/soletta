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

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct unix_socket;

struct unix_socket *unix_socket_server_new(const void *data, const char *socket_path, void (*data_read_cb)(void *data, int fd));
struct unix_socket *unix_socket_client_new(const void *data, const char *socket_path, void (*data_read_cb)(void *data, int fd));
int unix_socket_write(struct unix_socket *un_socket, const void *data, size_t count);
void unix_socket_del(struct unix_socket *un_socket);


#ifdef __cplusplus
}
#endif

