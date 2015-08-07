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
struct sol_mqtt_message;

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
 * @struct sol_mqtt_config
 *
 * @brief Server Configuration and callback handlers
 */
struct sol_mqtt_config {
#define SOL_MQTT_CONFIG_API_VERSION (1)
    /**
     * Should always be set to SOL_MQTT_CONFIG_API_VERSION
     */
    uint16_t api_version;

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
    char *client_id;

    /**
     * A message that the broker should send when the client disconnects.
     */
    struct sol_mqtt_message *will;

    /**
     * @brief On connect callback
     *
     * @param mqtt MQTT Object
     * @param data User provided data
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a connect request has been processed
     */
    void (*connect) (struct sol_mqtt *mqtt, void *data);

    /**
     * @brief On disconnect callback
     *
     * @param mqtt MQTT Object
     * @param data User provided data
     *
     * @see sol_mqtt_connect
     *
     * Callback called when the client has disconnected from the
     * broker.
     */
    void (*disconnect) (struct sol_mqtt *mqtt, void *data);

    /**
     * @brief On publish callback
     *
     * @param mqtt MQTT Object
     * @param data User provided data
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a publish request has been processed.
     */
    void (*publish) (struct sol_mqtt *mqtt, void *data);

    /**
     * @brief On message callback
     *
     * @param mqtt MQTT Object
     * @param message Message received from the broker
     * @param data User provided data
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
    void (*message) (struct sol_mqtt *mqtt, const struct sol_mqtt_message *message, void *data);

    /**
     * @brief On subscribe callback
     *
     * @param mqtt MQTT Object
     * @param data User provided data
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a subscribe request has been processed.
     */
    void (*subscribe) (struct sol_mqtt *mqtt, void *data);

    /**
     * @brief On unsubscribe callback
     *
     * @param mqtt MQTT Object
     * @param data User provided data
     *
     * @see sol_mqtt_connect
     *
     * Callback called when a unsubscribe request has been processed.
     */
    void (*unsubscribe) (struct sol_mqtt *mqtt, void *data);
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
struct sol_mqtt *sol_mqtt_connect(const char *host, int port, const struct sol_mqtt_config *config, void *data);

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
int sol_mqtt_get_connection_status(struct sol_mqtt *mqtt);

/**
 * @brief Send the Broker a message to be published in a given topic
 *
 * @param mqtt MQTT Object
 * @param message Message to be published. The memory associated to this object should be handled by the caller.
 *
 * @return 0 on success, -EINVAL otherwise
 */
int sol_mqtt_publish(struct sol_mqtt *mqtt, struct sol_mqtt_message *message);

/**
 * @brief Ask the Broker to be subscribed to a given topic
 *
 * @params mqtt MQTT Object
 * @params topic Topic to subscribe
 * @param topic_len Topic length in bytes
 * @param qos MQTT QOS that should be used by the subscribe message
 *
 * @return 0 on success, -EINVAL otherwise
 */
int sol_mqtt_subscribe(struct sol_mqtt *mqtt, char *topic, size_t topic_len, sol_mqtt_qos qos);

/**
 * @brief Create a new MQTT message
 *
 * @param topic Topic where the message will be published
 * @param topic_len Length in bytes of the topic
 * @param payload Payload of the message
 * @param payload_len Length in bytes of the payload
 * @param qos MQTT QOS that should be used when sending the message
 * @param retain If the message should be retained by the broker
 *
 * @return New message object on success, NULL otherwise
 */
struct sol_mqtt_message *sol_mqtt_message_new(char *topic, size_t topic_len, void *payload,
        size_t payload_len, sol_mqtt_qos, bool retain);

/**
 * @brief Free the memory associated to a given message
 *
 * @param message MQTT message to be copied
 *
 * @return Copy of message on success, NULL otherwise
 */
struct sol_mqtt_message *sol_mqtt_message_copy(struct sol_mqtt_message *message);

/**
 * @brief Free the memory associated to a given message
 *
 * @param message MQTT message to be freed
 */
void sol_mqtt_message_free(struct sol_mqtt_message *message);

/**
 * @brief Get the topic of a given MQTT message
 *
 * @param message MQTT message to get the topic from
 * @param buf Buffer to write the topic to
 * @param size Size in bytes of buf
 *
 * @return Number of bytes written to buf
 */
size_t sol_mqtt_get_topic(const struct sol_mqtt_message *message, char *buf, size_t size);

/**
 * @brief Get address and size of the message payload
 *
 * @param payload Upon return, will be set to the address of the payload memory
 * @param len Upon return, will be set to the size in bytes of the payload memory.
 *
 * @return 0 on success, -EINVAL otherwise
 */
int sol_mqtt_get_payload(const struct sol_mqtt_message *message, void **payload, size_t *len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
