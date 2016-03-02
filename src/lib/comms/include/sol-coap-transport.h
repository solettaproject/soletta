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

#include <sol-buffer.h>
#include <sol-network.h>
#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle a CoAP transport.
 *
 * It declares a way to create a transport for Constrained
 * Application Protocol (CoAP).
 */

/**
 * @defgroup CoAP_Transport CoAP_Transport
 * @ingroup Coap
 *
 * @{
 */

/**
 * @brief Struct that represents CoAP transport.
 *
 * It declares a required interface that CoAP uses to communicate.
 * One can create a new transport filling this struct with the
 * proper methods.
 *
 * @see sol_coap_server_new()
 */
struct sol_coap_transport {
#ifndef SOL_NO_API_VERSION
#define SOL_COAP_TRANSPORT_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif

    /**
     * @brief Sends data
     *
     * @param transport The pointer to this struct (self).
     * @param buf The data that is wanted to be sent.
     * @param len The size of the given data's buffer.
     * @param addr The address that will be sent.
     * @return 0 on success, -errno on error.
     */
    int (*sendmsg)(struct sol_coap_transport *transport,
        const void *buf, size_t len, const struct sol_network_link_addr *addr);

    /**
     * @brief Receives data
     *
     * @param transport The pointer to this struct (self).
     * @param buf The buffer that will be used to store the received data.
     * @param len The size of the given data's buffer.
     * @param addr The address from where the data came.
     * @return 0 on success, -errno on error.
     */
    int (*recvmsg)(struct sol_coap_transport *transport,
        void *buf, size_t len, struct sol_network_link_addr *addr);

    /**
     * @brief Set a callback to be called when is possible to send data.
     *
     * @param transport The pointer to this struct (self).
     * @param on_can_write The callback to be called.
     * @param user_data The data that will be given when the callback be called.
     * @return 0 on success, -errno on error.
     */
    int (*set_on_write)(struct sol_coap_transport *transport,
        bool (*on_can_write)(void *data, struct sol_coap_transport *transport),
        const void *user_data);

    /**
     * @brief Set a callback to be called when there is available data to read.
     *
     * @param transport The pointer to this struct (self).
     * @param on_can_read The callback to be called.
     * @param user_data The data that will be given when the callback be called.
     * @return 0 on success, -errno on error.
     */
    int (*set_on_read)(struct sol_coap_transport *transport,
        bool (*on_can_read)(void *data, struct sol_coap_transport *transport),
        const void *user_data);
};

/**
 * @brief Sends data through a CoAP transport.
 *
 * This function is wrapper over the @c sendmsg method of the @c sol_coap_transport
 * struct. It checks the parameters and the API version of the transport.
 *
 * @param transport The pointer to this struct (self).
 * @param buf The data that is wanted to be sent.
 * @param len The size of the given data's buffer.
 * @param addr The address that will be sent.
 * @return 0 on success, -errno on error.
 *
 * @see sol_coap_transport_set_on_write()
 */
int sol_coap_transport_sendmsg(struct sol_coap_transport *transport, const void *buf, size_t len,
    const struct sol_network_link_addr *addr);

/**
 * @brief Receives data using from a CoAP transport.
 *
 * This function is wrapper over the @c recvmsg method of the @c sol_coap_transport
 * struct. It checks the parameters and the API version of the transport.
 *
 * @param transport The pointer to this struct (self).
 * @param buf The buffer that will be used to store the received data.
 * @param len The size of the given data's buffer.
 * @param addr The address from where the data came.
 * @return 0 on success, -errno on error.
 *
 * @see sol_coap_transport_set_on_read()
 */
int sol_coap_transport_recvmsg(struct sol_coap_transport *transport, void *buf, size_t len,
    struct sol_network_link_addr *addr);

/**
 * @brief Receives data using from a CoAP transport.
 *
 * This function is wrapper over the @c set_on_write method of the @c sol_coap_transport
 * struct. It checks the parameters and the API version of the transport.
 *
 * @param transport The pointer to this struct (self).
 * @param on_can_write The callback to be called.
 * @param user_data The data that will be given when the callback be called.
 * @return 0 on success, -errno on error.
 *
 * @see sol_coap_transport_sendmsg()
 */
int sol_coap_transport_set_on_write(struct sol_coap_transport *transport,
    bool (*on_can_write)(void *data, struct sol_coap_transport *transport),
    const void *user_data);

/**
 * @brief Set a callback to be called when there is available data to read.
 *
 * This function is wrapper over the @c set_on_read method of the @c sol_coap_transport
 * struct. It checks the parameters and the API version of the transport.
 *
 * @param transport The pointer to this struct (self).
 * @param on_can_read The callback to be called.
 * @param user_data The data that will be given when the callback be called.
 * @return 0 on success, -errno on error.
 *
 * @see sol_coap_transport_recvmsg()
 */
int sol_coap_transport_set_on_read(struct sol_coap_transport *transport,
    bool (*on_can_read)(void *data, struct sol_coap_transport *transport),
    const void *user_data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
