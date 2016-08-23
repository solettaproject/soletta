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

#include "sol-lwm2m.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to create Servers talking the LWM2M protocol.
 *
 */

/**
 * @defgroup lwm2m_server LWM2M Server
 * @ingroup LWM2M
 *
 * @brief Routines to create Servers talking the LWM2M protocol.
 *
 * @{
 */

/**
 * @typedef sol_lwm2m_server
 * @brief A handle to a LWM2M server.
 * @see sol_lwm2m_server_new()
 */
struct sol_lwm2m_server;
typedef struct sol_lwm2m_server sol_lwm2m_server;

/**
 * @typedef sol_lwm2m_client_info
 * @brief A handle that contains information about a registered LWM2M client.
 * @see sol_lwm2m_server_get_clients()
 */
struct sol_lwm2m_client_info;
typedef struct sol_lwm2m_client_info sol_lwm2m_client_info;

/**
 * @brief Enum that expresses a LWM2M client lifecycle changes.
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
     * Indicates that the server is discarding a client, since
     * the server did not hear from it after some time.
     */
    SOL_LWM2M_REGISTRATION_EVENT_TIMEOUT
};

/**
 * @brief Creates a new LWM2M server.
 *
 * The server will be immediately operational and waiting for connections.
 *
 * @param coap_port The UDP port to be used for the NoSec CoAP Server.
 * @param num_sec_modes The number of DTLS Security Modes this Bootstrap Server will support.
 * @param ... An @c uint16_t indicating the UDP port to be used for the Secure DTLS Server; and at least one @c sol_lwm2m_security_mode followed by its relevant parameters, as per the table below:
 *
 * Security Mode | Follow-up arguments | Description
 * ------------- | ------------------- | ------------------
 * SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY | struct sol_lwm2m_security_psk **known_psks | @c known_psks The Clients' Pre-Shared Keys this Server has previous knowledge of. It MUST be a NULL-terminated array.
 * SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY | struct sol_lwm2m_security_rpk *rpk, struct sol_blob **known_pub_keys | @c rpk This Server's Key Pair - @c known_pub_keys The Clients' Public Keys this Bootstrap Server has previous knowledge of. It MUST be a NULL-terminated array.
 *
 * @note: SOL_LWM2M_SECURITY_MODE_CERTIFICATE is not supported yet.
 *
 * @return The LWM2M server or @c NULL on error.
 */
struct sol_lwm2m_server *sol_lwm2m_server_new(uint16_t coap_port,
    uint16_t num_sec_modes, ...);

/**
 * @brief Adds a registration monitor.
 *
 * This function registers a monitor, making it easier to observe a
 * LWM2M client's life cycle. This means that every time a LWM2M
 * client is registered, updated, deleted or timed out, @a
 * sol_lwm2m_server_registration_event_cb will be called.
 *
 * @param server The LWM2M server.
 * @param sol_lwm2m_server_registration_event_cb A callback that is used to inform a LWM2M client registration event - @c data User data; @c server The LWM2M server; @c cinfo The client that generated the registration event; @c event The registration event itself.
 * @param data The user data to @c sol_lwm2m_server_registration_event_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_del_registration_monitor()
 */
int sol_lwm2m_server_add_registration_monitor(struct sol_lwm2m_server *server,
    void (*sol_lwm2m_server_registration_event_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event),
    const void *data);

/**
 * @brief Removes a registration monitor.
 *
 * @param server The LWM2M server.
 * @param sol_lwm2m_server_registration_event_cb The previous registered callback. - @c data User data; @c server The LWM2M server; @c cinfo The client that generated the registration event; @c event The registration event itself.
 * @param data The user data to @c sol_lwm2m_server_registration_event_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
int sol_lwm2m_server_del_registration_monitor(struct sol_lwm2m_server *server,
    void (*sol_lwm2m_server_registration_event_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event),
    const void *data);

/**
 * @brief Gets all registerd clients.
 *
 * @param server The LWM2M Server.
 * @return a vector of #sol_lwm2m_client_info or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
const struct sol_ptr_vector *sol_lwm2m_server_get_clients(const struct sol_lwm2m_server *server);

/**
 * @brief Observes a client object, instance or resource.
 *
 * Every time the observed path changes, the client will notify the LWM2M server.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client to be observed.
 * @param path The path to be observed (Example: /3/0/0).
 * @param sol_lwm2m_server_content_cb A callback to be called when the observed path changes, used to inform a observable/read response. - @c server The LWM2M server; @c client The LWM2M client; @c path The client's path; @c response_code The response code; @c content_type The response content type; @c content The response content; @c data User data.
 * @param data User data to @c sol_lwm2m_server_content_cb
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_del_observer()
 */
int sol_lwm2m_server_add_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*sol_lwm2m_server_content_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data);

/**
 * @brief Unobserve a client object, instance or resource.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client to be unobserved.
 * @param path The path to be unobserved (Example: /3/0/0).
 * @param sol_lwm2m_server_content_cb The previous registered callback. - @c server The LWM2M server; @c client The LWM2M client; @c path The client's path; @ response_code The response code; @c content_type The response content type; @c content The response content; @c data User data.
 * @param data User data to @c sol_lwm2m_server_content_cb
 * @return 0 on success, -errno on error.
 *
 * @note In order do completly unobserve a path, all observers must be deleted.
 * @see sol_lwm2m_server_add_observer()
 */
int sol_lwm2m_server_del_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*sol_lwm2m_server_content_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data);

/**
 * @brief Writes an object instance or resource.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to write
 * @param path The object path to be written (Example /1/1).
 * @param resources An array of #sol_lwm2m_resource
 * @param len The length of @c resources
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the write operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 */
int sol_lwm2m_server_write(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Deletes an object instance on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to delete an object
 * @param path The object path to be deleted (Example /1/1).
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the delete operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_server_delete_object_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Executes an resource on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to execute the resource.
 * @param path The object path to be executed (Example /1/1/8).
 * @param args Arguments to the execute command.
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the execute operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_server_execute_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path, const char *args,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Creates an object instance on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to create an object instance.
 * @param path The object path to be created (Example /1).
 * @param resources An array of #sol_lwm2m_resource which contains the required resources to create an object.
 * @param len The length of @c resources.
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the create operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 */
int sol_lwm2m_server_create_object_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Reads an object, instance or resource from a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to be read.
 * @param path The path to be read (Example /3/0/0).
 * @param sol_lwm2m_server_content_cb A callback to be called when the read operation is completed. - @c server The LWM2M server; @c client The LWM2M client; @c path The client's path; @c response_code The response code; @c content_type The response content type; @c content The response content; @c data User data.
 * @param data User data to @c sol_lwm2m_server_content_cb
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_server_read(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*sol_lwm2m_server_content_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data);

/**
 * @brief Deletes a server instance.
 *
 * Use this function to stop the LWM2M server and release its resources.
 *
 * @param server The LWM2M server to be deleted.
 * @see sol_lwm2m_server_new()
 */
void sol_lwm2m_server_del(struct sol_lwm2m_server *server);

/**
 * @brief Gets the name of client.
 *
 * @param client The LWM2M client info.
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
const char *sol_lwm2m_client_info_get_sms_number(const struct sol_lwm2m_client_info *client);

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
 * @brief Get client's objects.
 *
 * @param client The LWM2M client info.
 * @return A array of #sol_lwm2m_client_object or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @note Be advised that it's not recommended to store object
 * pointers, because during the client's update method, all
 * the objects are renewed.
 * @see #sol_lwm2m_client_object
 */
const struct sol_ptr_vector *sol_lwm2m_client_info_get_objects(const struct sol_lwm2m_client_info *client);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
