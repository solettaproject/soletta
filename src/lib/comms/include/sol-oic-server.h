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

#include "sol-oic-common.h"

/**
 * @file
 * @brief Routines to create servers talking OIC protocol.
 */

/**
 * @ingroup OIC
 *
 * @{
 */

#ifndef OIC_MANUFACTURER_NAME
#define OIC_MANUFACTURER_NAME "Soletta"
#endif
#ifndef OIC_MANUFACTURER_URL
#define OIC_MANUFACTURER_URL "https://soletta-project.org"
#endif
#ifndef OIC_MODEL_NUMBER
#define OIC_MODEL_NUMBER "Unknown"
#endif
#ifndef OIC_MANUFACTURE_DATE
#define OIC_MANUFACTURE_DATE "2015-01-01"
#endif
#ifndef OIC_PLATFORM_VERSION
#define OIC_PLATFORM_VERSION "Unknown"
#endif
#ifndef OIC_HARDWARE_VERSION
#define OIC_HARDWARE_VERSION "Unknown"
#endif
#ifndef OIC_FIRMWARE_VERSION
#define OIC_FIRMWARE_VERSION "Unknown"
#endif
#ifndef OIC_SUPPORT_URL
#define OIC_SUPPORT_URL "Unknown"
#endif

struct sol_oic_server_resource;

struct sol_oic_resource_type {
#define SOL_OIC_RESOURCE_TYPE_API_VERSION (1)
    uint16_t api_version;
    int : 0; /* save possible hole for a future field */

    struct sol_str_slice resource_type;
    struct sol_str_slice interface;

    struct {
        sol_coap_responsecode_t (*handle)(const struct sol_network_link_addr *cliaddr,
            const void *data, const struct sol_vector *input, struct sol_vector *output);
    } get, put, post, delete;
};

int sol_oic_server_init(void);
void sol_oic_server_release(void);

struct sol_oic_server_resource *sol_oic_server_add_resource(
    const struct sol_oic_resource_type *rt, const void *handler_data,
    enum sol_oic_resource_flag flags);
void sol_oic_server_del_resource(struct sol_oic_server_resource *resource);

bool sol_oic_notify_observers(struct sol_oic_server_resource *resource,
    const struct sol_vector *repr);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
