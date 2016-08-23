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

#include "sol-coap.h"
#include "sol-lwm2m.h"
#include "sol-lwm2m-common.h"
#include "sol-lwm2m-client.h"
#include "sol-lwm2m-server.h"
#include "sol-lwm2m-bs-server.h"
#include "sol-socket-dtls.h"
#include "sol-socket.h"

enum lwm2m_entity_type {
    LWM2M_CLIENT,
    LWM2M_SERVER,
    LWM2M_BOOTSTRAP_SERVER
};

struct sol_lwm2m_security {
    struct sol_socket_dtls_credential_cb callbacks;
    enum lwm2m_entity_type type;
    /* entity MUST have one of the following types:
     * struct sol_lwm2m_client
     * struct sol_lwm2m_server
     * struct sol_lwm2m_bootstrap_server
     */
    void *entity;
};

bool
sol_lwm2m_security_supports_security_mode(struct sol_lwm2m_security *security,
    enum sol_lwm2m_security_mode sec_mode);

struct sol_lwm2m_security *sol_lwm2m_client_security_add(
    struct sol_lwm2m_client *lwm2m_client, enum sol_lwm2m_security_mode sec_mode);

void sol_lwm2m_client_security_del(struct sol_lwm2m_security *security);

struct sol_lwm2m_security *sol_lwm2m_server_security_add(
    struct sol_lwm2m_server *lwm2m_server, enum sol_lwm2m_security_mode sec_mode);

void sol_lwm2m_server_security_del(struct sol_lwm2m_security *security);

struct sol_lwm2m_security *sol_lwm2m_bootstrap_server_security_add(
    struct sol_lwm2m_bootstrap_server *lwm2m_bs_server, enum sol_lwm2m_security_mode sec_mode);

void sol_lwm2m_bootstrap_server_security_del(struct sol_lwm2m_security *security);
