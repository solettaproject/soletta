/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <sol-coap-transport.h>
#include <sol-network.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to use CoAP protocol IP transport.
 */

/**
 * @defgroup CoAP_Transport_IP CoAP_Transport_IP
 * @ingroup CoAP_Transport
 *
 * @{
 */

/**
 * @brief Creates a new CoAP transport over UDP.
 *
 * This function will create a new CoAP transport using UDP socket.
 *
 * @param addr The address to be used (where the socket will be bound).
 * @return A CoAP transport handle or @c NULL on error
 *
 * @see sol_coap_server_new()
 * @see sol_coap_transport_ip_secure_new()
 * @see sol_coap_transport_del()
 */
struct sol_coap_transport *sol_coap_transport_ip_new(const struct sol_network_link_addr *addr);

/**
 * @brief Creates a new CoAP transport over UDP using a DTLS socket.
 *
 * This function will create a new CoAP transport using UDP socket and all the
 * traffic will be encrypted.
 *
 * @param addr The address to be used (where the socket will be bound).
 * @return A CoAP transport handle or @c NULL on error
 *
 * @see sol_coap_server_new()
 * @see sol_coap_transport_ip_new()
 * @see sol_coap_transport_del()
 */
struct sol_coap_transport *sol_coap_transport_ip_secure_new(const struct sol_network_link_addr *addr);

/**
 * @brief Deletes a CoAP transport created with @c sol_coap_transport_ip_secure_new() or
 * @c sol_coap_transport_ip_new()
 *
 * @param transport The transport to be deleted.
 *
 * @see sol_coap_transport_ip_secure_new()
 * @see sol_coap_transport_ip_new()
 */
void sol_coap_transport_ip_del(struct sol_coap_transport *transport);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
