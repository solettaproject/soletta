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

#include <errno.h>
#include <mosquitto.h>
#include <stdbool.h>

#define SOL_LOG_DOMAIN &_sol_mqtt_log_domain
#include <sol-log-internal.h>
#include <sol-mainloop.h>
#include <sol-util.h>

#include "sol-mqtt.h"

SOL_LOG_INTERNAL_DECLARE(_sol_mqtt_log_domain, "mqtt");

static int init_ref;
#define CHECK_INIT(ret) \
    do { \
        if (unlikely(init_ref < 1)) { \
            SOL_WRN("sol-mqtt used before initialization"); \
            return ret; \
        } \
    } while (0)

#define MQTT_CHECK_API(ptr, ...) \
    do {                                        \
        if (unlikely(ptr->api_version != \
            SOL_MQTT_CONFIG_API_VERSION)) { \
            SOL_WRN("Couldn't handle mqtt handler that has unsupported " \
                "version '%u', expected version is '%u'", \
                ptr->api_version, SOL_MQTT_CONFIG_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)

struct sol_mqtt {
    struct mosquitto *mosq;

    struct sol_fd *socket_read;
    struct sol_fd *socket_write;

    struct sol_timeout *connect_timeout;
    struct sol_timeout *disconnect_timeout;
    struct sol_timeout *publish_timeout;
    struct sol_timeout *subscribe_timeout;
    struct sol_timeout *unsubscribe_timeout;
    struct sol_timeout *message_timeout;

    const struct sol_mqtt_config *config;

    void *data;

    int socket_fd;

    int connection_status;

    time_t keepalive;
};

struct sol_mqtt_message {
    char *topic;
    void *payload;
    size_t payload_len;
    int id;
    sol_mqtt_qos qos;
    bool retain;
};

static void
sol_mqtt_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    init_ref++;
    mosquitto_lib_init();
}

static void
sol_mqtt_shutdown(void)
{
    init_ref--;

    if (init_ref == 0)
        mosquitto_lib_cleanup();
}

static bool
sol_mqtt_event_loop(void *data, int fd, unsigned int active_flags)
{
    struct sol_mqtt *mqtt = data;
    int r;

    r = mosquitto_loop_read(mqtt->mosq, 1);
    r |= mosquitto_loop_write(mqtt->mosq, 1);
    r |= mosquitto_loop_misc(mqtt->mosq);

    if (r != 0) {
        SOL_WRN("Unable to perform mqtt socket operation");
        goto remove;
    }

    /* No more data to write, remove SOL_FD_FLAGS_OUT event watcher */
    if (active_flags & SOL_FD_FLAGS_OUT && !mosquitto_want_write(mqtt->mosq))
        goto remove;

    return true;

remove:
    if (active_flags & SOL_FD_FLAGS_OUT)
        mqtt->socket_write = NULL;
    else
        mqtt->socket_read = NULL;

    return false;
}

/*
 * When mosquitto calls the a user provided callback, it's internal lock is held
 * so we need to go back to solleta mainloop before calling our user callback
 * in order to prevent a deadlock.
 */

#define CALLBACK_WRAPPER(_cb) \
    static bool \
    sol_mqtt_on_ ## _cb ## _wrapper(void * data) \
    { \
        struct sol_mqtt *mqtt = data; \
        SOL_NULL_CHECK(mqtt, false); \
        mqtt->_cb ## _timeout = NULL; \
        if (mqtt->config->_cb) \
            mqtt->config->_cb(mqtt, mqtt->data); \
        return false; \
    }

CALLBACK_WRAPPER(connect);
CALLBACK_WRAPPER(disconnect);
CALLBACK_WRAPPER(publish);
CALLBACK_WRAPPER(subscribe);
CALLBACK_WRAPPER(unsubscribe);

static void
sol_mqtt_on_connect(struct mosquitto *mosq, void *data, int rc)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    mqtt->connection_status = rc;

    if (mqtt->connect_timeout || !mqtt->config || !mqtt->config->connect)
        return;

    mqtt->connect_timeout = sol_timeout_add(0, sol_mqtt_on_connect_wrapper, mqtt);
}

static void
sol_mqtt_on_disconnect(struct mosquitto *mosq, void *data, int rc)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    mqtt->connection_status = SOL_MQTT_DISCONNECTED;

    if (mqtt->disconnect_timeout || !mqtt->config || !mqtt->config->disconnect)
        return;

    mqtt->disconnect_timeout = sol_timeout_add(0, sol_mqtt_on_disconnect_wrapper, mqtt);
}

static void
sol_mqtt_on_publish(struct mosquitto *mosq, void *data, int id)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    if (mqtt->publish_timeout || !mqtt->config || !mqtt->config->publish)
        return;

    mqtt->publish_timeout = sol_timeout_add(0, sol_mqtt_on_publish_wrapper, mqtt);
}

struct message_wrapper_data {
    struct sol_mqtt *mqtt;
    struct sol_mqtt_message *message;
};

static bool
sol_mqtt_on_message_wrapper(void *data)
{
    struct message_wrapper_data *wrapper_data = data;
    struct sol_mqtt *mqtt = wrapper_data->mqtt;
    struct sol_mqtt_message *message = wrapper_data->message;

    SOL_NULL_CHECK_GOTO(mqtt, end);
    SOL_NULL_CHECK_GOTO(message, end);

    if (mqtt->config && mqtt->config->message)
        mqtt->config->message(mqtt, message, mqtt->data);

end:
    free(wrapper_data);

    sol_mqtt_message_free(message);

    return false;
}

static void
sol_mqtt_on_message(struct mosquitto *mosq, void *data, const struct mosquitto_message *m_message)
{
    struct sol_mqtt *mqtt = data;
    struct sol_mqtt_message *message;
    struct message_wrapper_data *wrapper_data;

    SOL_NULL_CHECK(mqtt);
    SOL_NULL_CHECK(m_message);

    if (!mqtt->config || !mqtt->config->message)
        return;

    wrapper_data = malloc(sizeof(*wrapper_data));
    SOL_NULL_CHECK(wrapper_data);

    message = sol_mqtt_message_new(m_message->topic, strlen(m_message->topic),
            m_message->payload, m_message->payloadlen, (sol_mqtt_qos) m_message->qos, m_message->retain);
    SOL_NULL_CHECK_GOTO(message, error);

    message->id = m_message->mid;

    wrapper_data->mqtt = mqtt;
    wrapper_data->message = message;

    mqtt->message_timeout = sol_timeout_add(0, sol_mqtt_on_message_wrapper, wrapper_data);

    return;

error:
    free(wrapper_data);
}

static void
sol_mqtt_on_subscribe(struct mosquitto *mosq, void *data, int id, int qos_count, const int *granted_qos)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    if (qos_count == 0) {
        SOL_WRN("Unable to subscribe");
        return;
    }

    if (mqtt->subscribe_timeout || !mqtt->config || !mqtt->config->subscribe)
        return;

    mqtt->subscribe_timeout = sol_timeout_add(0, sol_mqtt_on_subscribe_wrapper, mqtt);
}

static void
sol_mqtt_on_unsubscribe(struct mosquitto *mosq, void *data, int id)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    if (mqtt->unsubscribe_timeout || !mqtt->config || !mqtt->config->unsubscribe)
        return;

    mqtt->unsubscribe_timeout = sol_timeout_add(0, sol_mqtt_on_unsubscribe_wrapper, mqtt);
}

SOL_API struct sol_mqtt *
sol_mqtt_connect(const char *host, int port, const struct sol_mqtt_config *config, void *data)
{
    struct sol_mqtt *mqtt;
    int r;

    SOL_NULL_CHECK(host, NULL);
    SOL_NULL_CHECK(config, NULL);
    MQTT_CHECK_API(config, NULL);

    if (config->client_id == NULL && config->clean_session == false) {
        SOL_WRN("client_id is NULL but clean_session is set to false.");
        return NULL;
    }

    sol_mqtt_init();

    mqtt = calloc(1, sizeof(*mqtt));
    SOL_NULL_CHECK(mqtt, NULL);

    mqtt->mosq = mosquitto_new(config->client_id, config->clean_session, mqtt);
    SOL_NULL_CHECK_GOTO(mqtt->mosq, error);

    mqtt->config = config;
    mqtt->data = data;

    mosquitto_connect_callback_set(mqtt->mosq, sol_mqtt_on_connect);
    mosquitto_disconnect_callback_set(mqtt->mosq, sol_mqtt_on_disconnect);
    mosquitto_publish_callback_set(mqtt->mosq, sol_mqtt_on_publish);
    mosquitto_message_callback_set(mqtt->mosq, sol_mqtt_on_message);
    mosquitto_subscribe_callback_set(mqtt->mosq, sol_mqtt_on_subscribe);
    mosquitto_unsubscribe_callback_set(mqtt->mosq, sol_mqtt_on_unsubscribe);

    if (config->will) {
        r = mosquitto_will_set(mqtt->mosq, config->will->topic, config->will->payload_len,
                config->will->payload, (int) config->will->qos, config->will->retain);

        if (r != MOSQ_ERR_SUCCESS) {
            SOL_WRN("Unable to set will message");
            return NULL;
        }
    }

    mqtt->connection_status = SOL_MQTT_DISCONNECTED;

    r = mosquitto_connect_async(mqtt->mosq, host, port, mqtt->keepalive);
    if (r != MOSQ_ERR_SUCCESS)
        SOL_WRN("Unable to connect to %s:%d", host, port);

    mqtt->socket_fd = mosquitto_socket(mqtt->mosq);
    if (mqtt->socket_fd == -1) {
        SOL_WRN("Unable to get socket file descriptor");
        goto socket_error;
    }

    mqtt->socket_read = sol_fd_add(mqtt->socket_fd, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_PRI,
            sol_mqtt_event_loop, mqtt);
    SOL_NULL_CHECK(mqtt->socket_read, NULL);

    return mqtt;

socket_error:
    mosquitto_destroy(mqtt->mosq);
error:
    free(mqtt);
    return NULL;
}

SOL_API int
sol_mqtt_reconnect(struct sol_mqtt *mqtt)
{
    int r;

    SOL_NULL_CHECK(mqtt, -EINVAL);

    r = mosquitto_reconnect_async(mqtt->mosq);

    if (r != MOSQ_ERR_SUCCESS) {
        SOL_WRN("Unable to reconnect");
        return -EINVAL;
    }

    if (mqtt->socket_read)
        sol_fd_del(mqtt->socket_read);
    if (mqtt->socket_write)
        sol_fd_del(mqtt->socket_write);

    mqtt->socket_read = NULL;
    mqtt->socket_write = NULL;

    mqtt->socket_fd = mosquitto_socket(mqtt->mosq);
    if (mqtt->socket_fd == -1) {
        SOL_WRN("Unable to get socket file descriptor");
        return -EINVAL;
    }

    mqtt->socket_read = sol_fd_add(mqtt->socket_fd, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_PRI,
            sol_mqtt_event_loop, mqtt);

    return 0;
}

SOL_API void
sol_mqtt_disconnect(struct sol_mqtt *mqtt)
{
    CHECK_INIT();
    SOL_NULL_CHECK(mqtt);

    if (mqtt->socket_read)
        sol_fd_del(mqtt->socket_read);
    if (mqtt->socket_write)
        sol_fd_del(mqtt->socket_write);

    if (mqtt->connect_timeout)
        sol_timeout_del(mqtt->connect_timeout);
    if (mqtt->disconnect_timeout)
        sol_timeout_del(mqtt->disconnect_timeout);
    if (mqtt->publish_timeout)
        sol_timeout_del(mqtt->publish_timeout);
    if (mqtt->message_timeout)
        sol_timeout_del(mqtt->message_timeout);
    if (mqtt->subscribe_timeout)
        sol_timeout_del(mqtt->subscribe_timeout);
    if (mqtt->unsubscribe_timeout)
        sol_timeout_del(mqtt->unsubscribe_timeout);

    mosquitto_disconnect_callback_set(mqtt->mosq, NULL);
    mosquitto_disconnect(mqtt->mosq);

    mosquitto_destroy(mqtt->mosq);
    free(mqtt);

    sol_mqtt_shutdown();
}

SOL_API int
sol_mqtt_get_connection_status(struct sol_mqtt *mqtt)
{
    SOL_NULL_CHECK(mqtt, -EINVAL);
    return mqtt->connection_status;
}

SOL_API int
sol_mqtt_publish(struct sol_mqtt *mqtt, struct sol_mqtt_message *message)
{
    int r;

    CHECK_INIT(-EINVAL);
    SOL_NULL_CHECK(mqtt, -EINVAL);
    SOL_NULL_CHECK(message, -EINVAL);

    r = mosquitto_publish(mqtt->mosq, &message->id, message->topic, message->payload_len, message->payload,
            (int) message->qos, message->retain);

    if (r != MOSQ_ERR_SUCCESS) {
        SOL_WRN("Unable to publish to '%s'", message->topic);
        return -EINVAL;
    }

    if (!mqtt->socket_write) {
        mqtt->socket_write = sol_fd_add(mqtt->socket_fd, SOL_FD_FLAGS_OUT, sol_mqtt_event_loop, mqtt);
        SOL_NULL_CHECK(mqtt->socket_write, -EINVAL);
    }

    return 0;
}

SOL_API int
sol_mqtt_subscribe(struct sol_mqtt *mqtt, char *topic, size_t topic_len, sol_mqtt_qos qos)
{
    int r;
    char *s_topic;

    CHECK_INIT(-EINVAL);
    SOL_NULL_CHECK(mqtt, -EINVAL);
    SOL_NULL_CHECK(topic, -EINVAL);

    s_topic = strndupa(topic, topic_len);

    r = mosquitto_subscribe(mqtt->mosq, NULL, s_topic, (int) qos);
    if (r != MOSQ_ERR_SUCCESS) {
        SOL_WRN("Unable to subscribe to '%s'", topic);
        return -EINVAL;
    }

    return 0;
}

SOL_API struct sol_mqtt_message *
sol_mqtt_message_new(char *topic, size_t topic_len, void *payload, size_t payload_len,
        sol_mqtt_qos qos, bool retain)
{
    struct sol_mqtt_message *message;

    SOL_NULL_CHECK(topic, NULL);
    SOL_NULL_CHECK(payload, NULL);

    message = calloc(1, sizeof(struct sol_mqtt_message));
    SOL_NULL_CHECK(message, NULL);

    message->topic = calloc(1, topic_len + 1);
    SOL_NULL_CHECK_GOTO(message->topic, topic_error);
    memcpy(message->topic, topic, topic_len);

    message->payload = sol_util_memdup(payload, payload_len);
    SOL_NULL_CHECK_GOTO(message->topic, payload_error);

    message->payload_len = payload_len;

    message->qos = qos;
    message->retain = retain;

    return message;

payload_error:
    free(message->topic);
topic_error:
    free(message);

    return NULL;
}

SOL_API struct sol_mqtt_message *
sol_mqtt_message_copy(struct sol_mqtt_message *message)
{
    struct sol_mqtt_message *m_copy;
    SOL_NULL_CHECK(message, NULL);

    m_copy = sol_mqtt_message_new(message->topic, strlen(message->topic), message->payload,
            message->payload_len, message->qos, message->retain);

    SOL_NULL_CHECK(m_copy, NULL);

    m_copy->id = message->id;

    return m_copy;
}

SOL_API void
sol_mqtt_message_free(struct sol_mqtt_message *message) {
    if (!message)
        return;

    free(message->topic);
    free(message->payload);
    free(message);
}

SOL_API size_t
sol_mqtt_get_topic(const struct sol_mqtt_message *message, char *buf, size_t size)
{
    size_t s;

    SOL_NULL_CHECK(message, 0);
    SOL_NULL_CHECK(buf, 0);

    s = strlen(message->topic);
    if (s > size)
        s = size;

    strncpy(buf, message->topic, s);

    return s;
}

SOL_API int
sol_mqtt_get_payload(const struct sol_mqtt_message *message, void **payload, size_t *len)
{
    SOL_NULL_CHECK(message, -EINVAL);
    SOL_NULL_CHECK(payload, -EINVAL);
    SOL_NULL_CHECK(len, -EINVAL);

    *payload = message->payload;
    *len = message->payload_len;

    return 0;
}
