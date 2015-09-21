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
#include <string.h>

#include "sol-flow/key-value.h"
#include "sol-types.h"

struct key_value_data {
    struct sol_key_value kv;
    bool default_is_valid;
};

struct key_value_node_type {
    struct sol_flow_node_type base;
    int (*value_process_func) (const struct sol_flow_packet *packet,
        struct key_value_data *kv_data);
    int (*open_func) (struct sol_flow_node *node,
        struct key_value_data *kv_data,
        const struct sol_flow_node_options *options);
    bool (*can_send_packet_func) (struct key_value_data *kv_data);
};

struct key_value_splitter_node_type {
    struct sol_flow_node_type base;
    int (*send_value_packet_func) (struct sol_flow_node *node,
        struct sol_key_value *kv);
};

static void
key_value_close(struct sol_flow_node *node, void *data)
{
    struct key_value_data *kv_data = data;

    sol_key_value_clear(&kv_data->kv);
}

static int
key_value_string_open(struct sol_flow_node *node,
    struct key_value_data *kv_data,
    const struct sol_flow_node_options *options)
{
    int r;
    const struct sol_flow_node_type_key_value_string_options *opts =
        (const struct sol_flow_node_type_key_value_string_options *)options;

    r = sol_key_value_string_init(&kv_data->kv, opts->key, opts->value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
key_value_int_open(struct sol_flow_node *node, struct key_value_data *kv_data,
    const struct sol_flow_node_options *options)
{
    int r;
    const struct sol_flow_node_type_key_value_int_options *opts =
        (const struct sol_flow_node_type_key_value_int_options *)options;

    r = sol_key_value_int_init(&kv_data->kv, opts->key, opts->value.val);
    SOL_INT_CHECK(r, < 0, r);

    kv_data->default_is_valid = opts->default_is_valid;
    return 0;
}

static int
key_value_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct key_value_data *kv_data = data;
    const struct key_value_node_type *type;

    type = (const struct key_value_node_type *)
        sol_flow_node_get_type(node);

    r = type->open_func(node, kv_data, options);
    SOL_INT_CHECK(r, < 0, r);

    if (type->can_send_packet_func(kv_data))
        return sol_flow_send_key_value_packet(node, 0, &kv_data->kv);

    return 0;
}

static int
key_value_int_value_process(const struct sol_flow_packet *packet,
    struct key_value_data *kv_data)
{
    struct sol_irange irange;
    int r;

    r = sol_flow_packet_get_irange(packet, &irange);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_key_value_set_int_value(&kv_data->kv, irange.val);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
key_value_string_process(const struct sol_flow_packet *packet,
    struct key_value_data *kv_data)
{
    const char *str;
    int r;

    r = sol_flow_packet_get_string(packet, &str);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_key_value_set_string_value(&kv_data->kv, str);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static bool
key_value_string_can_send_packet(struct key_value_data *kv_data)
{
    if (kv_data->kv.key && kv_data->kv.value.s)
        return true;
    return false;
}

static bool
key_value_int_can_send_packet(struct key_value_data *kv_data)
{
    if (kv_data->kv.key && ((kv_data->kv.value.i != 0) ||
        (kv_data->kv.value.i == 0 && kv_data->default_is_valid)))
        return true;
    return false;
}

static int
value_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct key_value_data *kv_data = data;
    const struct key_value_node_type *type;
    int r;


    type = (const struct key_value_node_type *)
        sol_flow_node_get_type(node);

    r = type->value_process_func(packet, kv_data);
    SOL_INT_CHECK(r, < 0, r);

    if (type->can_send_packet_func(kv_data))
        return sol_flow_send_key_value_packet(node, 0, &kv_data->kv);

    return 0;
}


static int
key_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct key_value_data *kv_data = data;
    const char *str;
    const struct key_value_node_type *type;
    int r;

    r = sol_flow_packet_get_string(packet, &str);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_key_value_set_key(&kv_data->kv, str);
    SOL_INT_CHECK(r, < 0, r);

    type = (const struct key_value_node_type *)
        sol_flow_node_get_type(node);

    if (type->can_send_packet_func(kv_data))
        return sol_flow_send_key_value_packet(node, 0, &kv_data->kv);

    return 0;
}

static int
send_string_value_packet(struct sol_flow_node *node, struct sol_key_value *kv)
{
    if (kv->type != SOL_KEY_VALUE_TYPE_STRING)
        return -EINVAL;
    return sol_flow_send_string_packet(node, 1, kv->value.s);
}

static int
send_int_value_packet(struct sol_flow_node *node, struct sol_key_value *kv)
{
    struct sol_irange irange;

    if (kv->type != SOL_KEY_VALUE_TYPE_INT)
        return -EINVAL;

    irange.val = irange.min = irange.max = kv->value.i;
    irange.step = 0;
    return sol_flow_send_irange_packet(node, 1, &irange);
}

static int
key_value_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_key_value kv;
    const struct key_value_splitter_node_type *type;
    int r;

    type = (const struct key_value_splitter_node_type *)
        sol_flow_node_get_type(node);

    r = sol_flow_packet_get_key_value(packet, &kv);
    SOL_INT_CHECK(r, < 0, r);

    r = type->send_value_packet_func(node, &kv);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_string_packet(node, 0, kv.key);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

#include "key-value-gen.c"
