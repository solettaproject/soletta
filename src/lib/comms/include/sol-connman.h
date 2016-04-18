/*
 * This file is part of the Soletta Project
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
#include <stdint.h>
#include <sol-network.h>
#include <sol-buffer.h>
#include <sol-vector.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct sol_connman_service
 *
 * @brief service struct
 */
struct sol_connman_service;

/**
 * @enum sol_connman_service_state
 *
 * @brief service state
 *
 * One of these must be chosen to set the state of service,
 * with sol_connman_service_get_state()
 *
 * @note SOL_CONNMAN_SERVICE_STATE_REMOVE is used to show
 * the service has been removed. When it is notified,
 * the service has been removed in the system.
 */
enum sol_connman_service_state {
    SOL_CONNMAN_SERVICE_STATE_UNKNOWN       = 0,
    SOL_CONNMAN_SERVICE_STATE_IDLE          = 1,
    SOL_CONNMAN_SERVICE_STATE_ASSOCIATION   = 2,
    SOL_CONNMAN_SERVICE_STATE_CONFIGURATION = 3,
    SOL_CONNMAN_SERVICE_STATE_READY         = 4,
    SOL_CONNMAN_SERVICE_STATE_ONLINE        = 5,
    SOL_CONNMAN_SERVICE_STATE_DISCONNECT    = 6,
    SOL_CONNMAN_SERVICE_STATE_FAILURE       = 7,
    SOL_CONNMAN_SERVICE_STATE_REMOVE        = 8,
};

#define SOL_CONNMAN_SERVICE_TYPE_ETHERNET   "ethernet"  /**< @brief ethernet service type string */
#define SOL_CONNMAN_SERVICE_TYPE_WIFI       "wifi"      /**< @brief wifi service type string */
#define SOL_CONNMAN_SERVICE_TYPE_BLUETOOTH  "bluetooth" /**< @brief bluetooth service type string */
#define SOL_CONNMAN_SERVICE_TYPE_CELLULAR   "cellular"   /**< @brief bluetooth service type string */
#define SOL_CONNMAN_SERVICE_TYPE_GPS        "gps"       /**< @brief gps service type string */
#define SOL_CONNMAN_SERVICE_TYPE_VPN        "vpn"       /**< @brief vpn service type string */
#define SOL_CONNMAN_SERVICE_TYPE_GADGET     "gadget"    /**< @brief gadget service type string */
#define SOL_CONNMAN_SERVICE_TYPE_P2P        "p2p"       /**< @brief p2p service type string */

/**
 * @enum sol_connman_state
 *
 * @brief the global connection state of system
 *
 * One of these must be chosen for the global connection state,
 * with sol_connman_get_state()
 *
 */
enum sol_connman_state {
    SOL_CONNMAN_STATE_UNKNOWN   = 0,
    SOL_CONNMAN_STATE_IDLE      = 1,
    SOL_CONNMAN_STATE_READY     = 2,
    SOL_CONNMAN_STATE_ONLINE    = 3,
    SOL_CONNMAN_STATE_OFFLINE   = 4,
};

/**
 * @brief Gets the service name
 *
 * This function gets the name for the connman service,
 * after the service is to give by service monitor.
 *
 * @param service The service structure which the name is desired
 *
 * @return The name of the services on success, NULL on failure.
 */
const char *sol_connman_service_get_name(
    const struct sol_connman_service *service);

/**
 * @brief Gets the service state
 *
 * This function gets the state for the connman service,
 * after the service is to give by service monitor.
 *
 * @note SOL_CONNMAN_SERVICE_STATE_REMOVE is used to show
 * the service has been removed. When it is notified,
 * the service has been removed in the system.
 *
 * @param service The service structure which the state is desired
 *
 * @return The state of the services on success,
 * SOL_CONNMAN_SERVICE_STATE_UNKNOWN on failure.
 */
enum sol_connman_service_state sol_connman_service_get_state(
    const struct sol_connman_service *service);

/**
 * @brief Gets the service type
 *
 * This function gets the type for the connman service,
 * after the service is to give by service monitor.
 *
 * @param service The service structure which the type is desired
 *
 * @return The type of the services on success,
 * SOL_CONNMAN_SERVICE_TYPE_UNKNOWN on failure.
 */
const char *sol_connman_service_get_type(
    const struct sol_connman_service *service);

/**
 * @brief Gets the service network address
 *
 * This function gets the network address for the connman service,
 * after the service is to give by service monitor.
 *
 * @param service The service structure which the network address is desired
 * @param family which type of the network address is desired (ipv4 or ipv6).
 *
 * @return A point to sol_network_link_addr created on success, NULL on failure.
 */
struct sol_network_link_addr *sol_connman_service_get_network_address(
    const struct sol_connman_service *service, enum sol_network_family family);

/**
 * @brief Gets the service strength
 *
 * This function gets the service strength for the connman service,
 * after the service is to give by service monitor.
 *
 * @param service The service structure which the service strength is
 * desired
 *
 * @return 0-100 on success, -errno on failure.
 */
int32_t sol_connman_service_get_strength(
    const struct sol_connman_service *service);

/**
 * @brief Gets the call result for service
 *
 * This function gets the call result for service.
 * Since the connman function is asynchronous, the call return is not connection error,
 * but just some dispatching/immediate error. The actual state change will be notified
 * via sol_connman_add_service_monitor() callbacks.
 * After the actual state change is notified via sol_connman_add_service_monitor()
 * callbacks, sol_connman_service_get_call_result can be used to get the actual state.
 *
 * @return TRUE on success, FALSE on failure
 */
bool sol_connman_service_get_call_result(
    const struct sol_connman_service *service);

/**
 * @brief Gets the global connection state of system
 *
 * This function gets the global connection state for system.
 *
 * @return The state of the global connection state on success,
 * SOL_CONNMAN_STATE_UNKNOWN on failure
 */
enum sol_connman_state sol_connman_get_state(void);

/**
 * @brief Sets the global connection state to offline
 *
 * This function sets the global connection state to offline,
 * enter offline Mode too.
 *
 * @param enabled The value is set for offline on/off
 *
 * @return 0 on success, -errno on failure.
 */
int sol_connman_set_offline(bool enabled);

/**
 * @brief Gets the global connection state of offline
 *
 * This function gets the global connection state of offline.
 *
 * @return True if the offline is enabled, false otherwise.
 */
bool sol_connman_get_offline(void);

/**
 * @brief Connect the service
 *
 * This function connects the service, after the service is to give by service monitor.
 * Since the connman function is asynchronous, the return is not connection error,
 * but just some dispatching/immediate error. The actual state change will be notified
 * via sol_connman_add_service_monitor() callbacks. This callback must be added BEFORE
 * the sol_connman_service_connect() is called to ensure no messages are lost.
 *
 * @param service The service structure which the connection is desired
 *
 * @return 0 on success, -errno on failure.
 */
int sol_connman_service_connect(struct sol_connman_service *service);

/**
 * @brief Disconnect the service
 *
 * This function disconnects the service.
 * Since the connman function is asynchronous, the return is not disconnection error,
 * but just some dispatching/immediate error. The actual state change will be notified
 * via sol_connman_add_service_monitor() callbacks. This callback must be added BEFORE
 * the sol_connman_service_disconnect() is called to ensure no messages are lost.
 *
 * @param service The service structure which the disconnection is desired
 *
 * @return 0 on success, -errno on failure.
 */
int sol_connman_service_disconnect(struct sol_connman_service *service);

/**
 * @brief Adds a monitor for the updated connman services
 *
 * To monitor the property of services, it gives information
 * about the services. The callback is used to provide the information.
 * sol_connman_add_service_monitor will call sol_connman_lazy_init
 * to init connection manager. This callback must be added BEFORE
 * the all functions are called to ensure connection manager
 * has been initialized.
 *
 * @param cb monitor Callback to be called when the services are updated
 * @param data Add a user data per callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_connman_add_service_monitor(void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data);

/**
 * @brief Dels a monitor for the updated connman services
 *
 * Removes the monitor for updated connman services.
 * sol_connman_del_service_monitor will call sol_connman_lazy_shutdown
 * to shutdown connection manager. This callback must be added AFTER
 * the all functions are called to ensure connection manager has been shutdown.
 *
 * @param cb monitor Callback to be called when the services are updated
 * @param data Add a user data per callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_connman_del_service_monitor(void (*cb)(void *data, const struct sol_connman_service *service),
    const void *data);

/**
 * @brief Gets the connman services vector.
 *
 * The connman services vector can be given via sol_connman_get_vector.
 *
 * @param vector The retrieved content
 *
 * @return 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_connman_get_service_vector(struct sol_vector **vector);

#ifdef __cplusplus
}
#endif
