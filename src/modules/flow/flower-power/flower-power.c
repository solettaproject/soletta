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

#include "sol-flower-power.h"
#include "sol-flow/flower-power.h"
#include "sol-flow-internal.h"

#include <sol-http-client.h>
#include <sol-json.h>
#include <sol-util-internal.h>

#include <errno.h>
#include <float.h>
#include <math.h>
#include <time.h>


static void
packet_type_flower_power_packet_dispose(const struct sol_flow_packet_type *packet_type,
    void *mem)
{
    struct sol_flower_power_data *packet_type_flower_power = mem;

    free(packet_type_flower_power->id);
}

static int
packet_type_flower_power_packet_init(
    const struct sol_flow_packet_type *packet_type,
    void *mem, const void *input)
{
    const struct sol_flower_power_data *in = input;
    struct sol_flower_power_data *packet_type_flower_power = mem;

    SOL_NULL_CHECK(in->id, -EINVAL);

    packet_type_flower_power->id = strdup(in->id);
    SOL_NULL_CHECK(packet_type_flower_power->id, -ENOMEM);

    packet_type_flower_power->timestamp = in->timestamp;

    packet_type_flower_power->fertilizer = in->fertilizer;
    packet_type_flower_power->fertilizer_min = in->fertilizer_min;
    packet_type_flower_power->fertilizer_max = in->fertilizer_max;

    packet_type_flower_power->light = in->light;
    packet_type_flower_power->light_min = in->light_min;
    packet_type_flower_power->light_max = in->light_max;

    packet_type_flower_power->temperature = in->temperature;
    packet_type_flower_power->temperature_min = in->temperature_min;
    packet_type_flower_power->temperature_max = in->temperature_max;

    packet_type_flower_power->water = in->water;
    packet_type_flower_power->water_min = in->water_min;
    packet_type_flower_power->water_max = in->water_max;

    return 0;
}

#define PACKET_TYPE_FLOWER_POWER_PACKET_TYPE_API_VERSION (1)

static const struct sol_flow_packet_type _PACKET_TYPE_FLOWER_POWER = {
    SOL_SET_API_VERSION(.api_version = PACKET_TYPE_FLOWER_POWER_PACKET_TYPE_API_VERSION, )
    .name = "PACKET_TYPE_FLOWER_POWER",
    .data_size = sizeof(struct sol_flower_power_data),
    .init = packet_type_flower_power_packet_init,
    .dispose = packet_type_flower_power_packet_dispose,
};
SOL_API const struct sol_flow_packet_type *PACKET_TYPE_FLOWER_POWER =
    &_PACKET_TYPE_FLOWER_POWER;

#undef PACKET_TYPE_FLOWER_POWER_PACKET_TYPE_API_VERSION

static struct sol_flow_packet *
sol_flower_power_new_packet(const struct sol_flower_power_data *fpd)
{
    SOL_NULL_CHECK(fpd, NULL);
    return sol_flow_packet_new(PACKET_TYPE_FLOWER_POWER, fpd);
}

static int
sol_flower_power_get_packet(const struct sol_flow_packet *packet,
    struct sol_flower_power_data *fpd)
{
    SOL_NULL_CHECK(packet, -EINVAL);
    if (sol_flow_packet_get_type(packet) != PACKET_TYPE_FLOWER_POWER)
        return -EINVAL;

    return sol_flow_packet_get(packet, fpd);
}

static int
sol_flower_power_send_packet(struct sol_flow_node *src,
    uint16_t src_port, const struct sol_flower_power_data *fpd)
{
    struct sol_flow_packet *packet;

    packet = sol_flower_power_new_packet(fpd);
    SOL_NULL_CHECK(packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, packet);
}

struct http_get_data {
    struct sol_flow_node *node;
    struct sol_ptr_vector pending_conns;
    char *client_id;
    char *client_secret;
    char *username;
    char *password;
    char *token;
};

struct filter_data {
    char *id;
};

static int
http_get_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct http_get_data *mdata = data;
    const struct sol_flow_node_type_flower_power_http_get_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_HTTP_GET_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_flower_power_http_get_options *)options;

    if (!opts->client_id || !strlen(opts->client_id) ||
        !opts->client_secret || !strlen(opts->client_secret)) {
        SOL_ERR("Valid client id and secret are required.");
        return -EINVAL;
    }

    mdata->client_id = strdup(opts->client_id);
    SOL_NULL_CHECK(mdata->client_id, -ENOMEM);
    mdata->client_secret = strdup(opts->client_secret);
    SOL_NULL_CHECK_GOTO(mdata->client_secret, open_error);

    mdata->node = node;

    sol_ptr_vector_init(&mdata->pending_conns);

    return 0;

open_error:
    free(mdata->client_id);
    return -ENOMEM;
}

static void
http_get_close(struct sol_flow_node *node, void *data)
{
    struct sol_http_client_connection *connection;
    struct http_get_data *mdata = data;
    uint16_t i;

    free(mdata->client_id);
    /* Zero memory used to store client secret and password */
    if (mdata->client_secret) {
        /* TODO we may need something like explicit_bzero to avoid otimizations
         * removing this memset. Or we could shouldn't alloc this memory this
         * way since it could be swapped at some point...
         * Also it was written in options->client_secret, so... */
        memset(mdata->client_secret, 0, strlen(mdata->client_secret));
        free(mdata->client_secret);
    }
    free(mdata->username);
    if (mdata->password) {
        memset(mdata->password, 0, strlen(mdata->password));
        free(mdata->password);
    }
    if (mdata->token) {
        memset(mdata->token, 0, strlen(mdata->token));
        free(mdata->token);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&mdata->pending_conns, connection, i)
        sol_http_client_connection_cancel(connection);
    sol_ptr_vector_clear(&mdata->pending_conns);
}

#define BASE_URL "https://apiflowerpower.parrot.com/"
#define STATUS_URL BASE_URL "sensor_data/v4/garden_locations_status"
#define AUTH_URL BASE_URL "user/v1/authenticate"
#define AUTH_START "Bearer "

static void
generate_token_cb(void *data,
    const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct http_get_data *mdata = data;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    struct sol_buffer buffer;
    enum sol_json_loop_reason reason;
    const size_t auth_len = strlen(AUTH_START);

    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

    if (!response) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Error while reaching service to generate token.");
        return;
    }
    SOL_HTTP_RESPONSE_CHECK_API(response);

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Service returned unexpected response code: %d for request %s",
            response->response_code, AUTH_URL);
        return;
    }

    if (!response->content.used) {
        sol_flow_send_error_packet(mdata->node, ENOKEY,
            "Empty response from server for request %s", AUTH_URL);
        return;
    }

    sol_json_scanner_init(&scanner, response->content.data,
        response->content.used);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        size_t value_size, token_size;
        int r;

        if (!SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "access_token"))
            continue;

        r = sol_json_token_get_unescaped_string(&value, &buffer);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        value_size = buffer.used;
        token_size = value_size + auth_len + 1;

        free(mdata->token);
        mdata->token = malloc(token_size);
        SOL_NULL_CHECK(mdata->token);

        strcpy(mdata->token, AUTH_START);
        memcpy(mdata->token + auth_len, buffer.data, buffer.used);
        *(mdata->token + token_size - 1) = '\0';
        sol_buffer_fini(&buffer);

        return;
    }

error:
    sol_flow_send_error_packet(mdata->node, ENOKEY,
        "Server response doesn't contain a token.");
}

static int
generate_token(struct http_get_data *mdata)
{
    struct sol_http_params params;
    struct sol_http_client_connection *connection;
    int r;

    sol_http_params_init(&params);
    if ((!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_QUERY("grant_type", "password"))) ||
        (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_QUERY("username", mdata->username))) ||
        (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_QUERY("password", mdata->password))) ||
        (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_QUERY("client_id", mdata->client_id))) ||
        (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_QUERY("client_secret",
        mdata->client_secret)))) {
        SOL_WRN("Failed to set query params");
        sol_http_params_clear(&params);
        return -ENOMEM;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, AUTH_URL,
        &params, generate_token_cb, mdata);

    sol_http_params_clear(&params);

    if (!connection) {
        SOL_WRN("Could not create HTTP request for %s", AUTH_URL);
        return -EINVAL;
    }

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;
}

static int
http_set_password(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_get_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (!in_value || !strlen(in_value)) {
        sol_flow_send_error_packet(node, EINVAL, "Invalid password");
        return 0;
    }

    free(mdata->password);
    mdata->password = strdup(in_value);
    SOL_NULL_CHECK(mdata->password, -ENOMEM);

    if (!mdata->username)
        return 0;

    return generate_token(mdata);
}

static bool
get_measure(struct sol_json_token *measure_token, struct sol_drange *measure,
    struct sol_drange *min, struct sol_drange *max)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    bool found_key = false;

    sol_json_scanner_init_from_token(&scanner, measure_token);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "gauge_values")) {
            found_key = true;
            break;
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK)
        return false;

    if (!found_key) {
        SOL_WRN("Failed to find 'gauge_values' key");
        return false;
    }

    sol_json_scanner_init_from_token(&scanner, &value);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "current_value")) {
            if (sol_json_token_get_double(&value, &measure->val)) {
                SOL_DBG("Failed to get current value");
                measure->val = NAN;
            }
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "max_threshold")) {
            if (sol_json_token_get_double(&value, &max->val)) {
                SOL_DBG("Failed to get max value");
                max->val = NAN;
            }
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "min_threshold")) {
            if (sol_json_token_get_double(&value, &min->val)) {
                SOL_DBG("Failed to get min value");
                min->val = NAN;
            }
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK)
        return false;

    return true;
}

static int
get_timestamp(struct sol_json_token *value, struct timespec *timestamp)
{
    struct tm time_tm = { 0 };
    time_t tmp_timestamp;
    char *p, *timestamp_str;

    timestamp_str = sol_json_token_get_unescaped_string_copy(value);
    SOL_NULL_CHECK(timestamp_str, -EINVAL);
    p = strptime(timestamp_str, "%Y-%m-%dT%H:%M:%SZ", &time_tm);
    free(timestamp_str);

    if (!p || *p) {
        SOL_WRN("Failed to convert timestamp");
        return -EINVAL;
    }

    tmp_timestamp = mktime(&time_tm);
    if (tmp_timestamp < 0) {
        SOL_WRN("Failed to convert timestamp");
        return -EINVAL;
    }

    /* since time was received on UTC but mktime was used with current timezone
     * it need to be converted considering timezone */
    timestamp->tv_sec = tmp_timestamp - timezone;
    timestamp->tv_nsec = 0;

    return 0;
}

#define INIT_FERTILIZER(_fertilizer) \
    _fertilizer.min = 0; \
    _fertilizer.max = 10;

#define INIT_LIGHT(_light) \
    _light.min = 0.13; \
    _light.max = 104;

#define INIT_TEMPERATURE(_temperature) \
    _temperature.min = 268.15; \
    _temperature.max = 328.15;

#define INIT_WATER(_water) \
    _water.min = 0; \
    _water.max = 50;


/* SENSOR INFORMATION PACKET / NODE TYPES */

static void
packet_type_flower_power_sensor_packet_dispose(const struct sol_flow_packet_type *packet_type,
    void *mem)
{
    struct sol_flower_power_sensor_data *fpsd = mem;

    free(fpsd->id);
}

static int
packet_type_flower_power_sensor_packet_init(
    const struct sol_flow_packet_type *packet_type,
    void *mem, const void *input)
{
    const struct sol_flower_power_sensor_data *in = input;
    struct sol_flower_power_sensor_data *fpsd = mem;

    SOL_NULL_CHECK(in->id, -EINVAL);

    fpsd->id = strdup(in->id);
    SOL_NULL_CHECK(fpsd->id, -ENOMEM);

    fpsd->battery_level = in->battery_level;
    fpsd->timestamp = in->timestamp;
    fpsd->battery_end_of_life = in->battery_end_of_life;

    return 0;
}

#define PACKET_TYPE_FLOWER_POWER_SENSOR_PACKET_TYPE_API_VERSION (1)

static const struct sol_flow_packet_type _PACKET_TYPE_FLOWER_POWER_SENSOR = {
    SOL_SET_API_VERSION(.api_version = PACKET_TYPE_FLOWER_POWER_SENSOR_PACKET_TYPE_API_VERSION, )
    .name = "PACKET_TYPE_FLOWER_POWER_SENSOR",
    .data_size = sizeof(struct sol_flower_power_sensor_data),
    .init = packet_type_flower_power_sensor_packet_init,
    .dispose = packet_type_flower_power_sensor_packet_dispose,
};
SOL_API const struct sol_flow_packet_type *PACKET_TYPE_FLOWER_POWER_SENSOR =
    &_PACKET_TYPE_FLOWER_POWER_SENSOR;

#undef PACKET_TYPE_FLOWER_POWER_SENSOR_PACKET_TYPE_API_VERSION
static struct sol_flow_packet *
sol_flower_power_sensor_new_packet_components(const char *id,
    const struct timespec *timestamp,
    const struct timespec *battery_end_of_life,
    struct sol_drange *battery_level)
{
    struct sol_flower_power_sensor_data fpsd;

    SOL_NULL_CHECK(id, NULL);
    SOL_NULL_CHECK(timestamp, NULL);
    SOL_NULL_CHECK(battery_end_of_life, NULL);
    SOL_NULL_CHECK(battery_level, NULL);

    fpsd.id = (char *)id;
    fpsd.timestamp = *timestamp;
    fpsd.battery_end_of_life = *battery_end_of_life;
    fpsd.battery_level = *battery_level;

    return sol_flow_packet_new(PACKET_TYPE_FLOWER_POWER_SENSOR, &fpsd);
}

static int
sol_flower_power_sensor_send_packet_components(struct sol_flow_node *src,
    uint16_t src_port, char *id, struct timespec *timestamp,
    struct timespec *battery_end_of_life,
    struct sol_drange *battery_level)
{
    struct sol_flow_packet *packet;

    packet = sol_flower_power_sensor_new_packet_components(id, timestamp,
        battery_end_of_life, battery_level);
    SOL_NULL_CHECK(packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, packet);
}

static void
http_get_cb(void *data, const struct sol_http_client_connection *connection,
    struct sol_http_response *response)
{
    struct http_get_data *mdata = data;
    struct sol_json_scanner scanner, locations_scanner, sensors_scanner;
    struct sol_json_token token, key, value, locations, sensors;
    int r = 0;
    enum sol_json_loop_reason reason;
    bool found_locations = false, found_sensors = false;

    if (sol_ptr_vector_remove(&mdata->pending_conns, connection) < 0)
        SOL_WRN("Failed to find pending connection %p", connection);

    if (!response) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Error while reaching service to get plants and sensors info.");
        return;
    }
    SOL_HTTP_RESPONSE_CHECK_API(response);

    if (response->response_code != SOL_HTTP_STATUS_OK) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Service returned unexpected response code: %d for request %s",
            response->response_code, STATUS_URL);
        return;
    }

    if (!response->content.used) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "Empty response from server for request %s", STATUS_URL);
        return;
    }

    sol_json_scanner_init(&scanner, response->content.data,
        response->content.used);

    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "locations")) {
            found_locations = true;
            locations = value;
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "sensors")) {
            found_sensors = true;
            sensors = value;
        }
    }
    if (reason != SOL_JSON_LOOP_REASON_OK)
        goto error;

    if (!found_locations) {
        SOL_DBG("No plants found on response");
    } else {
        sol_json_scanner_init_from_token(&locations_scanner, &locations);
        SOL_JSON_SCANNER_ARRAY_LOOP (&locations_scanner, &token,
            SOL_JSON_TYPE_OBJECT_START, reason) {
            struct sol_flower_power_data fpd =
                SOL_FLOWER_POWER_DATA_INIT_VALUE(NAN);

            SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&locations_scanner, &token,
                &key, &value, reason) {
                if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "fertilizer")) {
                    INIT_FERTILIZER(fpd.fertilizer);
                    INIT_FERTILIZER(fpd.fertilizer_min);
                    INIT_FERTILIZER(fpd.fertilizer_max);

                    if (!get_measure(&value, &fpd.fertilizer,
                        &fpd.fertilizer_min, &fpd.fertilizer_max)) {
                        SOL_WRN("Failed to get fertilizer info");
                        r = -EINVAL;
                        goto fpd_error;
                    }
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "light")) {
                    INIT_LIGHT(fpd.light);
                    INIT_LIGHT(fpd.light_min);
                    INIT_LIGHT(fpd.light_max);

                    if (!get_measure(&value, &fpd.light,
                        &fpd.light_min, &fpd.light_max)) {
                        SOL_WRN("Failed to get light info");
                        r = -EINVAL;
                        goto fpd_error;
                    }
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                    "air_temperature")) {
                    INIT_TEMPERATURE(fpd.temperature);
                    INIT_TEMPERATURE(fpd.temperature_min);
                    INIT_TEMPERATURE(fpd.temperature_max);

                    if (!get_measure(&value, &fpd.temperature,
                        &fpd.temperature_min, &fpd.temperature_max)) {
                        SOL_WRN("Failed to get temperature info");
                        r = -EINVAL;
                        goto fpd_error;
                    }

                    /* convert from Celsius to Kelvin */
                    if (!isnan(fpd.temperature.val))
                        fpd.temperature.val += 273.15;
                    if (!isnan(fpd.temperature_min.val))
                        fpd.temperature_min.val += 273.15;
                    if (!isnan(fpd.temperature_max.val))
                        fpd.temperature_max.val += 273.15;
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                    "soil_moisture")) {
                    INIT_WATER(fpd.water);
                    INIT_WATER(fpd.water_min);
                    INIT_WATER(fpd.water_max);

                    if (!get_measure(&value, &fpd.water, &fpd.water_min,
                        &fpd.water_max)) {
                        SOL_WRN("Failed to get water info");
                        r = -EINVAL;
                        goto fpd_error;
                    }
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                    "location_identifier")) {
                    fpd.id = sol_json_token_get_unescaped_string_copy(&value);
                    SOL_NULL_CHECK_GOTO(fpd.id, fpd_error);
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                    "last_sample_upload")) {
                    r = get_timestamp(&value, &fpd.timestamp);
                    SOL_INT_CHECK_GOTO(r, < 0, fpd_error);
                }
            }
            r = sol_flower_power_send_packet(mdata->node,
                SOL_FLOW_NODE_TYPE_FLOWER_POWER_HTTP_GET__OUT__OUT,
                &fpd);
fpd_error:
            free(fpd.id);
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    }

    if (!found_sensors) {
        SOL_DBG("No sensors found on response");
    } else {
        sol_json_scanner_init_from_token(&sensors_scanner, &sensors);
        SOL_JSON_SCANNER_ARRAY_LOOP (&sensors_scanner, &token,
            SOL_JSON_TYPE_OBJECT_START, reason) {
            struct sol_drange battery_level = { -1, 0, 100, DBL_MIN };
            struct timespec timestamp = { 0 };
            struct timespec battery_end_of_life = { 0 };
            char *id = NULL;

            SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&sensors_scanner, &token,
                &key, &value, reason) {
                if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "battery_level")) {
                    sol_json_scanner_init_from_token(&scanner, &value);
                    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key,
                        &value, reason) {
                        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                            "level_percent")) {
                            if (sol_json_token_get_double(&value,
                                &battery_level.val)) {
                                SOL_DBG("Failed to get battery level");
                                goto sensor_error;
                            }
                        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                            "battery_end_of_life_date_utc")) {
                            r = get_timestamp(&value, &battery_end_of_life);
                            SOL_INT_CHECK_GOTO(r, < 0, sensor_error);
                        }
                    }
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                    "sensor_serial")) {
                    id = sol_json_token_get_unescaped_string_copy(&value);
                    SOL_NULL_CHECK_GOTO(id, error);
                } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key,
                    "last_upload_datetime_utc")) {
                    r = get_timestamp(&value, &timestamp);
                    SOL_INT_CHECK_GOTO(r, < 0, sensor_error);
                }
            }
            r = sol_flower_power_sensor_send_packet_components(mdata->node,
                SOL_FLOW_NODE_TYPE_FLOWER_POWER_HTTP_GET__OUT__DEVICE_INFO,
                id, &timestamp, &battery_end_of_life, &battery_level);
sensor_error:
            free(id);
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    }

    return;

error:
    sol_flow_send_error_packet(mdata->node, EINVAL,
        "Error while parsing server response.");
}

#undef INIT_FERTILIZER
#undef INIT_LIGHT
#undef INIT_TEMPERATURE
#undef INIT_WATER

static int
http_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_get_data *mdata = data;
    struct sol_http_params params;
    struct sol_http_client_connection *connection;
    int r;

    if (!mdata->token) {
        sol_flow_send_error_packet(node, EINVAL, "Missing valid token");
        return 0;
    }

    sol_http_params_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Authorization", mdata->token))) {
        SOL_WRN("Failed to set query params");
        sol_http_params_clear(&params);
        return -ENOMEM;
    }

    connection = sol_http_client_request(SOL_HTTP_METHOD_GET, STATUS_URL,
        &params, http_get_cb, mdata);

    sol_http_params_clear(&params);

    if (!connection) {
        SOL_WRN("Could not create HTTP request for %s", STATUS_URL);
        return -EINVAL;
    }

    r = sol_ptr_vector_append(&mdata->pending_conns, connection);
    if (r < 0) {
        SOL_WRN("Failed to keep pending connection.");
        sol_http_client_connection_cancel(connection);
        return -ENOMEM;
    }

    return 0;
}

#undef BASE_URL
#undef STATUS_URL
#undef AUTH_URL
#undef AUTH_START

static int
http_set_username(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_get_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (!in_value || !strlen(in_value)) {
        sol_flow_send_error_packet(node, EINVAL, "Invalid username");
        return 0;
    }

    free(mdata->username);
    mdata->username = strdup(in_value);
    SOL_NULL_CHECK(mdata->username, -ENOMEM);

    if (!mdata->password)
        return 0;

    return generate_token(mdata);
}

static int
parse_packet(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_flower_power_data fpd;
    int r;

    r = sol_flower_power_get_packet(packet, &fpd);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__ID,
        fpd.id);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_timestamp_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__TIMESTAMP,
        &fpd.timestamp);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__FERTILIZER,
        &fpd.fertilizer);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__FERTILIZER_MIN,
        &fpd.fertilizer_min);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__FERTILIZER_MAX,
        &fpd.fertilizer_max);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__LIGHT,
        &fpd.light);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__LIGHT_MIN,
        &fpd.light_min);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__LIGHT_MAX,
        &fpd.light_max);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__KELVIN,
        &fpd.temperature);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__KELVIN_MIN,
        &fpd.temperature_min);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__KELVIN_MAX,
        &fpd.temperature_max);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__WATER,
        &fpd.water);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__WATER_MIN,
        &fpd.water_min);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__WATER_MAX,
        &fpd.water_max);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
filter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct filter_data *mdata = data;
    const struct sol_flow_node_type_flower_power_filter_id_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_FILTER_ID_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_flower_power_filter_id_options *)options;

    if (opts->id) {
        mdata->id = strdup(opts->id);
        SOL_NULL_CHECK(opts->id, -ENOMEM);
    }

    return 0;
}

static void
filter_close(struct sol_flow_node *node, void *data)
{
    struct filter_data *mdata = data;

    free(mdata->id);
}

static int
filter_set_id(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (!in_value || !strlen(in_value)) {
        sol_flow_send_error_packet(node, EINVAL, "Invalid id");
        return -EINVAL;
    }

    free(mdata->id);
    mdata->id = strdup(in_value);
    SOL_NULL_CHECK(mdata->id, -ENOMEM);

    return 0;
}

static int
filter_packet(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_data *mdata = data;
    struct sol_flower_power_data fpd;
    int r;

    r = sol_flower_power_get_packet(packet, &fpd);
    SOL_INT_CHECK(r, < 0, r);

    if (!fpd.id || !mdata->id) {
        sol_flow_send_error_packet(node, EINVAL,
            "Failed to compare plant ids");
        return -EINVAL;
    }

    /* Don't forward packets if ids don't match */
    if (strcmp(fpd.id, mdata->id))
        return 0;

    return sol_flower_power_send_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_FILTER_ID__OUT__OUT, &fpd);
}

static struct sol_flow_packet *
sol_flower_power_sensor_new_packet(const struct sol_flower_power_sensor_data *fpsd)
{
    SOL_NULL_CHECK(fpsd, NULL);
    return sol_flow_packet_new(PACKET_TYPE_FLOWER_POWER_SENSOR, fpsd);
}

static int
sol_flower_power_sensor_get_packet(const struct sol_flow_packet *packet,
    struct sol_flower_power_sensor_data *fpsd)
{
    SOL_NULL_CHECK(packet, -EINVAL);
    if (sol_flow_packet_get_type(packet) != PACKET_TYPE_FLOWER_POWER_SENSOR)
        return -EINVAL;

    return sol_flow_packet_get(packet, fpsd);
}

static int
sol_flower_power_sensor_get_packet_components(
    const struct sol_flow_packet *packet,
    const char **id, struct timespec *timestamp,
    struct timespec *battery_end_of_life,
    struct sol_drange *battery_level)
{
    struct sol_flower_power_sensor_data fpsd;
    int ret;

    SOL_NULL_CHECK(packet, -EINVAL);
    if (sol_flow_packet_get_type(packet) != PACKET_TYPE_FLOWER_POWER_SENSOR)
        return -EINVAL;

    ret = sol_flow_packet_get(packet, &fpsd);
    SOL_INT_CHECK(ret, != 0, ret);

    if (id)
        *id = fpsd.id;
    if (timestamp)
        *timestamp = fpsd.timestamp;
    if (battery_end_of_life)
        *battery_end_of_life = fpsd.battery_end_of_life;
    if (battery_level)
        *battery_level = fpsd.battery_level;

    return ret;
}

static int
sol_flower_power_sensor_send_packet(struct sol_flow_node *src,
    uint16_t src_port, const struct sol_flower_power_sensor_data *fpsd)
{
    struct sol_flow_packet *packet;

    packet = sol_flower_power_sensor_new_packet(fpsd);
    SOL_NULL_CHECK(packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, packet);
}

static int
parse_sensor_packet(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_drange battery_level;
    struct timespec timestamp, battery_end_of_life;
    const char *id;
    int r;

    r = sol_flower_power_sensor_get_packet_components(packet, &id, &timestamp,
        &battery_end_of_life, &battery_level);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_SENSOR_VALUE__OUT__ID,
        id);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_timestamp_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_SENSOR_VALUE__OUT__TIMESTAMP,
        &timestamp);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_timestamp_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_SENSOR_VALUE__OUT__BATTERY_END_OF_LIFE,
        &battery_end_of_life);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_SENSOR_VALUE__OUT__BATTERY_LEVEL,
        &battery_level);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
filter_sensor_packet(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_data *mdata = data;
    struct sol_flower_power_sensor_data fpsd;
    int r;

    r = sol_flower_power_sensor_get_packet(packet, &fpsd);
    SOL_INT_CHECK(r, < 0, r);

    if (!fpsd.id || !mdata->id) {
        sol_flow_send_error_packet(node, EINVAL,
            "Failed to compare sensor ids");
        return -EINVAL;
    }

    /* Don't forward packets if ids don't match */
    if (strcmp(fpsd.id, mdata->id))
        return 0;

    return sol_flower_power_sensor_send_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_FILTER_SENSOR_ID__OUT__OUT, &fpsd);
}

#include "flower-power-gen.c"
