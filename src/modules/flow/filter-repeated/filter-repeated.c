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

#include "sol-flow/filter-repeated.h"
#include "sol-flow-internal.h"
#include "sol-util.h"


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

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized && in_value == mdata->value)
        return 0;

    mdata->initialized = true;
    mdata->value = in_value;

    return sol_flow_send_boolean_packet(node,
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
        sol_flow_send_error_packet(node, errno, sol_util_strerrora(errno));
        return -errno;
    }
    mdata->code = code_value;

    return sol_flow_send_error_packet(node, code_value, in_value);
}

static int
float_filter(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct filter_drange_data *mdata = data;
    int r;
    struct sol_drange in_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->initialized && sol_drange_equal(&in_value, &mdata->value))
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

    if (mdata->initialized && sol_irange_equal(&in_value, &mdata->value))
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
        sol_flow_send_error_packet(node, errno, sol_util_strerrora(errno));
        return -errno;
    }

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_FILTER_REPEATED_STRING__OUT__OUT, in_value);
}

#include "filter-repeated-gen.c"
