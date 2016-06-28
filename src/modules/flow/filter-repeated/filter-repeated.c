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

#include <errno.h>

#include "sol-flow/filter-repeated.h"
#include "sol-flow-internal.h"
#include "sol-util-internal.h"


struct filter_boolean_data {
    bool value;
    bool initialized : 1;
};

struct filter_byte_data {
    unsigned char value;
    bool initialized : 1;
};

struct filter_error_data {
    char *msg;
    int code;
    bool initialized : 1;
};

struct filter_drange_data {
    struct sol_drange value;
    bool initialized : 1;
};

struct filter_irange_data {
    struct sol_irange value;
    bool initialized : 1;
};

struct filter_rgb_data {
    struct sol_rgb value;
    bool initialized : 1;
};

struct filter_direction_vector_data {
    struct sol_direction_vector value;
    bool initialized : 1;
};

struct filter_string_data {
    char *value;
};

static int
boolean_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_boolean_data *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_bool(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized && in_value == mdata->value)
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_bool_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_BOOLEAN__OUT__OUT, in_value);
}

static int
byte_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_byte_data *mdata = data;
    int r;
    unsigned char in_value;

    r = sol_flow_packet_get_byte(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized && in_value == mdata->value)
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_BYTE__OUT__OUT, in_value);
}

static void
error_close(struct sol_flow_node *node, void *data)
{
    struct filter_error_data *mdata = data;

    free(mdata->msg);
}

static int
error_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_error_data *mdata = data;
    const char *in_value;
    int r, code_value;

    r = sol_flow_packet_get_error(packet, &code_value, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized &&
        mdata->code == code_value &&
        mdata->msg &&
        !strcmp(mdata->msg, in_value))
        return 0;

    free(mdata->msg);
    mdata->msg = strdup(in_value);
    if (!mdata->msg) {
        sol_flow_send_error_packet_errno(node, errno);
        return -errno;
    }
    mdata->code = code_value;
    mdata->initialized = true;

    return sol_flow_send_error_packet_str(node, code_value, in_value);
}

static int
float_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_drange_data *mdata = data;
    int r;
    struct sol_drange in_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized && sol_drange_eq(&in_value, &mdata->value))
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_FLOAT__OUT__OUT, &in_value);
}

static int
int_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_irange_data *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized && sol_irange_eq(&in_value, &mdata->value))
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_INT__OUT__OUT, &in_value);
}

static int
rgb_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_rgb_data *mdata = data;
    int r;
    struct sol_rgb in_value;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized &&
        !memcmp(&in_value, &mdata->value, sizeof(struct sol_rgb)))
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_RGB__OUT__OUT, &in_value);
}

static int
direction_vector_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_direction_vector_data *mdata = data;
    int r;
    struct sol_direction_vector in_value;

    r = sol_flow_packet_get_direction_vector(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized &&
        !memcmp(&in_value, &mdata->value, sizeof(struct sol_direction_vector)))
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_DIRECTION_VECTOR__OUT__OUT, &in_value);
}

static void
string_close(struct sol_flow_node *node, void *data)
{
    struct filter_string_data *mdata = data;

    free(mdata->value);
}

static int
string_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_string_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->value && !strcmp(mdata->value, in_value))
        return 0;

    free(mdata->value);
    mdata->value = strdup(in_value);
    if (!mdata->value) {
        sol_flow_send_error_packet_errno(node, errno);
        return -errno;
    }

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_STRING__OUT__OUT, in_value);
}

#include "filter-repeated-gen.c"
