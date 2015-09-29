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
 * @brief IBM Bluemix phonemotion demo client
 *
 * Sample client that connects IBM Bluemix phonemotion demo and simulates
 * sensor data from a device.
 *
 * https://github.com/ibm-messaging/iotf-phonemotion
 *
 * To test, go to http://www.ibm.com/cloud-computing/bluemix/solutions/iot/
 * and enter a user and a pin, then provide those credentials to the sample
 *
 * E.g: ./bluemix http://iotf.mybluemix.net/auth soletta_test 0000
 */

#include <math.h>
#include <sol-http-client.h>
#include <sol-json.h>
#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mqtt.h>

struct bluemix_data {
    /**
     * HTML POST authentication
     */
    char *user;
    char *pin;
    char *url;

    /**
     * MQTT authentication
     */
    char *org_id;
    char *client_id;
    char *auth_token;

    struct sol_timeout *connect_timeout;

    /**
     * MQTT Communication
     */
    struct sol_mqtt *mqtt;
    struct sol_timeout *publish_timeout;
};

/**
 * MQTT Data communication
 */

#define PI 3.14159265
#define SENSOR_FMT "{\"d\": {\"ax\": %f, \"ay\": %f, \"az\": %f, \"oa\": %f, \"ob\": %f, \"og\": %f}}"

static bool
publish_callback(void *vdata)
{
    struct bluemix_data *data = (struct bluemix_data *)vdata;
    struct sol_buffer payload = SOL_BUFFER_INIT_EMPTY;
    struct sol_mqtt_message message;
    char topic[] = "iot-2/evt/sensorData/fmt/json-iotf";
    int r;
    double v;
    static int pulse = 0;

    pulse = (pulse + 1) % 360;
    v = sin(pulse * PI / 180) * 10;

    r = sol_buffer_append_printf(&payload, SENSOR_FMT, v * 1.25, v * 1, v * 0.75, v * 20, v * 15, v * 10);
    SOL_INT_CHECK(r, < 0, false);

    message = (struct sol_mqtt_message){
        .topic = topic,
        .payload = &payload,
        .qos = SOL_MQTT_QOS_EXACTLY_ONCE,
        .retain = false,
    };

    r = sol_mqtt_publish(data->mqtt, &message);

    sol_buffer_fini(&payload);

    SOL_INT_CHECK(r, != 0, false);

    return true;
}

static bool
try_reconnect(void *data)
{
    return sol_mqtt_reconnect((struct sol_mqtt *)data) != 0;
}

static void
on_connect(void *vdata, struct sol_mqtt *mqtt)
{
    struct bluemix_data *data = (struct bluemix_data *)vdata;

    if (sol_mqtt_get_connection_status(mqtt) != SOL_MQTT_CONNECTED) {
        SOL_WRN("Unable to connect, retrying...");
        sol_timeout_add(1000, try_reconnect, mqtt);
        return;
    }

    data->publish_timeout = sol_timeout_add(100, publish_callback, data);
    if (!data->publish_timeout)
        SOL_WRN("Unable to setup callback");
}

static void
on_disconnect(void *data, struct sol_mqtt *mqtt)
{
    SOL_INF("Reconnecting...");
    sol_timeout_add(1000, try_reconnect, mqtt);
}

static struct sol_mqtt_config config = {
    .api_version = SOL_MQTT_CONFIG_API_VERSION,
    .clean_session = true,
    .keepalive = 60,
    .username = "use-token-auth",
    .handlers = {
        .connect = on_connect,
        .disconnect = on_disconnect,
    },
};

static bool
mqtt_init(void *vdata)
{
    struct bluemix_data *data = (struct bluemix_data *)vdata;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    int r;

    SOL_NULL_CHECK_GOTO(data, error);

    r = sol_buffer_append_printf(&buffer, "%s.messaging.internetofthings.ibmcloud.com", data->org_id);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    config.client_id = data->client_id;
    config.password = data->auth_token;

    data->mqtt = sol_mqtt_connect(buffer.data, 1883, &config, data);

    sol_buffer_fini(&buffer);

    if (!data->mqtt) {
        SOL_WRN("Unable to create MQTT session. Retrying...");
        return true;
    }

    SOL_INF("Sending sensor data");

    return false;

error:
    SOL_WRN("Unable to initialize MQTT");
    sol_quit();

    return false;
}

/**
 * Authenticate to HTTP Server
 */

static bool
json_token_to_string(struct sol_json_token *token, char **out)
{
    if (sol_json_token_get_type(token) != SOL_JSON_TYPE_STRING)
        return false;
    free(*out);
    *out = strndup(token->start + 1, token->end - token->start - 2);
    return !!*out;
}

static bool
parse_auth_json(struct bluemix_data *data, const char *json, size_t size)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, json, size);

    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "clientid")) {
            if (!json_token_to_string(&value, &data->client_id))
                goto error;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "authtoken")) {
            if (!json_token_to_string(&value, &data->auth_token))
                goto error;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "orgid")) {
            if (!json_token_to_string(&value, &data->org_id))
                goto error;
        }
    }

    return true;

error:
    SOL_WRN("Error parsing auth json");
    return false;
}

static bool get_user_token(void *vdata);

static void
request_callback(void *vdata, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct bluemix_data *data = (struct bluemix_data *)vdata;

    if (!response->content.used || response->response_code != SOL_HTTP_STATUS_OK) {
        SOL_DBG("Message: %.*s", (int)response->content.used, (char *)response->content.data);
        goto error;
    }

    if (!parse_auth_json(data, response->content.data, response->content.used))
        goto error;

    SOL_INF("Connected: Starting sensor communication");

    sol_timeout_add(1000, mqtt_init, data);

    return;

error:
    SOL_WRN("Unable to get client id and auth code. Retrying...");
    if (!data->connect_timeout) {
        data->connect_timeout = sol_timeout_add(1000, get_user_token, data);

        if (!data->connect_timeout) {
            SOL_WRN("Retry failed.");
            sol_quit();
        }
    }
}

static bool
get_user_token(void *vdata)
{
    struct sol_http_param params;
    struct sol_http_client_connection *connection;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    struct bluemix_data *data = (struct bluemix_data *)vdata;
    int r;

    SOL_NULL_CHECK_GOTO(data, error);
    SOL_NULL_CHECK_GOTO(data->user, error);
    SOL_NULL_CHECK_GOTO(data->pin, error);
    SOL_NULL_CHECK_GOTO(data->url, error);

    data->connect_timeout = NULL;

    r = sol_buffer_append_printf(&buffer, "{\"email\":\"%s\", \"pin\":\"%s\"}", data->user, data->pin);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_http_param_init(&params);

    r = sol_http_param_add(&params, SOL_HTTP_REQUEST_PARAM_HEADER("Content-Type", "application/json"));
    SOL_INT_CHECK_GOTO(r, == 0, param_error);

    r = sol_http_param_add(&params, SOL_HTTP_REQUEST_PARAM_POST_DATA(sol_buffer_get_slice(&buffer)));
    SOL_INT_CHECK_GOTO(r, == 0, param_error);

    SOL_INF("Connecting to the server");

    connection = sol_http_client_request(SOL_HTTP_METHOD_POST, data->url, &params, request_callback, data);
    SOL_NULL_CHECK_GOTO(connection, param_error);

    sol_http_param_free(&params);
    sol_buffer_fini(&buffer);

    return false;

param_error:
    sol_http_param_free(&params);
    sol_buffer_fini(&buffer);

error:
    SOL_WRN("Invalid parameters for connection");
    sol_quit();

    return false;
}

int
main(int argc, char *argv[])
{
    struct bluemix_data data = { 0 };

    sol_init();

    if (argc < 4) {
        SOL_INF("Usage: %s <url> <user> <pin>", argv[0]);
        return 0;
    }

    data.user = argv[2];
    data.pin = argv[3];
    data.url = argv[1];

    data.connect_timeout = sol_timeout_add(0, get_user_token, &data);

    if (!data.connect_timeout) {
        SOL_WRN("Unable to schedule the connection");
        return -1;
    }

    sol_run();

    if (data.mqtt)
        sol_mqtt_disconnect(data.mqtt);

    free(data.org_id);
    free(data.client_id);
    free(data.auth_token);

    sol_shutdown();

    return 0;
}

