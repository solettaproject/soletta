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

    struct sol_fd *socket_watch;

    struct sol_timeout *connect_timeout;
    struct sol_timeout *disconnect_timeout;
    struct sol_timeout *publish_timeout;
    struct sol_timeout *subscribe_timeout;
    struct sol_timeout *unsubscribe_timeout;
    struct sol_timeout *message_timeout;

    void *data;

    struct sol_mqtt_handlers handlers;

    int socket_fd;

    int connection_status;

    time_t keepalive;
};

static struct sol_mqtt_message *
sol_mqtt_message_new(const char *topic, const struct sol_buffer *payload, sol_mqtt_qos qos, bool retain)
{
    struct sol_mqtt_message *message;

    SOL_NULL_CHECK(topic, NULL);
    SOL_NULL_CHECK(payload, NULL);

    message = calloc(1, sizeof(struct sol_mqtt_message));
    SOL_NULL_CHECK(message, NULL);

    message->topic = sol_util_memdup(topic, strlen(topic) + 1);
    SOL_NULL_CHECK_GOTO(message->topic, topic_error);

    message->payload = sol_buffer_copy(payload);
    SOL_NULL_CHECK_GOTO(message->payload, payload_error);

    message->qos = qos;
    message->retain = retain;

    return message;

payload_error:
    free(message->topic);
topic_error:
    free(message);

    return NULL;
}

static void
sol_mqtt_message_free(struct sol_mqtt_message *message)
{
    if (!message)
        return;

    sol_buffer_fini(message->payload);
    free(message->payload);
    free(message->topic);
    free(message);
}

static void
sol_mqtt_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
    init_ref++;

    if (init_ref == 1)
        mosquitto_lib_init();
}

static void
sol_mqtt_shutdown(void)
{
    SOL_INT_CHECK(init_ref, <= 0);

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

    /* No more data to write, unset SOL_FD_FLAGS_OUT */
    if (active_flags & SOL_FD_FLAGS_OUT && !mosquitto_want_write(mqtt->mosq) &&
        !sol_fd_unset_flags(mqtt->socket_watch, SOL_FD_FLAGS_OUT)) {
        SOL_WRN("Unable to unset SOL_FD_FLAGS_OUT");
        goto remove;
    }

    return true;

remove:
    mqtt->socket_watch = NULL;
    return false;
}

/*
 * When mosquitto calls the a user provided callback, its internal lock is held
 * so we need to go back to solleta mainloop before calling our user callback
 * in order to prevent a deadlock.
 */

#define CALLBACK_WRAPPER(_cb) \
    static bool \
    sol_mqtt_on_ ## _cb ## _wrapper(void *data) \
    { \
        struct sol_mqtt *mqtt = data; \
        SOL_NULL_CHECK(mqtt, false); \
        mqtt->_cb ## _timeout = NULL; \
        if (mqtt->handlers._cb) \
            mqtt->handlers._cb(mqtt->data, mqtt); \
        return false; \
    }

CALLBACK_WRAPPER(connect);
CALLBACK_WRAPPER(disconnect);
CALLBACK_WRAPPER(publish);
CALLBACK_WRAPPER(subscribe);
CALLBACK_WRAPPER(unsubscribe);

#undef CALLBACK_WRAPPER

static void
sol_mqtt_on_connect(struct mosquitto *mosq, void *data, int rc)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    mqtt->connection_status = rc;

    if (mqtt->connect_timeout || !mqtt->handlers.connect)
        return;

    mqtt->connect_timeout = sol_timeout_add(0, sol_mqtt_on_connect_wrapper, mqtt);
}

static void
sol_mqtt_on_disconnect(struct mosquitto *mosq, void *data, int rc)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    mqtt->connection_status = SOL_MQTT_DISCONNECTED;

    if (mqtt->disconnect_timeout || !mqtt->handlers.disconnect)
        return;

    mqtt->disconnect_timeout = sol_timeout_add(0, sol_mqtt_on_disconnect_wrapper, mqtt);
}

static void
sol_mqtt_on_publish(struct mosquitto *mosq, void *data, int id)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    if (mqtt->publish_timeout || !mqtt->handlers.publish)
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

    if (mqtt->handlers.message)
        mqtt->handlers.message(mqtt->data, mqtt, message);

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
    struct sol_buffer payload;

    SOL_NULL_CHECK(mqtt);
    SOL_NULL_CHECK(m_message);

    if (!mqtt->handlers.message)
        return;

    wrapper_data = malloc(sizeof(*wrapper_data));
    SOL_NULL_CHECK(wrapper_data);

    payload = SOL_BUFFER_INIT_CONST(m_message->payload, m_message->payloadlen);
    message = sol_mqtt_message_new(m_message->topic, &payload, (sol_mqtt_qos)m_message->qos, m_message->retain);
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

    if (mqtt->subscribe_timeout || !mqtt->handlers.subscribe)
        return;

    mqtt->subscribe_timeout = sol_timeout_add(0, sol_mqtt_on_subscribe_wrapper, mqtt);
}

static void
sol_mqtt_on_unsubscribe(struct mosquitto *mosq, void *data, int id)
{
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt);

    if (mqtt->unsubscribe_timeout || !mqtt->handlers.unsubscribe)
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

    mqtt->handlers = config->handlers;
    mqtt->data = data;

    mosquitto_connect_callback_set(mqtt->mosq, sol_mqtt_on_connect);
    mosquitto_disconnect_callback_set(mqtt->mosq, sol_mqtt_on_disconnect);
    mosquitto_publish_callback_set(mqtt->mosq, sol_mqtt_on_publish);
    mosquitto_message_callback_set(mqtt->mosq, sol_mqtt_on_message);
    mosquitto_subscribe_callback_set(mqtt->mosq, sol_mqtt_on_subscribe);
    mosquitto_unsubscribe_callback_set(mqtt->mosq, sol_mqtt_on_unsubscribe);

    if (config->will) {
        r = mosquitto_will_set(mqtt->mosq, config->will->topic, config->will->payload->used,
            config->will->payload->data, (int)config->will->qos, config->will->retain);

        if (r != MOSQ_ERR_SUCCESS) {
            SOL_WRN("Unable to set will message");
            goto error;
        }
    }

    mqtt->connection_status = SOL_MQTT_DISCONNECTED;

    r = mosquitto_connect_async(mqtt->mosq, host, port, mqtt->keepalive);
    if (r != MOSQ_ERR_SUCCESS)
        SOL_WRN("Unable to connect to %s:%d", host, port);

    mqtt->socket_fd = mosquitto_socket(mqtt->mosq);
    if (mqtt->socket_fd == -1) {
        SOL_WRN("Unable to get socket file descriptor");
        goto error;
    }

    mqtt->socket_watch = sol_fd_add(mqtt->socket_fd, SOL_FD_FLAGS_IN | SOL_FD_FLAGS_PRI,
        sol_mqtt_event_loop, mqtt);
    SOL_NULL_CHECK_GOTO(mqtt->socket_watch, error);

    return mqtt;

error:
    mosquitto_destroy(mqtt->mosq);
    free(mqtt);
    return NULL;
}

SOL_API int
sol_mqtt_reconnect(struct sol_mqtt *mqtt)
{
    int r;
    unsigned int flags;

    SOL_NULL_CHECK(mqtt, -EINVAL);

    r = mosquitto_reconnect_async(mqtt->mosq);

    if (r != MOSQ_ERR_SUCCESS) {
        SOL_WRN("Unable to reconnect");
        return -EINVAL;
    }

    if (mqtt->socket_watch)
        sol_fd_del(mqtt->socket_watch);

    mqtt->socket_watch = NULL;

    mqtt->socket_fd = mosquitto_socket(mqtt->mosq);
    if (mqtt->socket_fd == -1) {
        SOL_WRN("Unable to get socket file descriptor");
        return -EINVAL;
    }

    flags = SOL_FD_FLAGS_IN | SOL_FD_FLAGS_PRI;
    if (mosquitto_want_write(mqtt->mosq))
        flags |= SOL_FD_FLAGS_OUT;

    mqtt->socket_watch = sol_fd_add(mqtt->socket_fd, flags, sol_mqtt_event_loop, mqtt);
    SOL_NULL_CHECK(mqtt->socket_watch, -EINVAL);

    return 0;
}

SOL_API void
sol_mqtt_disconnect(struct sol_mqtt *mqtt)
{
    CHECK_INIT();
    SOL_NULL_CHECK(mqtt);

    if (mqtt->socket_watch)
        sol_fd_del(mqtt->socket_watch);

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
sol_mqtt_get_connection_status(const struct sol_mqtt *mqtt)
{
    SOL_NULL_CHECK(mqtt, -EINVAL);
    return mqtt->connection_status;
}

SOL_API int
sol_mqtt_publish(const struct sol_mqtt *mqtt, struct sol_mqtt_message *message)
{
    int r;

    CHECK_INIT(-EINVAL);
    SOL_NULL_CHECK(mqtt, -EINVAL);
    SOL_NULL_CHECK(message, -EINVAL);

    r = mosquitto_publish(mqtt->mosq, &message->id, message->topic, message->payload->used,
        message->payload->data, (int)message->qos, message->retain);

    if (r != MOSQ_ERR_SUCCESS) {
        SOL_WRN("Unable to publish to '%s'", message->topic);
        return -EINVAL;
    }

    if (mosquitto_want_write(mqtt->mosq) && !sol_fd_set_flags(mqtt->socket_watch,
        sol_fd_get_flags(mqtt->socket_watch) | SOL_FD_FLAGS_OUT))
        return -EINVAL;

    return 0;
}

SOL_API int
sol_mqtt_subscribe(const struct sol_mqtt *mqtt, const char *topic, sol_mqtt_qos qos)
{
    int r;

    CHECK_INIT(-EINVAL);
    SOL_NULL_CHECK(mqtt, -EINVAL);
    SOL_NULL_CHECK(topic, -EINVAL);

    r = mosquitto_subscribe(mqtt->mosq, NULL, topic, (int)qos);
    if (r != MOSQ_ERR_SUCCESS) {
        SOL_WRN("Unable to subscribe to '%s'", topic);
        return -EINVAL;
    }

    return 0;
}

