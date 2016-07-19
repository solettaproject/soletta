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

#include <sol-buffer.h>
#include <sol-certificate.h>
#include <sol-common-buildopts.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle the MQTT protocol.
 *
 * Wrapper library for MQTT communication using the mosquitto MQTT
 * library.
 */

/**
 * @defgroup MQTT MQTT
 * @ingroup Comms
 *
 * MQTT is a machine-to-machine (M2M)/"Internet of Things" connectivity
 * protocol. It was designed as an extremely lightweight
 * publish/subscribe messaging transport. It is useful for connections
 * with remote locations where a small code footprint is required
 * and/or network bandwidth is at a premium. For example, it has been
 * used in sensors communicating to a broker via satellite link, over
 * occasional dial-up connections with healthcare providers, and in a
 * range of home automation and small device scenarios. It is also ideal
 * for mobile applications because of its small size, low power usage,
 * minimised data packets, and efficient distribution of information to
 * one or many receivers
 *
 * @{
 */

/**
 * @typedef sol_mqtt_qos
 *
 * @brief MQTT QOS level for message delivery
 */
typedef enum {
    /**
     * The message is delivered according to the capabilities of the
     * underlying network. No response is sent by the receiver and no
     * retry is performed by the sender. The message arrives at the
     * receiver either once or not at all.
     */
    SOL_MQTT_QOS_AT_MOST_ONCE = 0,

    /**
     * This quality of service ensures that the message arrives at the
     * receiver at least once. A QoS 1 PUBLISH Packet has a Packet
     * Identifier in its variable header and is acknowledged by a PUBACK
     * Packet.
     */
    SOL_MQTT_QOS_AT_LEAST_ONCE = 1,

    /**
     * This is the highest quality of service, for use when neither
     * loss nor duplication of messages are acceptable. There is an
     * increased overhead associated with this quality of service.
     */
    SOL_MQTT_QOS_EXACTLY_ONCE = 2,
} sol_mqtt_qos;

/**
 * @enum sol_mqtt_conn_status
 *
 * @brief Connection status
 */
enum sol_mqtt_conn_status {
    /**
     * Disconnected due to unexpected reasons.
     */
    SOL_MQTT_DISCONNECTED = -1,

    /**
     * Successfully connected to the broker
     */
    SOL_MQTT_CONNECTED = 0,

    /**
     * MQTT protocol rejected by the broker
     */
    SOL_MQTT_WRONG_PROTOCOL = 1,

    /**
     * Client ID rejected by the broker
     */
    SOL_MQTT_ID_REJECTED = 2,

    /**
     * Broker unavailable at provided host
     */
    SOL_MQTT_UNAVAILABLE = 3,
};

/**
 * @typedef sol_mqtt
 *
 * @brief MQTT Object
 *
 * @see sol_mqtt_connect
 *
 * This object is the abstraction of a MQTT session. This is the base
 * structure for all MQTT operations and is obtained through the
 * sol_mqtt_connect() API.
 */
struct sol_mqtt;
typedef struct sol_mqtt sol_mqtt;

/**
 * @brief MQTT Message
 *
 * This object is the abstraction of a MQTT message and is the base for
 * publishing and receiving data to/from the broker.
 */
typedef struct sol_mqtt_message {
#ifndef SOL_NO_API_VERSION
#define SOL_MQTT_MESSAGE_API_VERSION (1)
    /**
     * Should always be set to SOL_MQTT_MESSAGE_API_VERSION
     */
    uint16_t api_version;
#endif

    /**
     * The topic which the message was/will be posted to
     */
    char *topic;

    /**
     * The message payload
     */
    struct sol_buffer *payload;

    /**
     * The message Id
     */
    int id;

    /**
     * The message Quality of service
     */
    sol_mqtt_qos qos;

    /**
     * If true, the message will be retained by the broker
     */
    bool retain;
} sol_mqtt_message;

/**
 * @brief MQTT callback handlers
 */
typedef struct sol_mqtt_handlers {
#ifndef SOL_NO_API_VERSION
#define SOL_MQTT_HANDLERS_API_VERSION (1)
    /**
     * Should always be set to SOL_MQTT_HANDLERS_API_VERSION
     */
    uint16_t api_version;
#endif

    /**
     * @brief On connect callback
     *
     * @param data User provided data
     * @param mqtt MQTT Object
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a connect request has been processed
     */
    void (*connect)(void *data, struct sol_mqtt *mqtt);

    /**
     * @brief On disconnect callback
     *
     * @param data User provided data
     * @param mqtt MQTT Object
     *
     * @see sol_mqtt_connect
     *
     * Callback called when the client has disconnected from the
     * broker.
     */
    void (*disconnect) (void *data, struct sol_mqtt *mqtt);

    /**
     * @brief On publish callback
     *
     * @param data User provided data
     * @param mqtt MQTT Object
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a publish request has been processed.
     */
    void (*publish) (void *data, struct sol_mqtt *mqtt);

    /**
     * @brief On message callback
     *
     * @param data User provided data
     * @param mqtt MQTT Object
     * @param message Message received from the broker
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a message is received from the broker.
     * This does not include PING messages, only messages incoming due
     * to publish request from other clients or the broker itself.
     *
     * The memory associated to the message object will be freed after
     * the callback returns.
     */
    void (*message) (void *data, struct sol_mqtt *mqtt, const struct sol_mqtt_message *message);

    /**
     * @brief On subscribe callback
     *
     * @param data User provided data
     * @param mqtt MQTT Object
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a subscribe request has been processed.
     */
    void (*subscribe) (void *data, struct sol_mqtt *mqtt);

    /**
     * @brief On unsubscribe callback
     *
     * @param data User provided data
     * @param mqtt MQTT Object
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a unsubscribe request has been processed.
     */
    void (*unsubscribe) (void *data, struct sol_mqtt *mqtt);
} sol_mqtt_handlers;

/**
 * @brief Server Configuration
 */
typedef struct sol_mqtt_config {
#ifndef SOL_NO_API_VERSION
#define SOL_MQTT_CONFIG_API_VERSION (1)
    /**
     * Should always be set to SOL_MQTT_CONFIG_API_VERSION
     */
    uint16_t api_version;
#endif

    /**
     * If set, the broker will drop all messages and subscriptions when
     * the client disconnects. Must be set if no client id is provided.
     */
    bool clean_session;

    /**
     * The host port to connect
     */
    uint16_t port;

    /**
     * Time interval between PING messages that should be sent by the
     * broker to the client in miliseconds.
     */
    time_t keep_alive;

    /**
     * NULL terminated string that should be used as client ID. If
     * not set, clean_session must be set to true.
     */
    const char *client_id;

    /**
     * NULL terminated string with the username
     */
    const char *username;

    /**
     * NULL terminated string with the password
     */
    const char *password;

    /**
     * A message that the broker should send when the client disconnects.
     */
    const struct sol_mqtt_message *will;

    /**
     * CA Certificate for SSL connections
     */
    const struct sol_cert *ca_cert;

    /**
     * Client certificate for SSL connections
     */
    const struct sol_cert *client_cert;

    /**
     * Private key for SSL connections
     */
    const struct sol_cert *private_key;

    /**
     * User data provided to the callbacks
     */
    const void *data;

    /**
     * The host address of the MQTT broker
     */
    const char *host;

    /**
     * Handlers to be used with this connection
     */
    const struct sol_mqtt_handlers handlers;
} sol_mqtt_config;

/**
 * @brief Connect to a MQTT broker
 *
 * @param config Configuration and callbacks
 *
 * @return New mqtt object on success, NULL otherwise
 */
struct sol_mqtt *sol_mqtt_connect(const struct sol_mqtt_config *config);

/**
 * @brief Reestablish the connection to the MQTT broker
 *
 * @param mqtt MQTT Object
 *
 * @return 0 on success, -EINVAL otherwise
 */
int sol_mqtt_reconnect(struct sol_mqtt *mqtt);

/**
 * @brief Disconnect from MQTT Broker
 *
 * @param mqtt MQTT Object;
 *
 * @see sol_mqtt_connect
 *
 * Terminate the connection to the broker and free the resources
 * associated to the mqtt object.
 */
void sol_mqtt_disconnect(struct sol_mqtt *mqtt);

/**
 * @brief Get connection status
 *
 * @param mqtt MQTT Object
 *
 * @see sol_mqtt_conn_status
 *
 * @return sol_mqtt_conn_status code
 */
int sol_mqtt_get_connection_status(const struct sol_mqtt *mqtt);

/**
 * @brief Send the Broker a message to be published in a given topic
 *
 * @param mqtt MQTT Object
 * @param message Message to be published. The memory associated to this object should be handled by the caller.
 *
 * @return 0 on success, -EINVAL otherwise
 */
int sol_mqtt_publish(const struct sol_mqtt *mqtt, struct sol_mqtt_message *message);

/**
 * @brief Ask the Broker to be subscribed to a given topic
 *
 * @param mqtt MQTT Object
 * @param topic Null terminated string with the topic to subscribe to
 * @param qos MQTT QOS that should be used by the subscribe message
 *
 * @return 0 on success, -EINVAL otherwise
 */
int sol_mqtt_subscribe(const struct sol_mqtt *mqtt, const char *topic, sol_mqtt_qos qos);

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @c struct sol_mqtt_message has
 * the expected API version.
 *
 * In case it has wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_MQTT_MESSAGE_CHECK_API_VERSION(msg_, ...) \
    if (SOL_UNLIKELY((msg_)->api_version != \
        SOL_MQTT_MESSAGE_API_VERSION)) { \
        SOL_ERR("Unexpected API version (message is %" PRIu16 ", expected %" PRIu16 ")", \
            (msg_)->api_version, SOL_MQTT_MESSAGE_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_MQTT_MESSAGE_CHECK_API_VERSION(msg_, ...)
#endif

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
