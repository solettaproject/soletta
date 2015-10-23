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

#include <stdbool.h>
#include <stdint.h>
#include <sol-coap.h>
#include <sol-network.h>
#include <sol-str-slice.h>
#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "sol-oic-common.h"

/**
 * @file
 * @brief Routines to create clients talking OIC protocol.
 */

/**
 * @defgroup OIC Open Interconnect Consortium
 * @ingroup Comms
 *
 * Implementation of protocol defined by Open Interconnect Consortium
 * (OIC - http://openinterconnect.org/)
 *
 * It's a common communication framework based on industry standard
 * technologies to wirelessly connect and intelligently manage
 * the flow of information among devices, regardless of form factor,
 * operating system or service provider.
 *
 * Both client and server sides are covered by this module.
 *
 * @{
 */

struct sol_oic_client {
#define SOL_OIC_CLIENT_API_VERSION (1)
    uint16_t api_version;
    int : 0; /* save possible hole for a future field */
    struct sol_coap_server *server;
    struct sol_coap_server *dtls_server;
};

struct sol_oic_resource {
#define SOL_OIC_RESOURCE_API_VERSION (1)
    uint16_t api_version;
    int : 0; /* save possible hole for a future field */
    struct sol_network_link_addr addr;
    struct sol_str_slice href;
    struct sol_str_slice device_id;
    struct sol_vector types;
    struct sol_vector interfaces;
    struct {
        struct sol_timeout *timeout;
        int clear_data;
    } observe;
    int refcnt;
    bool observable : 1;
    bool active : 1;
    bool slow : 1;
    bool secure : 1;
};

bool sol_oic_client_find_resource(struct sol_oic_client *client,
    struct sol_network_link_addr *cliaddr, const char *resource_type,
    void (*resource_found_cb)(struct sol_oic_client *cli,
    struct sol_oic_resource *res,
    void *data),
    void *data);

bool sol_oic_client_get_server_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(struct sol_oic_client *cli,
    const struct sol_oic_server_information *info, void *data),
    void *data);

bool sol_oic_client_resource_request(struct sol_oic_client *client, struct sol_oic_resource *res,
    sol_coap_method_t method, const struct sol_vector *reprs,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_vector *reprs, void *data),
    void *data);

bool sol_oic_client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_str_slice *href, const struct sol_vector *reprs, void *data),
    void *data, bool observe);

struct sol_oic_resource *sol_oic_resource_ref(struct sol_oic_resource *r);
void sol_oic_resource_unref(struct sol_oic_resource *r);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
