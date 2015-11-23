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

#include <sol-buffer.h>
#include <sol-certificate.h>
#include <sol-common-buildopts.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle MQTT protocol.
 *
 * Wrapper library for MQTT communication using the mosquitto MQTT
 * library.
 */

/**
 * @defgroup MQTT
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
     * Succesfully conencted to the broker
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
     * Broker anavailable at provided host
     */
    SOL_MQTT_UNAVAILABLE = 3,
};

/**
 * @struct sol_mqtt
 *
 * @brief MQTT Object
 *
 * @see sol_mqtt_connect
 *
 * This object is the abstraction of a MQTT session. This is the base
 * structure for all MQTT operations and is obtained through the
 * sol_mqtt_connect API.
 */
struct sol_mqtt;

/**
 * @struct sol_mqtt_message
 *
 * @brief MQTT Message
 *
 * This object is the abstraction of a MQTT message and is the base for
 * publishing and receiving data to/from the broker.
 */
struct sol_mqtt_message {
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
};

/**
 * @struct sol_mqtt_handlers
 *
 * @brief MQTT callback handlers
 */
struct sol_mqtt_handlers {
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
    void (*connect) (void *data, struct sol_mqtt *mqtt);

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
};

/**
 * @struct sol_mqtt_config
 *
 * @brief Server Configuration
 */
struct sol_mqtt_config {
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
     * Time interval between PING messages that should be sent by the
     * broker to the client.
     */
    time_t keepalive;

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
     * Handlers to be used with this connection
     */
    const struct sol_mqtt_handlers handlers;
};

/**
 * @brief Connect to a MQTT broker
 *
 * @param host The host address of the MQTT broker
 * @param port The host port to connect to
 * @param config Configuration and callbacks
 * @param data User data provided to the callbacks
 *
 * @return New mqtt object on sucess, NULL otherwise
 */
struct sol_mqtt *sol_mqtt_connect(const char *host, uint16_t port, const struct sol_mqtt_config *config, void *data);

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
 * @param MQTT Object
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

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
