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

#include "sol-flow/tuple.h"
#include "sol-types.h"

struct tuple_data {
    char *key;
    char *value;
};

static void
tuple_close(struct sol_flow_node *node, void *data)
{
    struct tuple_data *tdata = data;

    free(tdata->key);
    free(tdata->value);
}

static int
send_packet_if_possible(struct sol_flow_node *node, struct tuple_data *tdata)
{
    struct sol_flow_packet *packets[2], *tuple;
    int r;

    if (!tdata->key || !tdata->value)
        return 0;

    packets[0] = sol_flow_packet_new_string(tdata->key);
    SOL_NULL_CHECK(packets[0], -ENOMEM);
    packets[1] = sol_flow_packet_new_string(tdata->value);
    SOL_NULL_CHECK_GOTO(packets[1], err_value);

    tuple = sol_flow_packet_new(
        sol_flow_node_type_tuple_string_get_composed_string_string_packet_type(),
        packets);
    SOL_NULL_CHECK_GOTO(tuple, err_tuple);

    r = sol_flow_send_packet(node, 0, tuple);
    sol_flow_packet_del(packets[0]);
    sol_flow_packet_del(packets[1]);
    return r;

err_tuple:
    sol_flow_packet_del(packets[1]);
err_value:
    sol_flow_packet_del(packets[0]);
    return -ENOMEM;

    return 0;
}

static int
tuple_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct tuple_data *tdata = data;
    const struct sol_flow_node_type_tuple_string_options *opts =
        (const struct sol_flow_node_type_tuple_string_options *)options;
    int r;

    if (opts->key) {
        tdata->key = strdup(opts->key);
        SOL_NULL_CHECK(tdata->key, -ENOMEM);
    }

    if (opts->value) {
        tdata->value = strdup(opts->value);
        SOL_NULL_CHECK_GOTO(tdata->value, err_value);
    }

    r = send_packet_if_possible(node, tdata);
    SOL_INT_CHECK_GOTO(r, < 0, err_send);
    return 0;

err_send:
    free(tdata->value);
err_value:
    free(tdata->key);
    return -ENOMEM;
}

static int
replace_string_from_packet(const struct sol_flow_packet *packet, char **dst)
{
    const char *str;
    char *aux;
    int r;

    r = sol_flow_packet_get_string(packet, &str);
    SOL_INT_CHECK(r, < 0, r);
    aux = strdup(str);
    SOL_NULL_CHECK(*dst, -ENOMEM);
    free(*dst);
    *dst = aux;
    return 0;
}

static int
value_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct tuple_data *tdata = data;
    int r;

    r = replace_string_from_packet(packet, &tdata->value);
    SOL_INT_CHECK(r, < 0, r);
    return send_packet_if_possible(node, tdata);
}


static int
key_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct tuple_data *tdata = data;
    int r;

    r = replace_string_from_packet(packet, &tdata->key);
    SOL_INT_CHECK(r, < 0, r);
    return send_packet_if_possible(node, tdata);
}

static int
tuple_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_flow_packet *array[2];
    const struct sol_flow_packet_type *p_type;
    const char *k, *v;
    int r;

    p_type = sol_flow_packet_get_type(packet);
    if (p_type !=
        sol_flow_node_type_tuple_splitter_get_composed_string_string_packet_type()) {
        SOL_ERR("Not a Composed String-String packet. Type:%s", p_type->name);
        return -EINVAL;
    }

    r = sol_flow_packet_get(packet, &array);
    SOL_INT_CHECK(r, < 0, r);

    p_type = sol_flow_packet_get_type(array[0]);

    if (p_type != SOL_FLOW_PACKET_TYPE_STRING) {
        SOL_ERR("The tuple key is not a string, type:%s", p_type->name);
        return -EINVAL;
    }

    p_type = sol_flow_packet_get_type(array[1]);
    if (p_type != SOL_FLOW_PACKET_TYPE_STRING) {
        SOL_ERR("The tuple value is not a string, type:%s", p_type->name);
        return -EINVAL;
    }

    r = sol_flow_packet_get_string(array[0], &k);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_flow_packet_get_string(array[1], &v);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_string_packet(node, 0, k);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_flow_send_string_packet(node, 1, v);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

#include "tuple-gen.c"
