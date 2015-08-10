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
#include "flower-power-gen.h"
#include "sol-flow-internal.h"

#include <sol-http-client.h>
#include <sol-json.h>
#include <sol-util.h>

#include <errno.h>
#include <float.h>


static void
packet_type_flower_power_packet_dispose(const struct sol_flow_packet_type *packet_type,
    void *mem)
{
    struct sol_flower_power_data *packet_type_flower_power = mem;

    free(packet_type_flower_power->id);
    free(packet_type_flower_power->timestamp);
}

static int
packet_type_flower_power_packet_init(const struct sol_flow_packet_type *packet_type,
    void *mem, const void *input)
{
    const struct sol_flower_power_data *in = input;
    struct sol_flower_power_data *packet_type_flower_power = mem;

    SOL_NULL_CHECK(in->id, -EINVAL);
    SOL_NULL_CHECK(in->timestamp, -EINVAL);

    packet_type_flower_power->id = strdup(in->id);
    SOL_NULL_CHECK(packet_type_flower_power->id, -ENOMEM);

    packet_type_flower_power->timestamp = strdup(in->timestamp);
    SOL_NULL_CHECK_GOTO(packet_type_flower_power->timestamp, init_error);

    packet_type_flower_power->fertilizer = in->fertilizer;
    packet_type_flower_power->light = in->light;
    packet_type_flower_power->temperature = in->temperature;
    packet_type_flower_power->water = in->water;

    return 0;

init_error:
    free(packet_type_flower_power->id);
    return -ENOMEM;
}

#define PACKET_TYPE_FLOWER_POWER_PACKET_TYPE_API_VERSION (1)

static const struct sol_flow_packet_type _PACKET_TYPE_FLOWER_POWER = {
    .api_version = PACKET_TYPE_FLOWER_POWER_PACKET_TYPE_API_VERSION,
    .name = "PACKET_TYPE_FLOWER_POWER",
    .data_size = sizeof(struct sol_flower_power_data),
    .init = packet_type_flower_power_packet_init,
    .dispose = packet_type_flower_power_packet_dispose,
};
SOL_API const struct sol_flow_packet_type *PACKET_TYPE_FLOWER_POWER =
    &_PACKET_TYPE_FLOWER_POWER;

#undef PACKET_TYPE_FLOWER_POWER_PACKET_TYPE_API_VERSION

SOL_API struct sol_flow_packet *
sol_flower_power_new_packet(const struct sol_flower_power_data *fpd)
{
    struct sol_flower_power_data packet_type_flower_power;

    SOL_NULL_CHECK(fpd, NULL);
    SOL_NULL_CHECK(fpd->id, NULL);
    SOL_NULL_CHECK(fpd->timestamp, NULL);

    packet_type_flower_power.id = strdup(fpd->id);
    SOL_NULL_CHECK(packet_type_flower_power.id, NULL);

    packet_type_flower_power.timestamp = strdup(fpd->timestamp);
    SOL_NULL_CHECK_GOTO(packet_type_flower_power.timestamp, new_error);

    packet_type_flower_power.fertilizer = fpd->fertilizer;
    packet_type_flower_power.light = fpd->light;
    packet_type_flower_power.temperature = fpd->temperature;
    packet_type_flower_power.water = fpd->water;

    return sol_flow_packet_new(PACKET_TYPE_FLOWER_POWER,
        &packet_type_flower_power);

new_error:
    free(packet_type_flower_power.id);
    return NULL;
}

SOL_API struct sol_flow_packet *
sol_flower_power_new_packet_components(const char *id,
    const char *timestamp,
    struct sol_drange *fertilizer, struct sol_drange *light,
    struct sol_drange *temperature, struct sol_drange *water)
{
    struct sol_flower_power_data packet_type_flower_power;

    SOL_NULL_CHECK(id, NULL);
    SOL_NULL_CHECK(timestamp, NULL);
    SOL_NULL_CHECK(fertilizer, NULL);
    SOL_NULL_CHECK(light, NULL);
    SOL_NULL_CHECK(temperature, NULL);
    SOL_NULL_CHECK(water, NULL);

    packet_type_flower_power.id = (char *)id;
    packet_type_flower_power.timestamp = (char *)timestamp;
    packet_type_flower_power.fertilizer = *fertilizer;
    packet_type_flower_power.light = *light;
    packet_type_flower_power.temperature = *temperature;
    packet_type_flower_power.water = *water;

    return sol_flow_packet_new(PACKET_TYPE_FLOWER_POWER,
        &packet_type_flower_power);
}

SOL_API int
sol_flower_power_get_packet(const struct sol_flow_packet *packet,
    struct sol_flower_power_data *fpd)

{
    SOL_NULL_CHECK(packet, -EINVAL);
    if (sol_flow_packet_get_type(packet) != PACKET_TYPE_FLOWER_POWER)
        return -EINVAL;

    return sol_flow_packet_get(packet, fpd);
}

SOL_API int
sol_flower_power_get_packet_components(const struct sol_flow_packet *packet,
    char **id, char **timestamp,
    struct sol_drange *fertilizer, struct sol_drange *light,
    struct sol_drange *temperature, struct sol_drange *water)

{
    struct sol_flower_power_data packet_type_flower_power;
    int ret;

    SOL_NULL_CHECK(packet, -EINVAL);
    if (sol_flow_packet_get_type(packet) != PACKET_TYPE_FLOWER_POWER)
        return -EINVAL;

    ret = sol_flow_packet_get(packet, &packet_type_flower_power);
    SOL_INT_CHECK(ret, != 0, ret);

    if (id)
        *id = packet_type_flower_power.id;
    if (timestamp)
        *timestamp = packet_type_flower_power.timestamp;
    if (fertilizer)
        *fertilizer = packet_type_flower_power.fertilizer;
    if (light)
        *light = packet_type_flower_power.light;
    if (temperature)
        *temperature = packet_type_flower_power.temperature;
    if (water)
        *water = packet_type_flower_power.water;

    return ret;
}

SOL_API int
sol_flower_power_send_packet(struct sol_flow_node *src,
    uint16_t src_port, const struct sol_flower_power_data *fpd)
{
    struct sol_flow_packet *packet;
    int ret;

    packet = sol_flower_power_new_packet(fpd);
    SOL_NULL_CHECK(packet, -ENOMEM);

    ret = sol_flow_send_packet(src, src_port, packet);
    if (ret != 0)
        sol_flow_packet_del(packet);

    return ret;
}

SOL_API int
sol_flower_power_send_packet_components(struct sol_flow_node *src,
    uint16_t src_port, char *id, char *timestamp,
    struct sol_drange *fertilizer, struct sol_drange *light,
    struct sol_drange *temperature, struct sol_drange *water)
{
    struct sol_flow_packet *packet;
    int ret;

    packet = sol_flower_power_new_packet_components(id, timestamp,
        fertilizer, light, temperature, water);
    SOL_NULL_CHECK(packet, -ENOMEM);

    ret = sol_flow_send_packet(src, src_port, packet);
    if (ret != 0)
        sol_flow_packet_del(packet);

    return ret;
}

struct http_get_data {
    struct sol_flow_node *node;
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

    return 0;

open_error:
    free(mdata->client_id);
    return -ENOMEM;
}

static void
http_get_close(struct sol_flow_node *node, void *data)
{
    struct http_get_data *mdata = data;

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

    /* FIXME: Cancel pending connections. Need HTTP API. */
}

#define RESPONSE_CHECK_API(response_, mdata_) \
    do { \
        if (unlikely(!response_)) { \
            sol_flow_send_error_packet(mdata_->node, EINVAL, \
                "Error while reaching service."); \
            return; \
        } \
        if (unlikely(response_->api_version != SOL_HTTP_RESPONSE_API_VERSION)) { \
            SOL_ERR("Unexpected API version (response is %u, expected %u)", \
                response->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
            return; \
        } \
    } while (0)

#define BASE_URL "https://apiflowerpower.parrot.com/"
#define STATUS_URL BASE_URL "sensor_data/v4/garden_locations_status"
#define AUTH_URL BASE_URL "user/v1/authenticate"
#define AUTH_START "Bearer "

static void
generate_token_cb(void *data, struct sol_http_response *response)
{
    struct http_get_data *mdata = data;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    const size_t auth_len = strlen(AUTH_START);

    RESPONSE_CHECK_API(response, mdata);

    if (response->response_code != 200) {
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
        size_t token_size;

        if (!sol_json_token_str_eq(&key, "access_token",
            strlen("access_token")))
            continue;

        /* value is between double quotes */
        token_size = value.end - value.start + auth_len - 1;

        free(mdata->token);
        mdata->token = malloc(token_size);
        SOL_NULL_CHECK(mdata->token);

        strcpy(mdata->token, AUTH_START);
        memcpy(mdata->token + auth_len, value.start + 1,
            value.end - value.start - 2);
        *(mdata->token + token_size - 1) = '\0';

        return;
    }

    sol_flow_send_error_packet(mdata->node, ENOKEY,
        "Server response doesn't contain a token.");
}

static int
generate_token(struct http_get_data *mdata)
{
    struct sol_http_param params;
    int r;

    sol_http_param_init(&params);
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
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    r = sol_http_client_request(SOL_HTTP_METHOD_GET, AUTH_URL,
        &params, generate_token_cb, mdata);

    sol_http_param_free(&params);

    if (r < 0) {
        SOL_WRN("Could not create HTTP request for %s", AUTH_URL);
        return r;
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
        sol_flow_send_error_packet(node, -EINVAL, "Invalid password");
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
get_measure(struct sol_json_token *measure_token, struct sol_drange *measure)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    bool found_key = false, found_cur = false;

    sol_json_scanner_init_from_token(&scanner, measure_token);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (sol_json_token_str_eq(&key, "gauge_values",
            strlen("gauge_values"))) {
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
        if (sol_json_token_str_eq(&key, "current_value", strlen("current_value"))) {
            if (sol_json_token_get_double(&value, &measure->val)) {
                SOL_WRN("Failed to get current value");
                return false;
            }
            found_cur = true;
        } else if (sol_json_token_str_eq(&key, "max_threshold",
            strlen("max_threshold"))) {
            if (sol_json_token_get_double(&value, &measure->max))
                SOL_DBG("Failed to get max value");
        } else if (sol_json_token_str_eq(&key, "min_threshold",
            strlen("min_threshold"))) {
            if (sol_json_token_get_double(&value, &measure->min))
                SOL_DBG("Failed to get min value");
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK)
        return false;

    return found_cur;
}

static void
http_get_cb(void *data, struct sol_http_response *response)
{
    struct http_get_data *mdata = data;
    struct sol_json_scanner scanner, locations_scanner;
    struct sol_json_token token, key, value;
    int r;
    enum sol_json_loop_reason reason;
    bool found_locations = false;

    RESPONSE_CHECK_API(response, mdata);

    if (response->response_code != 200) {
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
        if (sol_json_token_str_eq(&key, "locations", strlen("locations"))) {
            found_locations = true;
            break;
        }
    }
    if (reason != SOL_JSON_LOOP_REASON_OK)
        goto error;

    if (!found_locations) {
        SOL_DBG("No plants found on response");
        return;
    }

    sol_json_scanner_init_from_token(&locations_scanner, &value);
    SOL_JSON_SCANNER_ARRAY_LOOP (&locations_scanner, &token,
        SOL_JSON_TYPE_OBJECT_START, reason) {
        struct sol_drange fertilizer = { 0, -DBL_MAX, DBL_MAX, DBL_MIN };
        struct sol_drange water = { 0, -DBL_MAX, DBL_MAX, DBL_MIN };
        struct sol_drange temperature = { 0, -DBL_MAX, DBL_MAX, DBL_MIN };
        struct sol_drange light = { 0, -DBL_MAX, DBL_MAX, DBL_MIN };
        char *id = NULL, *timestamp = NULL;

        SOL_JSON_SCANNER_OBJECT_LOOP_NEST (&locations_scanner, &token,
            &key, &value, reason) {
            if (sol_json_token_str_eq(&key, "fertilizer",
                strlen("fertilizer"))) {
                if (!get_measure(&value, &fertilizer)) {
                    SOL_WRN("Failed to get fertilizer info");
                    goto error;
                }
            } else if (sol_json_token_str_eq(&key, "light",
                strlen("light"))) {
                if (!get_measure(&value, &light)) {
                    SOL_WRN("Failed to get light info");
                    goto error;
                }
            } else if (sol_json_token_str_eq(&key, "air_temperature",
                strlen("air_temperature"))) {
                if (!get_measure(&value, &temperature)) {
                    SOL_WRN("Failed to get temperature info");
                    goto error;
                }
            } else if (sol_json_token_str_eq(&key, "soil_moisture",
                strlen("soil_moisture"))) {
                if (!get_measure(&value, &water)) {
                    SOL_WRN("Failed to get water info");
                    goto error;
                }
            } else if (sol_json_token_str_eq(&key, "location_identifier",
                strlen("location_identifier"))) {
                id = strndupa(value.start + 1, value.end - value.start - 2);
                if (!id) {
                    SOL_WRN("Failed to get id");
                    goto error;
                }
            } else if (sol_json_token_str_eq(&key, "last_sample_upload",
                strlen("last_sample_upload"))) {
                timestamp = strndupa(value.start + 1,
                    value.end - value.start - 2);
                if (!timestamp) {
                    SOL_WRN("Failed to get timestamp");
                    goto error;
                }
            }
        }
        r = sol_flower_power_send_packet_components(mdata->node,
            SOL_FLOW_NODE_TYPE_FLOWER_POWER_HTTP_GET__OUT__OUT,
            id, timestamp, &fertilizer, &light, &temperature, &water);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    return;

error:
    sol_flow_send_error_packet(mdata->node, EINVAL,
        "Error while parsing server response.");
}

static int
http_get_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct http_get_data *mdata = data;
    struct sol_http_param params;
    int r;

    if (!mdata->token) {
        sol_flow_send_error_packet(node, EINVAL, "Missing valid token");
        return 0;
    }

    sol_http_param_init(&params);
    if (!sol_http_param_add(&params,
        SOL_HTTP_REQUEST_PARAM_HEADER("Authorization", mdata->token))) {
        SOL_WRN("Failed to set query params");
        sol_http_param_free(&params);
        return -ENOMEM;
    }

    r = sol_http_client_request(SOL_HTTP_METHOD_GET, STATUS_URL,
        &params, http_get_cb, mdata);

    sol_http_param_free(&params);

    if (r < 0) {
        SOL_WRN("Could not create HTTP request for %s", STATUS_URL);
        return r;
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
        sol_flow_send_error_packet(node, -EINVAL, "Invalid username");
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
    struct sol_drange fertilizer, light, temperature, water;
    char *id, *timestamp;
    int r;

    r = sol_flower_power_get_packet_components(packet, &id, &timestamp,
        &fertilizer, &light, &temperature, &water);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__ID,
        id);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__TIMESTAMP,
        timestamp);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__FERTILIZER,
        &fertilizer);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__LIGHT,
        &light);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__TEMPERATURE,
        &temperature);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_GET_VALUE__OUT__WATER,
        &water);
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
        sol_flow_send_error_packet(node, -EINVAL, "Invalid plant ids");
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
        sol_flow_send_error_packet(node, -EINVAL,
            "Failed to compare plant ids");
        return -EINVAL;
    }

    /* Don't forward packets if ids don't match */
    if (strcmp(fpd.id, mdata->id))
        return 0;

    return sol_flower_power_send_packet(node,
        SOL_FLOW_NODE_TYPE_FLOWER_POWER_FILTER_ID__OUT__OUT, &fpd);
}

#undef RESPONSE_CHECK_API
#include "flower-power-gen.c"
