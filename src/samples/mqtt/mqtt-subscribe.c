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

/**
 * @file
 * @brief MQTT Publish client
 *
 * Sample client that connects a broker at host:port and subscribes to
 * the provided topic. Whenever a new message is published to that topic,
 * it is printed in the console.
 */

#include <string.h>

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mqtt.h>

static char *topic;

static void
on_message(void *data, struct sol_mqtt *mqtt, const struct sol_mqtt_message *message)
{
    SOL_NULL_CHECK(message);

    SOL_INF("%.*s", (int)message->payload->used, (char *)message->payload->data);
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

    if (sol_mqtt_subscribe(mqtt, topic, SOL_MQTT_QOS_AT_MOST_ONCE))
        SOL_ERR("Unable to subscribe to topic %s", topic);
}

static void
on_disconnect(void *data, struct sol_mqtt *mqtt)
{
    SOL_INF("Reconnecting...");
    sol_timeout_add(1000, try_reconnect, mqtt);
}

int
main(int argc, char *argv[])
{
    struct sol_mqtt *mqtt;
    struct sol_mqtt_config config = {
        SOL_SET_API_VERSION(.api_version = SOL_MQTT_CONFIG_API_VERSION, )
        .clean_session = true,
        .keep_alive = 60,
        .handlers = {
            SOL_SET_API_VERSION(.api_version = SOL_MQTT_HANDLERS_API_VERSION, )
            .connect = on_connect,
            .disconnect = on_disconnect,
            .message = on_message,
        },
    };

    sol_init();

    if (argc < 4) {
        SOL_INF("Usage: %s <ip> <port> <topic>", argv[0]);
        return 0;
    }

    config.port = atoi(argv[2]);
    config.host = argv[1];
    topic = argv[3];

    mqtt = sol_mqtt_connect(&config);
    if (!mqtt) {
        SOL_WRN("Unable to create MQTT session");
        return -1;
    }

    sol_run();

    sol_mqtt_disconnect(mqtt);

    sol_shutdown();

    return 0;
}
