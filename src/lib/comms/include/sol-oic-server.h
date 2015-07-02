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

#include <sol-str-slice.h>
#include <sol-coap.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OIC_DEVICE_NAME
#define OIC_DEVICE_NAME "Soletta OIC Device"
#endif
#ifndef OIC_DEVICE_RESOURCE_TYPE
#define OIC_DEVICE_RESOURCE_TYPE "oc.core"
#endif
#ifndef OIC_DEVICE_ID
#define OIC_DEVICE_ID "soletta0"
#endif
#ifndef OIC_MANUFACTURER_NAME
#define OIC_MANUFACTURER_NAME "Custom"
#endif
#ifndef OIC_MANUFACTORER_MODEL
#define OIC_MANUFACTORER_MODEL "Custom"
#endif
#ifndef OIC_MANUFACTORER_DATE
#define OIC_MANUFACTORER_DATE "2015-01-01"
#endif
#ifndef OIC_INTERFACE_VERSION
#define OIC_INTERFACE_VERSION "1.0"
#endif
#ifndef OIC_PLATFORM_VERSION
#define OIC_PLATFORM_VERSION "1.0"
#endif
#ifndef OIC_FIRMWARE_VERSION
#define OIC_FIRMWARE_VERSION "1.0"
#endif
#ifndef OIC_SUPPORT_LINK
#define OIC_SUPPORT_LINK "http://solettaproject.org/support/"
#endif
#ifndef OIC_LOCATION
#define OIC_LOCATION "Unknown"
#endif
#ifndef OIC_EPI
#define OIC_EPI ""
#endif

struct sol_oic_device_definition;

struct sol_oic_resource_type {
#define SOL_OIC_RESOURCE_TYPE_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved; /* save this hole for a future field */
    struct sol_str_slice endpoint;
    struct sol_str_slice resource_type;
    struct sol_str_slice iface;

    struct {
        sol_coap_responsecode_t (*handle)(const struct sol_network_link_addr *cliaddr,
            const void *data, uint8_t *payload, uint16_t *payload_len);
    } get, put, post, delete;
};

bool sol_oic_server_init(int port);
void sol_oic_server_release(void);

struct sol_oic_device_definition *sol_oic_server_get_definition(struct sol_str_slice endpoint,
    struct sol_str_slice resource_type_prefix);
struct sol_oic_device_definition *sol_oic_server_register_definition(struct sol_str_slice endpoint,
    struct sol_str_slice resource_type_prefix, enum sol_coap_flags flags);
bool sol_oic_server_unregister_definition(const struct sol_oic_device_definition *definition);

struct sol_coap_resource *sol_oic_device_definition_register_resource_type(
    struct sol_oic_device_definition *definition,
    const struct sol_oic_resource_type *resource_type,
    void *handler_data, enum sol_coap_flags flags);
bool sol_oic_notify_observers(struct sol_coap_resource *resource, uint8_t *msg, uint16_t msg_len);

#ifdef __cplusplus
}
#endif
