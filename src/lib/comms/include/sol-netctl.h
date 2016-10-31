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

#include <stdbool.h>
#include <stdint.h>
#include <sol-network.h>
#include <sol-buffer.h>
#include <sol-vector.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to manage connections.
 */

/**
 * @defgroup NetCtl NetCtl
 * @ingroup Comms
 *
 * API that should be used to manage device connections.
 *
 * @warning Experimental API. Changes are expected in future releases.
 *
 * @{
 */

/**
 * @typedef sol_netctl_service
 *
 * @brief service struct
 */
struct sol_netctl_service;
typedef struct sol_netctl_service sol_netctl_service;

/**
 * @brief network params
 *
 * This struct contains the information of a network.
 * it has the addr of network link addr, the network of
 * netmask and its gateway of network.
 */
typedef struct sol_netctl_network_params {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_NETWORK_PARAMS_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /**
     * @brief The network devices address
     */
    struct sol_network_link_addr addr;
    /**
     * @brief The network devices netmask
     */
    struct sol_network_link_addr netmask;
    /**
     * @brief The network gateway
     */
    struct sol_network_link_addr gateway;
} sol_netctl_network_params;

#define SOL_NETCTL_AGENT_NAME         "Name"    /**< @brief the agent input name type string */
#define SOL_NETCTL_AGENT_IDENTITY     "Identity"    /**< @brief the agent input identity type string */
#define SOL_NETCTL_AGENT_PASSPHRASE   "Passphrase"    /**< @brief the agent input passphrase type string */
#define SOL_NETCTL_AGENT_WPS          "WPS"    /**< @brief the agent input WPS type string */
#define SOL_NETCTL_AGENT_USERNAME     "Username"    /**< @brief the agent input username type string */
#define SOL_NETCTL_AGENT_PASSWORD     "Password"    /**< @brief the agent input password type string */

/**
 * @enum sol_netctl_service_state
 *
 * @brief service state
 *
 * One of these must be chosen to set the state of service,
 * with sol_netctl_service_get_state()
 *
 * @note SOL_NETCTL_SERVICE_STATE_REMOVE is used to show
 * the service has been removed. When it is notified,
 * the service has been removed in the system.
 */
enum sol_netctl_service_state {
    /*
     * @brief the service is unknow
     *
     * a service is unknow when the service is init
     * and the service state can't be given.
     */
    SOL_NETCTL_SERVICE_STATE_UNKNOWN = 0,
    /*
     * @brief the service is idle
     *
     * a service is idle when the service is not in use
     * at all at the moment and it is not attempting to
     * connect or do anything.
     */
    SOL_NETCTL_SERVICE_STATE_IDLE,
    /*
     * @brief the service is association
     *
     * a service is association when the service tries to
     * establish a low-level connection to the network.
     */
    SOL_NETCTL_SERVICE_STATE_ASSOCIATION,
    /*
     * @brief the service is configuration
     *
     * a service is configuration when the service is trying to
     * retrieve/configure IP settings.
     */
    SOL_NETCTL_SERVICE_STATE_CONFIGURATION,
    /*
     * @brief the service is ready
     *
     * a service is ready when a successfully connected device.
     */
    SOL_NETCTL_SERVICE_STATE_READY,
    /*
     * @brief the service is online
     *
     * a service is online when an internet connection is available
     * and has been verified.
     */
    SOL_NETCTL_SERVICE_STATE_ONLINE,
    /*
     * @brief the service is disconnect
     *
     * a service is disconnect when the service is going to terminate
     * the current connection and will return to "idle".
     */
    SOL_NETCTL_SERVICE_STATE_DISCONNECT,
    /*
     * @brief the service is failure
     *
     * a service is failure when the service indicates a wrong behavior.
     */
    SOL_NETCTL_SERVICE_STATE_FAILURE,
    /*
     * @brief the service is remove
     *
     * a service is remove when the service is not available and removed
     * from the network system. At the same time, the service handle(pointer)
     * will become invalid.
     */
    SOL_NETCTL_SERVICE_STATE_REMOVE,
};

/**
 * @brief method of proxy generated for a network link.
 *
 */
enum sol_netctl_proxy_method {
    SOL_NETCTL_PROXY_METHOD_DIRECT,
    SOL_NETCTL_PROXY_METHOD_AUTO,
    SOL_NETCTL_PROXY_METHOD_MANUAL,
};

/**
 * @brief struct to represent a network proxy.
 *
 * This struct contains the necessary information to present a
 * network proxy for user.
 */
typedef struct sol_netctl_proxy {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_PROXY_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /** @brief proxy method provided */
    enum sol_netctl_proxy_method method;
    /** @brief automatic proxy configuration URL */
    char *url;
    /** @brief List of proxy URIs when manual method is set */
    sol_ptr_vector servers;
    /** @brief List of host can be accessed directly */
    sol_ptr_vector excludes;
} sol_netctl_proxy;

/**
 * @brief struct to represent a network provider.
 *
 * This struct contains the necessary information to present a
 * network provider for user.
 */
typedef struct sol_netctl_provider {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_PROVIDER_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /** @brief VPN host IP */
    char *host;
    /** @brief VPN Domain */
    char *domain;
    /** @brief provider name */
    char *name;
    /** @brief provider type */
    char *type;
} sol_netctl_provider;

/**
 * @brief struct to represent a network ethernet.
 *
 * This struct contains the necessary information to present a
 * network ethernet for user.
 */
typedef struct sol_netctl_ethernet {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_ETHERNET_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /** @brief Possible value are auto and manual */
    char *method;
    /** @brief Interface name */
    char *interface;
    /** @brief Ethernet device address */
    char *address;
    /** @brief Selected duplex settings of line */
    char *duplex;
    /** @brief The ethernet MTU */
    uint16_t mtu;
    /** @brief Selected speed of line */
    uint16_t speed;
} sol_netctl_ethernet;

#define SOL_NETCTL_SERVICE_TYPE_ETHERNET   "ethernet"  /**< @brief ethernet service type string */
#define SOL_NETCTL_SERVICE_TYPE_WIFI       "wifi"      /**< @brief wifi service type string */
#define SOL_NETCTL_SERVICE_TYPE_BLUETOOTH  "bluetooth" /**< @brief bluetooth service type string */
#define SOL_NETCTL_SERVICE_TYPE_CELLULAR   "cellular"   /**< @brief bluetooth service type string */
#define SOL_NETCTL_SERVICE_TYPE_GPS        "gps"       /**< @brief gps service type string */
#define SOL_NETCTL_SERVICE_TYPE_VPN        "vpn"       /**< @brief vpn service type string */
#define SOL_NETCTL_SERVICE_TYPE_GADGET     "gadget"    /**< @brief gadget service type string */
#define SOL_NETCTL_SERVICE_TYPE_P2P        "p2p"       /**< @brief p2p service type string */

/**
 * @enum sol_netctl_state
 *
 * @brief the global connection state of system
 *
 * One of these must be chosen for the global connection state,
 * with sol_netctl_get_state()
 *
 */
enum sol_netctl_state {
    /*
     * @brief the state is unknow
     *
     * a state is unknow when the state is init
     * and the state can't be given.
     */
    SOL_NETCTL_STATE_UNKNOWN = 0,
    /*
     * @brief the state is idle
     *
     * a state is idle when no serives is in either "ready" or "online" state.
     */
    SOL_NETCTL_STATE_IDLE,
    /*
     * @brief the state is ready
     *
     * a state is ready when at least one service is in "ready"
     * state and no service is in "online" state.
     */
    SOL_NETCTL_STATE_READY,
    /*
     * @brief the state is online
     *
     * a state is online when at least one service is in "online".
     */
    SOL_NETCTL_STATE_ONLINE,
    /*
     * @brief the state is offline
     *
     * a state is offline when the device is in offline mode and
     * the user doesn't re-enable individual technologies like
     * WiFi and Bluetooth while in offline mode.
     */
    SOL_NETCTL_STATE_OFFLINE,
};

/**
 * @brief agent input struct
 *
 * This struct contains the information of agent input.
 */
typedef struct sol_netctl_agent_input {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_AGENT_INPUT_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /**
     * @brief The agent prompt type
     */
    char *type;
    /**
     * @brief The agent input value
     */
    char *input;
} sol_netctl_agent_input;

/**
 * @brief agent callback functions
 *
 * This struct contains the callback functions of agent.
 */
typedef struct sol_netctl_agent {
#ifndef SOL_NO_API_VERSION
#define SOL_NETCTL_AGENT_API_VERSION (1)
    uint16_t api_version;
#endif
    /**
     * @brief connection error callback used to inform connection failure
     *
     * @param data the user data
     * @param service the connection failure service
     * @param error the error information
     */
    void (*report_error)(void *data, const struct sol_netctl_service *service,
        const char *error);
    /**
     * @brief connection input callback used to inform connection login input
     *
     * @param data the user data
     * @param service the connection login input service
     * @param inputs the ptr vector of login input type
     */
    void (*request_input)(void *data, const struct sol_netctl_service *service,
        const struct sol_ptr_vector *inputs);
    /**
     * @brief connection cancel callback used to inform connection cancel
     *
     * @param data the user data
     */
    void (*cancel)(void *data);
    /**
     * @brief agent release callback used to inform agent release
     *
     * @param data the user data
     */
    void (*release)(void *data);
} sol_netctl_agent;

/**
 * @brief Service monitor callback used to inform a service changed
 *
 * @param data the user data
 * @param service the monitor service
 */
typedef void (*sol_netctl_service_monitor_cb)
    (void *data,
    const struct sol_netctl_service *service);

/**
 * @brief Manager monitor callback used to inform a manager updated
 *
 * @param data the user data
 */
typedef void (*sol_netctl_manager_monitor_cb)(void *data);

/**
 * @brief error monitor callback used to inform a call result
 *
 * @param data the user data
 * @param service the activity service
 * @param error the call result
 */
typedef void (*sol_netctl_error_monitor_cb)
    (void *data, const struct sol_netctl_service *service,
    unsigned int error);

/**
 * @brief Gets the service name
 *
 * This function gets the name for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the name is desired
 *
 * @return The name of the services on success, NULL on failure.
 */
const char *sol_netctl_service_get_name(
    const struct sol_netctl_service *service);

/**
 * @brief Gets the service state
 *
 * This function gets the state for the netctl service,
 * after the service is to give by service monitor.
 *
 * @note SOL_NETCTL_SERVICE_STATE_REMOVE is used to show
 * the service has been removed. When it is notified,
 * the service has been removed in the system.
 *
 * @param service The service struct which the state is desired
 *
 * @return The state of the services on success,
 * SOL_NETCTL_SERVICE_STATE_UNKNOWN on failure.
 */
enum sol_netctl_service_state sol_netctl_service_get_state(
    const struct sol_netctl_service *service);

/**
 * @brief Gets the service security methods
 *
 * This function gets the list of security methods or key management settings.
 * Possible values are "none", "wep", "psk", "ieee8021x" and "wps".
 *
 * @param service The service struct which the security is desired
 *
 * @return A list of security methods for the given service.
 *
 * @note This methods might be only present for WiFi services.
 */
const struct sol_ptr_vector *
    sol_netctl_service_get_security(const struct sol_netctl_service *service);

/**
 * @brief Gets the service error
 *
 * This function gets the error for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the name is desired
 *
 * @return The error during connection or disconnection, NULL if no
 * error occur.
 */
const char *sol_netctl_service_get_error(
    const struct sol_netctl_service *service);

/**
 * @brief Gets the service type
 *
 * This function gets the type for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the type is desired
 *
 * @return The type of the services on success, NULL on failure.
 */
const char *sol_netctl_service_get_type(
    const struct sol_netctl_service *service);

/**
 * @brief Gets the service network address
 *
 * This function gets the network address for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the network address is desired
 * @param link The retrieved content
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_service_get_network_address(
    const struct sol_netctl_service *service, struct sol_network_link **link);

/**
 * @brief Gets the service strength
 *
 * This function gets the service strength for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service strength is
 * desired
 *
 * @return The strength of the services on success, 0 on failure.
 */
int32_t sol_netctl_service_get_strength(
    const struct sol_netctl_service *service);

/**
 * @brief Checks whether the service is favorite or not
 *
 * This function checks if the service is favorite or not for the netctl
 * service, after the service is to give by service monitor.
 *
 * @param service The service struct which the service favorite is
 * desired
 *
 * @return @c true if the service is favorite, otherwise @c false.
 */
bool sol_netctl_service_is_favorite(
    const struct sol_netctl_service *service);

/**
 * @brief Checks whether the service is immutable or not
 *
 * This function checks if the service is immutable or not for the netctl
 * service, after the service is to give by service monitor.
 *
 * @param service The service struct which the service immutable is
 * desired
 *
 * @return @c true if the service is immutable, otherwise @c false.
 */
bool sol_netctl_service_is_immutable(
    const struct sol_netctl_service *service);

/**
 * @brief Checks whether the service is autoconnect or not
 *
 * This function checks if the service is autoconnect or not for the netctl
 * service, after the service is to give by service monitor.
 *
 * @param service The service struct which the service autoconnect is
 * desired
 *
 * @return @c true if the service is autoconnect, otherwise @c false.
 */
bool sol_netctl_service_is_autoconnect(
    const struct sol_netctl_service *service);

/**
 * @brief Checks whether the service is roaming or not
 *
 * This function checks if the service is autoconnect or not for the netctl
 * service, after the service is to give by service monitor.
 *
 * @param service The service struct which the service roaming is
 * desired
 *
 * @return @c true if the service is roaming, otherwise @c false.
 */
bool sol_netctl_service_is_roaming(
    const struct sol_netctl_service *service);

/**
 * @brief Gets the service nameservers
 *
 * This function gets the service nameservers for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service nameservers is
 * desired
 *
 * @return The retrieved content on success, NULL on failure.
 */
const struct sol_ptr_vector *
sol_netctl_service_get_nameservers(const struct sol_netctl_service *service);

/**
 * @brief Gets the service timeservers
 *
 * This function gets the service timeservers for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service timeservers is
 * desired
 *
 * @return The retrieved content on success, NULL on failure.
 */
const struct sol_ptr_vector *
sol_netctl_service_get_timeservers(const struct sol_netctl_service *service);

/**
 * @brief Gets the service domains
 *
 * This function gets the service domains for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service domains is
 * desired
 *
 * @return The retrieved content on success, NULL on failure.
 */
const struct sol_ptr_vector *
sol_netctl_service_get_domains(const struct sol_netctl_service *service);

/**
 * @brief Gets the service proxy
 *
 * This function gets the service proxy for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service proxy is
 * desired
 *
 * @return sol_netctl_proxy struct on success, NULL on failure.
 */
const struct sol_netctl_proxy *
sol_netctl_service_get_proxy(const struct sol_netctl_service *service);

/**
 * @brief Gets the service provider
 *
 * This function gets the service provider for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service provider is
 * desired
 *
 * @return sol_netctl_provider struct on success, NULL on failure.
 */
const struct sol_netctl_provider *
sol_netctl_service_get_provider(const struct sol_netctl_service *service);

/**
 * @brief Gets the service ethernet
 *
 * This function gets the service ethernet for the netctl service,
 * after the service is to give by service monitor.
 *
 * @param service The service struct which the service ethernet is
 * desired
 *
 * @return sol_netctl_ethernet struct on success, NULL on failure.
 */
const struct sol_netctl_ethernet *
sol_netctl_service_get_ethernet(const struct sol_netctl_service *service);

/**
 * @brief Gets the global connection state of system
 *
 * This function gets the global connection state for system,
 * after the state is to give by manager monitor.
 * This callback of sol_netctl_add_manager_monitor must be
 * added BEFORE sol_netctl_get_state() is called
 * to ensure no messages are lost.
 *
 * @return The state of the global connection state on success,
 * SOL_NETCTL_STATE_UNKNOWN on failure
 */
enum sol_netctl_state sol_netctl_get_state(void);

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
int sol_netctl_set_radios_offline(bool enabled);

/**
 * @brief Gets the global connection state of offline
 *
 * This function gets the global connection state of offline,
 * after the state is to give by manager monitor.
 * This callback of sol_netctl_add_manager_monitor must be
 * added BEFORE sol_netctl_get_radios_offline() is called
 * to ensure no messages are lost.
 *
 * @return @c true if the offline is enabled, otherwise @c false.
 */
bool sol_netctl_get_radios_offline(void);

/**
 * @brief Connect the service
 *
 * This function connects the service, after the service is to give by service monitor.
 * Since the netctl function is asynchronous, the return is not connection error,
 * but just some dispatching/immediate error. The actual state change will be notified
 * via sol_netctl_add_service_monitor() callbacks. The service connect error info will be
 * notified by error monitor. This callbacks of sol_netctl_add_service_monitor and
 * sol_netctl_add_error_monitor must be added BEFORE the sol_netctl_service_connect()
 * is called to ensure no messages are lost.
 *
 * @param service The service struct which the connection is desired
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_service_connect(struct sol_netctl_service *service);

/**
 * @brief Disconnect the service
 *
 * This function disconnects the service.
 * Since the netctl function is asynchronous, the return is not disconnection error,
 * but just some dispatching/immediate error. The actual state change will be notified
 * via sol_netctl_add_service_monitor() callbacks.The service disconnect error info
 * will be notified by error monitor. This callbacks of sol_netctl_add_service_monitor and
 * sol_netctl_add_error_monitor must be added BEFORE the sol_netctl_service_disconnect()
 * is called to ensure no messages are lost.
 *
 * @param service The service struct which the disconnection is desired
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_service_disconnect(struct sol_netctl_service *service);

/**
 * @brief Adds a monitor for the updated netctl services
 *
 * To monitor the property of services, it gives information
 * about the services. The callback is used to provide the information.
 * sol_netctl_add_service_monitor will trigger all required activations.
 * This callback must be added BEFORE the all functions are called to
 * ensure connection manager has been initialized.
 *
 * @param cb monitor Callback to be called when the services are updated
 * @param data Add a user data per callback
 *
 * @return 1 on the first time success, 0 on success, -errno on failure.
 */
int sol_netctl_add_service_monitor(sol_netctl_service_monitor_cb cb,
    const void *data);

/**
 * @brief Dels a monitor for the updated netctl services
 *
 * Removes the monitor for updated netctl services.
 * sol_netctl_del_service_monitor will trigger all required activations.
 * This callback must be added AFTER the all functions are called to
 * ensure connection manager has been shutdown.
 *
 * @param cb monitor Callback to be called when the services are updated
 * @param data Add a user data per callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_del_service_monitor(sol_netctl_service_monitor_cb cb,
    const void *data);

/**
 * @brief Adds a monitor for the updated netctl manager properties
 *
 * To monitor (State, Offline...), a single monitor can be done for
 * "something changed", and then the new value can be get.
 * sol_netctl_add_manager_monitor will trigger all required activations.
 * This callback must be added BEFORE sol_netctl_get_state and
 * sol_netctl_get_radios_offline.
 *
 * @param cb monitor Callback to be called when the manager are updated
 * @param data Add a user data per callback
 *
 * @return 1 on the first time success, 0 on success, -errno on failure.
 */
int sol_netctl_add_manager_monitor(sol_netctl_manager_monitor_cb cb,
    const void *data);

/**
 * @brief Dels a monitor for the updated netctl manager properties
 *
 * Removes the monitor for updated netctl manager properties.
 * sol_netctl_del_manager_monitor will trigger all required activations.
 *
 * @param cb monitor Callback to be called when the manager are updated
 * @param data Add a user data per callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_del_manager_monitor(sol_netctl_manager_monitor_cb cb,
    const void *data);

/**
 * @brief Adds a monitor for the call error happens
 *
 * To monitor the call error happens, a single monitor can be done for
 * error happens, and then the call result can be get.
 * sol_netctl_add_error_monitor will call trigger all required activations.
 * This callback must be added BEFORE sol_netctl_service_connect and
 * sol_netctl_service_disconnect.
 *
 * @param cb monitor Callback to be called when the call result are updated
 * @param data Add a user data per callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_add_error_monitor(sol_netctl_error_monitor_cb cb,
    const void *data);

/**
 * @brief Dels a monitor for the call error happens
 *
 * Removes the monitor for updated the call error happens.
 * sol_netctl_del_error_monitor will trigger all required activations.
 *
 * @param cb monitor Callback to be called when the call result are updated
 * @param data Add a user data per callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_del_error_monitor(sol_netctl_error_monitor_cb cb,
    const void *data);
/**
 * @brief Gets the netctl services
 *
 * The netctl services vector can be given via sol_netctl_get_services.
 * The vector is the last-known and more may be added/removed dynamically,
 * then the pattern is to add a service monitor BEFORE calling the function.
 *
 * @return The retrieved content on success, NULL on failure.
 */
const struct sol_ptr_vector *sol_netctl_get_services(void);

/**
 * @brief Converts a string service state to sol_netctl_service_state.
 *
 * This function converts a string service state to enumeration sol_netctl_service_state.
 *
 * @see sol_netctl_service_state_from_str().
 *
 * @param state Valid values are "unknown", "idle", "association", "configuration", "ready",
 * "online", "disconnect", "failure", "remove".
 *
 * @return enumeration sol_netctl_service_state.
 */
enum sol_netctl_service_state sol_netctl_service_state_from_str(const char *state)
#ifndef DOXYGEN_RUN
SOL_ATTR_WARN_UNUSED_RESULT
#endif
;

/**
 * @brief Converts sol_netctl_service_state to a string name.
 *
 * This function converts sol_netctl_service_state enumeration to a string service state.
 *
 * @see sol_netctl_service_state_to_str().
 *
 * @param state sol_netctl_service_state.
 *
 * @return String representation of the sol_netctl_service_state..
 */
const char *sol_netctl_service_state_to_str(enum sol_netctl_service_state state)
#ifndef DOXYGEN_RUN
SOL_ATTR_WARN_UNUSED_RESULT
#endif
;

/**
 * @brief get so_netctl_service struct from serivce name.
 *
 * This function return sol_netctl_service struct by a string service name.
 *
 * @see sol_netctl_find_service_by_name.
 *
 * @param service_name Pointer to service name
 *
 * @return data struct of the sol_netctl_service.
 */
static inline struct sol_netctl_service *
sol_netctl_find_service_by_name(const char *service_name)
{
    const struct sol_ptr_vector *service_list;
    struct sol_netctl_service *service;
    const char *name;
    uint16_t i;

    service_list = sol_netctl_get_services();
    if (!service_list)
        return NULL;

    SOL_PTR_VECTOR_FOREACH_IDX (service_list, service, i) {
        name = sol_netctl_service_get_name(service);
        if (name && strcmp(name, service_name) == 0)
            return service;
    }
    return NULL;
}

/**
 * @brief register a agent for network connection
 *
 * A single agent is registered for an application that registering a new agent.
 *
 * @see struct sol_netctl_agent
 *
 * @param agent the agent Callback struct to be called when the related information is updated.
 * @param data Add a user data per callback.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_register_agent(const struct sol_netctl_agent *agent, const void *data);

/**
 * @brief unregister a agent for network connection
 *
 * @see sol_netctl_register_agent
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_unregister_agent(void);

/**
 * @brief request retry the connection or not when error type is reported
 *
 * Request retry the network connection or not when error type is reported.
 * When the network connection failure, the user can select retry the connection or
 * not retry it. The function can be used to select it.
 * If retry connection is selected, the failure network connection will be tried to
 * connect. If not retry connection is selected, the failure network connection will
 * not be tried to connect.
 * The connection failure information is informed via the agent callback.
 * The agent must be registered before using sol_netctl_request_retry.
 *
 * @see sol_netctl_register_agent
 *
 * @param service The service struct which the network connection is desired.
 * @param retry True is retry the failure selected network connection,
 * false is not retry the failure selected network connection.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_request_retry(struct sol_netctl_service *service,
    bool retry);

/**
 * @brief request the login input to connection
 *
 * Request the login input for network connection.
 * When the login information is needed in the process of network connection,
 * the funcation can be used to input the login information for network connection.
 * The login input information is informed via the agent callback.
 * The agent must be registered before using sol_netctl_request_input.
 *
 * @see sol_netctl_register_agent
 *
 * @param service The service struct which the network connection is desired.
 * @param inputs the ptr vector of input types and inputs from the user.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_request_input(struct sol_netctl_service *service,
    const struct sol_ptr_vector *inputs);

/**
 * @brief request scan the surrounding devices
 *
 * Request scan for the surrounding devices.
 * This must be inovked AFTER sol_netctl_add_service_monitor.
 *
 * @see sol_netctl_add_service_monitor
 *
 * @return 0 on success, -errno on failure.
 */
int sol_netctl_scan(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
