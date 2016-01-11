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

#include "sol-coap.h"
#include "sol-network.h"
#include "sol-str-slice.h"
#include "sol-vector.h"
#include "sol-buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines that handle LWM2M protocol.
 *
 */

/**
 * @defgroup LWM2M LWM2M
 * @ingroup Comms
 *
 * @brief Routines that handle LWM2M protocol.
 *
 * @{
 */

/**
 * @brief Macro that defines the default port for a LWM2M server.
 */
#define SOL_LWM2M_DEFAULT_SERVER_PORT (5683)

/**
 * @struct sol_lwm2m_server
 * @brief A handle to a LWM2M server.
 * @see sol_lwm2m_server_new()
 */
struct sol_lwm2m_server;

/**
 * @struct sol_lwm2m_client_info
 * @brief A handle that contains information about a registered LWM2M client.
 * @see sol_lwm2m_server_get_clients()
 */
struct sol_lwm2m_client_info;

/**
 * @struct sol_lwm2m_client_object_instance
 * @brief A handle that contains information about a client object instance.
 */
struct sol_lwm2m_client_object_instance;

/**
 * @struct sol_lwm2m_client_object
 * @brief A handle of a client's object.
 * @see sol_lwm2m_client_info_get_objects()
 */
struct sol_lwm2m_client_object;

/**
 * @brief LWM2M Client binding mode.
 *
 * A LWM2M server, may support multiple forms of binding.
 * The binding mode is requested by a client during its registration.
 *
 * In Queue binding mode a client flags to the server that it
 * may not be available for communication all the time, thus
 * the server must wait until it receives a heartbeat from the
 * client until it can send requests. The queue binding mode
 * is usefull, because the client may enter in deep sleep
 * and save battery and only wake up in certain times.
 *
 * @note The default binding mode is #SOL_LWM2M_BINDING_MODE_U and is the only one supported right know.
 * @see sol_lwm2m_client_info_get_binding_mode()
 */
enum sol_lwm2m_binding_mode {
    /**
     * Indicates that the client is reacheable all the time
     * and all the communication must be done using UDP.
     */
    SOL_LWM2M_BINDING_MODE_U,
    /**
     * Indicate that the client is using Queued UDP binding
     * and all the communication must be done using UDP.
     */
    SOL_LWM2M_BINDING_MODE_UQ,
    /**
     * Indicates that the client is reacheable all the time
     * and all the communication must be done using SMS.
     */
    SOL_LWM2M_BINDING_MODE_S,
    /**
     * Indicates that the client is using Queued SMS binding
     * and all the communication must be done using SMS
     */
    SOL_LWM2M_BINDING_MODE_SQ,
    /**
     * Indicates that the client is using UDP and SMS binding.
     * When the server sends an UDP request the client must
     * send the response using UDP.
     * When the server sends a SMS request the client must
     * send the response using SMS.
     */
    SOL_LWM2M_BINDING_MODE_US,
    /**
     * Indicates that the client using Queued SMS and UDP binding.
     * When the server sends an UDP request the client must
     * send the response using UDP.
     * When the server sends a SMS request the client must
     * send the response using SMS.
     */
    SOL_LWM2M_BINDING_MODE_UQS,
    /**
     * It was not possible to determine the client binding.
     */
    SOL_LWM2M_BINDING_MODE_UNKNOWN = -1,
};

/**
 * @brief Enum that express a LWM2M client lifecycle changes.
 *
 * @see sol_lwm2m_server_add_registration_monitor()
 */
enum sol_lwm2m_registration_event {
    /**
     * Indicates that a client was registered in the server.
     */
    SOL_LWM2M_REGISTRATION_EVENT_REGISTER,
    /**
     * Indicates that a client updated itself in the server.
     */
    SOL_LWM2M_REGISTRATION_EVENT_UPDATE,
    /**
     * Indicates that a client was unregistered.
     */
    SOL_LWM2M_REGISTRATION_EVENT_UNREGISTER,
    /**
     * Indicates that the server is discarting a client, since
     * the server did not hear from it after some time.
     */
    SOL_LWM2M_REGISTRATION_EVENT_TIMEOUT
};

/**
 * @brief Callback that is used to inform a LWM2M client registration event.
 *
 * @param server The LWM2M server.
 * @param cinfo The client that generated the registration event.
 * @param event The registration event itself.
 * @param data User data.
 * @see #sol_lwm2m_registration_event
 * @see sol_lwm2m_server_add_registration_monitor()
 */
typedef void (*sol_lwm2m_server_regisration_event_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event);

/**
 * @brief Creates a new LWM2M server.
 *
 * The server will be immediately operational and waiting for connections.
 *
 * @param port The UDP port to be used.
 * @return The LWM2M server or @c NULL on error.
 */
struct sol_lwm2m_server *sol_lwm2m_server_new(uint16_t port);

/**
 * @brief Adds a registration monitor.
 *
 * This function register a monitor, making it easir to observe a LWM2M client's life cycle.
 * This means that everytime a LWM2M client is registered, updated, deleted or timedout,
 * @c cb will be called.
 *
 * @param server The LWM2M server.
 * @param cb The previous registered callback.
 * @param data The user data to @c cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_add_registration_monitor()
 * @see #sol_lwm2m_server_regisration_event_cb
 */
int sol_lwm2m_server_add_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, const void *data);

/**
 * @brief Removes a registration monitor.
 *
 * @param server The LWM2M server.
 * @param cb The previous registered callback.
 * @param data The user data to @c cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
int sol_lwm2m_server_del_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, const void *data);

/**
 * @brief Gets all registerd clients.
 *
 * @param server The LWM2M Server.
 * @return An vector of #sol_lwm2m_client_info or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
const struct sol_ptr_vector *sol_lwm2m_server_get_clients(const struct sol_lwm2m_server *server);

/**
 * @brief Deletes a server instance.
 *
 * Use this function to stop the LWM2M server and release its resources.
 *
 * @param server The LWM2M server to be deleted.
 * @see sol_lwm2m_server_del()
 */
void sol_lwm2m_server_del(struct sol_lwm2m_server *server);

/**
 * @brief Gets the name of client.
 *
 * @param client The LWM2M cliento info.
 * @return The @c client name or @c NULL on error.
 */
const char *sol_lwm2m_client_info_get_name(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client location path in the LWM2M server.
 *
 * This value is specified by the LWM2M server and it will be used by the client
 * to identify itself.
 *
 * @param client The LWM2M client info.
 * @return The @c client location path or @c NULL on error.
 */
const char *sol_lwm2m_client_info_get_location(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client SMS number.
 *
 * A client may specify an SMS number to be used for communication.
 *
 * @param client The LWM2M client info.
 * @return The SMS number or @c NULL.
 */
const char *sol_lwm2m_client_info_get_sms(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client objects path.
 *
 * A LWM2M client may specify an alternate objects path.
 *
 * @param client The LWM2M client info.
 * @return The objects path or @c NULL.
 * @note The objects path may be @c NULL.
 */
const char *sol_lwm2m_client_info_get_objects_path(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client lifetime in seconds.
 *
 * @param client The LWM2M client info.
 * @param lifetime The client lifetime to be set.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_client_info_get_lifetime(const struct sol_lwm2m_client_info *client, uint32_t *lifetime);

/**
 * @brief Gets the client binding mode.
 *
 * @param client The LWM2M client info.
 * @return The client binding mode or #SOL_LWM2M_BINDING_MODE_UNKNOWN on error.
 */
enum sol_lwm2m_binding_mode sol_lwm2m_client_info_get_binding_mode(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client address.
 *
 * @param client The LWM2M client info.
 * @return The @c client address or @c NULL on error.
 */
const struct sol_network_link_addr *sol_lwm2m_client_info_get_address(const struct sol_lwm2m_client_info *client);

/**
 * @brief Get client objects.
 *
 * @param client The LWM2M client info.
 * @return A array of #sol_lwm2m_client_object or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @see #sol_lwm2m_client_object
 */
const struct sol_vector *sol_lwm2m_client_info_get_objects(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the object id.
 *
 * @param object The LWM2M object to get the id.
 * @param id The object id to be filled.
 * @return 0 success, negative errno on error.
 */
int sol_lwm2m_client_object_get_id(const struct sol_lwm2m_client_object *object, uint16_t *id);

/**
 * @brief Gets the instances of a given object.
 *
 * @param object The LWM2M object object to get the instances.
 * @return An array of uint16_t or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 */
const struct sol_vector *sol_lwm2m_client_object_get_instances(const struct sol_lwm2m_client_object *object);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
