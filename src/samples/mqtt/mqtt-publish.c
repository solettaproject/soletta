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

/**
 * @file
 * @brief MQTT Publish client
 *
 * Sample client that connects a broker at host:port and publishes the
 * provided message once every 1 second.
 */

#include <string.h>

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mqtt.h>

static struct sol_timeout *timeout;
static char *topic;
static struct sol_mqtt_message message;

static bool
publish_callback(void *data)
{
    static int id = 0;
    struct sol_mqtt *mqtt = data;

    SOL_NULL_CHECK(mqtt, false);

    SOL_INF("%d: Sending Message.", id++);

    if (sol_mqtt_publish(mqtt, &message)) {
        SOL_WRN("Unable to publish message");
        return false;
    }

    return true;
}

static bool
try_reconnect(void *data)
{
    return sol_mqtt_reconnect((struct sol_mqtt *)data) != 0;
}

static void
on_connect(void *data, struct sol_mqtt *mqtt)
{
    if (sol_mqtt_get_connection_status(mqtt) != SOL_MQTT_CONNECTED) {
        SOL_WRN("Unable to connect, retrying...");
        sol_timeout_add(1000, try_reconnect, mqtt);
        return;
    }

    if (!publish_callback(mqtt))
        return;

    timeout = sol_timeout_add(1000, publish_callback, mqtt);
    if (!timeout)
        SOL_WRN("Unable to setup callback");
}

static void
on_disconnect(void *data, struct sol_mqtt *mqtt)
{
    SOL_INF("Reconnecting...");
    sol_timeout_add(1000, try_reconnect, mqtt);
}

const struct sol_mqtt_config config = {
    .api_version = SOL_MQTT_CONFIG_API_VERSION,
    .clean_session = true,
    .keepalive = 60,
    .handlers = {
        .connect = on_connect,
        .disconnect = on_disconnect,
    },
};

int
main(int argc, char *argv[])
{
    struct sol_mqtt *mqtt;
    struct sol_buffer payload;
    int port;

    sol_init();

    if (argc < 5) {
        SOL_INF("Usage: %s <ip> <port> <topic> <message>", argv[0]);
        return 0;
    }

    port = atoi(argv[2]);
    topic = argv[3];

    payload = SOL_BUFFER_INIT_CONST(argv[4], strlen(argv[4]));

    message = (struct sol_mqtt_message){
        .topic = topic,
        .payload = &payload,
        .qos = SOL_MQTT_QOS_EXACTLY_ONCE,
        .retain = false,
    };

    mqtt = sol_mqtt_connect(argv[1], port, &config, NULL);
    if (!mqtt) {
        SOL_WRN("Unable to create MQTT session");
        return -1;
    }

    sol_run();

    if (timeout)
        sol_timeout_del(timeout);

    sol_mqtt_disconnect(mqtt);

    sol_shutdown();

    return 0;
}
