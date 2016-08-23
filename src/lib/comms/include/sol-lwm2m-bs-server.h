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
 * @brief Routines to create Bootstrap Servers talking the LWM2M protocol.
 *
 */

/**
 * @defgroup lwm2m_bs-server LWM2M Bootstrap Server
 * @ingroup LWM2M
 *
 * @brief Routines to create Bootstrap Servers talking the LWM2M protocol.
 *
 * @{
 */

/**
 * @typedef sol_lwm2m_bootstrap_server
 * @brief A handle to a LWM2M bootstrap server.
 * @see sol_lwm2m_bootstrap_server_new()
 */
struct sol_lwm2m_bootstrap_server;
typedef struct sol_lwm2m_bootstrap_server sol_lwm2m_bootstrap_server;

/**
 * @typedef sol_lwm2m_bootstrap_client_info
 * @brief A handle that contains information about a bootstrapping LWM2M client.
 */
struct sol_lwm2m_bootstrap_client_info;
typedef struct sol_lwm2m_bootstrap_client_info sol_lwm2m_bootstrap_client_info;

/**
 * @brief Creates a new LWM2M bootstrap server.
 *
 * The server will be immediately operational and waiting for connections.
 *
 * @param port The UDP port to be used.
 * @param known_clients A NULL-terminated array with the name of all clients this server has Bootstrap Information for.
 * @param num_sec_modes The number of DTLS Security Modes this Bootstrap Server will support.
 * @param ... At least one @c sol_lwm2m_security_mode followed by its relevant parameters, as per the table below:
 *
 * Security Mode | Follow-up arguments | Description
 * ------------- | ------------------- | ------------------
 * SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY | struct sol_lwm2m_security_psk **known_psks | @c known_psks The Clients' Pre-Shared Keys this Bootstrap Server has previous knowledge of.  It MUST be a NULL-terminated array.
 * SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY | struct sol_lwm2m_security_rpk *rpk, struct sol_blob **known_pub_keys | @c rpk This Bootstrap Server's Key Pair - @c known_pub_keys The Clients' Public Keys this Bootstrap Server has previous knowledge of.  It MUST be a NULL-terminated array.
 *
 * @note: SOL_LWM2M_SECURITY_MODE_CERTIFICATE is not supported yet.
 *
 * @return The LWM2M bootstrap server or @c NULL on error.
 */
struct sol_lwm2m_bootstrap_server *sol_lwm2m_bootstrap_server_new(uint16_t port,
    const char **known_clients, uint16_t num_sec_modes, ...);

/**
 * @brief Adds a bootstrap request monitor to the server.
 *
 * This function register a bootstrap request monitor.
 * This means that every time a LWM2M client performs a Bootstrap Request
 * @c sol_lwm2m_bootstrap_server_request_cb will be called.
 *
 * @param server The LWM2M bootstrap server.
 * @param sol_lwm2m_bootstrap_server_request_cb A callback that is used to inform a LWM2M client bootstrap request - @c data User data; @c server The LWM2M bootstrap server; @c bs_cinfo The client that initiated the bootstrap request.
 * @param data The user data to @c sol_lwm2m_bootstrap_server_request_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_bootstrap_server_del_request_monitor()
 */
int sol_lwm2m_bootstrap_server_add_request_monitor(struct sol_lwm2m_bootstrap_server *server,
    void (*sol_lwm2m_bootstrap_server_request_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo),
    const void *data);

/**
 * @brief Removes a bootstrap request monitor from the server.
 *
 * @param server The LWM2M bootstrap server.
 * @param sol_lwm2m_bootstrap_server_request_cb The previous registered callback. - @c data User data; @c server The LWM2M bootstrap server; @c bs_cinfo The client that initiated the bootstrap request.
 * @param data The user data to @c sol_lwm2m_bootstrap_server_request_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_bootstrap_server_add_request_monitor()
 */
int sol_lwm2m_bootstrap_server_del_request_monitor(struct sol_lwm2m_bootstrap_server *server,
    void (*sol_lwm2m_bootstrap_server_request_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo),
    const void *data);

/**
 * @brief Writes a full object through the Bootstrap Interface.
 *
 * @param server The LWM2M bootstrap server.
 * @param client The LWM2M bootstrap client info to write.
 * @param path The object path to be written (Example /2).
 * @param instances An array of #sol_lwm2m_resource arrays
 * @param instances_len An array with the length of each element from @c instances
 * @param instances_ids An array with the desired instance_id of each element from @c instances
 * @param len The length of @c instances
 * @param sol_lwm2m_bootstrap_server_status_response_cb A callback to be called when the write operation is completed. - @c server The LW2M bootstrap server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_bootstrap_server_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 * @note Only in Bootstrap Interface, the Write operation can be made using a TLV consisting of various Object Instances.
 */
int sol_lwm2m_bootstrap_server_write_object(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    struct sol_lwm2m_resource **instances, size_t *instances_len,
    uint16_t *instances_ids, size_t len,
    void (*sol_lwm2m_bootstrap_server_status_response_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Writes an object instance or resource through the Bootstrap Interface.
 *
 * @param server The LWM2M bootstrap server.
 * @param client The LWM2M bootstrap client info to write.
 * @param path The object path to be written (Example /2/1).
 * @param resources An array of #sol_lwm2m_resource
 * @param len The length of @c resources
 * @param sol_lwm2m_bootstrap_server_status_response_cb A callback to be called when the write operation is completed. - @c server The LW2M bootstrap server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_bootstrap_server_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 * @note Only in Bootstrap Interface, the Write operation can be made using a TLV consisting of various Object Instances.
 */
int sol_lwm2m_bootstrap_server_write(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*sol_lwm2m_bootstrap_server_status_response_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Deletes an object instance on a client through the Bootstrap Interface.
 *
 * @param server The LWM2M bootstrap server.
 * @param client The LWM2M bootstrap client info to delete an object
 * @param path The object path to be deleted (Example /1/1).
 * @param sol_lwm2m_bootstrap_server_status_response_cb A callback to be called when the delete operation is completed. - @c server The LW2M bootstrap server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_bootstrap_server_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note Only in Bootstrap Interface, Delete operation MAY target to '/' URI to delete all the existing Object Instances - except LWM2M Bootstrap Server Account - in the LWM2M Client, for initialization purpose before LWM2M Bootstrap Server sends Write operation(s) to the LWM2M Client.
 */
int sol_lwm2m_bootstrap_server_delete_object_instance(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    void (*sol_lwm2m_bootstrap_server_status_response_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Deletes a bootstrap server instance.
 *
 * Use this function to stop the LWM2M bootstrap server and release its resources.
 *
 * @param server The LWM2M bootstrap server to be deleted.
 * @see sol_lwm2m_bootstrap_server_new()
 */
void sol_lwm2m_bootstrap_server_del(struct sol_lwm2m_bootstrap_server *server);

/**
 * @brief Signals the end of the Bootstrap Process.
 *
 * Use this function to tell the LWM2M Client that this LWM2M Bootstrap
 * Server has finished sending the available Bootstrap Information.
 *
 * @param server The LWM2M Bootstrap Server.
 * @param client The LWM2M Bootstrap Client info object.
 * @note After this function returns, the @c sol_lwm2m_bootstrap_client_info handle will be invalid!
 * @see sol_lwm2m_bootstrap_server_new()
 */
int sol_lwm2m_bootstrap_server_send_finish(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client);

/**
 * @brief Gets the name of bootstrap client.
 *
 * @param client The LWM2M bootstrap client info.
 * @return The @c client name or @c NULL on error.
 */
const char *sol_lwm2m_bootstrap_client_info_get_name(const struct sol_lwm2m_bootstrap_client_info *client);

/**
 * @brief Gets the bootstrap client address.
 *
 * @param client The LWM2M bootstrap client info.
 * @return The @c client address or @c NULL on error.
 */
const struct sol_network_link_addr *sol_lwm2m_bootstrap_client_info_get_address(const struct sol_lwm2m_bootstrap_client_info *client);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
