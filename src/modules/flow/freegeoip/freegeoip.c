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

#include "freegeoip-gen.h"
#include "sol-flow-internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include <sol-http-client.h>
#include <sol-json.h>
#include <sol-mainloop.h>
#include <sol-util.h>

struct freegeoip_data {
    struct sol_flow_node *node;
    char *endpoint;
};

#define RESPONSE_CHECK_API(response_, mdata_) \
    do { \
        if (unlikely(!response_)) { \
            sol_flow_send_error_packet(mdata_->node, EINVAL, \
                "Error while reaching Freegeoip"); \
            return; \
        } \
        if (unlikely(response_->api_version != SOL_HTTP_RESPONSE_API_VERSION)) { \
            SOL_ERR("Unexpected API version (response is %u, expected %u)", \
                response->api_version, SOL_HTTP_RESPONSE_API_VERSION); \
            return; \
        } \
    } while (0)

enum fields {
    FIELD_IP = 1<<0,
    FIELD_COUNTRY_CODE = 1<<1,
    FIELD_COUNTRY_NAME = 1<<2,
    FIELD_REGION_CODE = 1<<3,
    FIELD_CITY_NAME = 1<<4,
    FIELD_ZIP_CODE = 1<<5,
    FIELD_TIMEZONE = 1<<6,
    FIELD_LATITUDE = 1<<7,
    FIELD_LONGITUDE = 1<<8,
    FIELD_ALL = 0x1ff
};

static bool
json_token_to_double(const struct sol_json_token *token, double *output)
{
    char *str = strndupa(token->start, token->end - token->start);
    char *endptr;
    double value;

    errno = 0;
    value = strtod(str, &endptr);
    if (errno != 0)
        return false;
    if (!endptr || *endptr)
        return false;
    *output = value;
    return true;
}

static void
freegeoip_query_finished(void *data, struct sol_http_response *response)
{
    struct freegeoip_data *mdata = data;
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    enum fields fields = FIELD_ALL;
    double latitude = 0.0f;
    double longitude = 0.0f;

    RESPONSE_CHECK_API(response, mdata);

    if (!response->content.used) {
        SOL_WRN("Empty response from FreeGeoIP");
        return;
    }

    if (response->response_code != 200) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "FreeGeoIP returned an unknown response code: %d",
            response->response_code);
        return;
    }

#define FIELD(json_field_, flow_field_) \
    do { \
        if (fields & FIELD_ ## flow_field_ && sol_json_token_str_eq(&key, \
                json_field_, strlen(json_field_))) { \
            fields &= ~(FIELD_ ## flow_field_); \
            sol_flow_send_string_packet(mdata->node, \
                SOL_FLOW_NODE_TYPE_LOCATION_FREEGEOIP__OUT__ ## flow_field_, \
                strndupa(value.start + 1, value.end - value.start - 2)); \
            continue; \
        } \
    } while (0)

    sol_json_scanner_init(&scanner, response->content.data, response->content.used);
    SOL_JSON_SCANNER_OBJECT_LOOP(&scanner, &token, &key, &value, reason) {
        FIELD("ip", IP);
        FIELD("country_name", COUNTRY_NAME);
        FIELD("country_code", COUNTRY_CODE);
        FIELD("city", CITY_NAME);
        FIELD("zip_code", ZIP_CODE);
        FIELD("time_zone", TIMEZONE);

        if (fields & FIELD_LATITUDE && sol_json_token_str_eq(&key, "latitude", strlen("latitude"))) {
            if (json_token_to_double(&value, &latitude))
                fields &= ~FIELD_LATITUDE;
            continue;
        }

        if (fields & FIELD_LONGITUDE && sol_json_token_str_eq(&key, "longitude", strlen("longitude"))) {
            if (json_token_to_double(&value, &longitude))
                fields &= ~FIELD_LONGITUDE;
            continue;
        }
    }

#undef FIELD

    if (reason != SOL_JSON_LOOP_REASON_OK)
        return;

    if (!(fields & (FIELD_LATITUDE | FIELD_LONGITUDE))) {
        sol_flow_send_location_latlon_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_LOCATION_FREEGEOIP__OUT__LOCATION, latitude,
            longitude);
    }
}

static int
query_addr(struct freegeoip_data *mdata, const char *addr)
{
    char json_endpoint[PATH_MAX];
    int r;

    r = snprintf(json_endpoint, sizeof(json_endpoint), "%s/json/%s",
        mdata->endpoint, addr ? addr : "");
    if (r < 0 || r >= (int)sizeof(json_endpoint)) {
        SOL_WRN("Could not prepare endpoint");
        return -EINVAL;
    }

    r = sol_http_client_request(SOL_HTTP_METHOD_GET, json_endpoint,
        NULL, freegeoip_query_finished, mdata);

    if (r < 0) {
        SOL_WRN("Could not create HTTP request");
        return -ENOTCONN;
    }

    return 0;
}

static int
freegeoip_in_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct freegeoip_data *mdata = data;

    return query_addr(mdata, NULL);
}

static int
freegeoip_ip_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct freegeoip_data *mdata = data;
    const char *addr;
    int r;

    r = sol_flow_packet_get_string(packet, &addr);
    if (r < 0)
        return r;

    return query_addr(mdata, addr);
}

static void
freegeoip_close(struct sol_flow_node *node, void *data)
{
    struct freegeoip_data *mdata = data;

    free(mdata->endpoint);
}

static int
freegeoip_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct freegeoip_data *mdata = data;
    const struct sol_flow_node_type_location_freegeoip_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_LOCATION_FREEGEOIP_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_location_freegeoip_options *)options;

    mdata->node = node;
    mdata->endpoint = strdup(opts->endpoint);
    if (!mdata->endpoint)
        return -ENOMEM;

    return 0;
}

#include "freegeoip-gen.c"
