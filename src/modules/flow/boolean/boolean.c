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

#include "sol-flow/boolean.h"
#include "sol-flow-internal.h"
#include "sol-mainloop.h"
#include "sol-vector.h"
#include "sol-str-table.h"

#include <stdlib.h>
#include <string.h>

struct boolean_data {
    bool init_in0 : 1;
    bool init_in1 : 1;
    bool in0 : 1;
    bool in1 : 1;
};

#define MULTI_LEN (SOL_FLOW_NODE_TYPE_BOOLEAN_AND__IN__IN_LAST + 1)

struct multi_boolean_data {
    uint32_t initialized;
    uint32_t connected;
    bool vals[MULTI_LEN];
};

static int
two_ports_process(struct sol_flow_node *node, void *data, uint16_t port_in, uint16_t port_out,
    const struct sol_flow_packet *packet, bool (*func) (bool in0, bool in1))
{
    int r;
    bool b;
    struct boolean_data *mdata = data;

    r = sol_flow_packet_get_boolean(packet, &b);
    SOL_INT_CHECK(r, < 0, r);

    if (port_in) {
        mdata->init_in1 = true;
        mdata->in1 = b;
    } else {
        mdata->init_in0 = true;
        mdata->in0 = b;
    }

    if (mdata->init_in0 && mdata->init_in1) {
        b = func(mdata->in0, mdata->in1);
        return sol_flow_send_boolean_packet(node, port_out, b);
    }

    return 0;
}

static int
multi_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct multi_boolean_data *mdata = data;

    mdata->connected |= 1u << port;
    return 0;
}

static int
multi_ports_process(struct sol_flow_node *node, void *data, uint16_t port_in, uint16_t port_out, const struct sol_flow_packet *packet, bool (*func) (bool in0, bool in1))
{
    struct multi_boolean_data *mdata = data;
    uint32_t ports;
    int r;
    uint8_t i;
    bool result;

    r = sol_flow_packet_get_boolean(packet, &mdata->vals[port_in]);
    SOL_INT_CHECK(r, < 0, r);

    mdata->initialized |= 1u << port_in;
    /* waits until at least a packet is received in each connected port */
    if (mdata->initialized != mdata->connected)
        return 0;

    if (unlikely(!(ports = mdata->initialized)))
        return 0;

    for (i = 0; !(ports & 1); i++)
        ports >>= 1;

    result = mdata->vals[i++];
    while ((ports >>= 1)) {
        if (ports & 1)
            result = func(result, mdata->vals[i]);
        i++;
    }

    return sol_flow_send_boolean_packet(node, port_out, result);
}

/* AND *****************************************************************/

static bool
and_func(bool in0, bool in1)
{
    return in0 && in1;
}

static int
and_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return multi_ports_process(node, data, port,
        SOL_FLOW_NODE_TYPE_BOOLEAN_AND__OUT__OUT, packet,
        and_func);
}

/* OR *****************************************************************/

static bool
or_func(bool in0, bool in1)
{
    return in0 || in1;
}

static int
or_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return multi_ports_process(node, data, port,
        SOL_FLOW_NODE_TYPE_BOOLEAN_OR__OUT__OUT, packet,
        or_func);
}

/* XOR ****************************************************************/

static bool
xor_func(bool in0, bool in1)
{
    return in0 ^ in1;
}

static int
xor_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    return two_ports_process(node, data, port, SOL_FLOW_NODE_TYPE_BOOLEAN_XOR__OUT__OUT, packet,
        xor_func);
}

/* NOT ********************************************************************/
static int
not_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    bool in;

    r = sol_flow_packet_get_boolean(packet, &in);
    SOL_INT_CHECK(r, < 0, r);

    return sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_BOOLEAN_NOT__OUT__OUT, !in);
}

/* TOGGLE *****************************************************************/
struct toggle_data {
    bool state;
};

static int
toggle_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct toggle_data *mdata = data;

    mdata->state = !mdata->state;

    return sol_flow_send_boolean_packet(node, SOL_FLOW_NODE_TYPE_BOOLEAN_TOGGLE__OUT__OUT,
        mdata->state);
}

static int
toggle_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct toggle_data *mdata = data;
    const struct sol_flow_node_type_boolean_toggle_options *opts =
        (const struct sol_flow_node_type_boolean_toggle_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_BOOLEAN_TOGGLE_OPTIONS_API_VERSION, -EINVAL);

    mdata->state = opts->initial_state;
    return 0;
}

/* COUNTER ***********************************************************/
enum state {
    FALSE,
    TRUE,
    NA = -1
};

struct counter_data {
    struct sol_vector map;
    uint16_t true_count;
    uint16_t false_count;
};

static int
counter_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct counter_data *mdata = data;
    enum state *s;

    s = sol_vector_append(&mdata->map);
    SOL_NULL_CHECK(s, -ENOMEM);
    *s = NA;
    return 0;
}

static int
counter_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    struct counter_data *mdata = data;
    enum state *s;

    s = sol_vector_get(&mdata->map, conn_id);
    if (*s == TRUE)
        mdata->true_count--;
    if (*s == FALSE)
        mdata->false_count--;

    *s = NA;
    return 0;
}

static int
counter_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct sol_irange val = { 0, 0, INT32_MAX, 1 };
    struct counter_data *mdata = data;
    enum state *s;
    bool packet_val;
    int r;

    r = sol_flow_packet_get_boolean(packet, &packet_val);
    SOL_INT_CHECK(r, < 0, r);
    s = sol_vector_get(&mdata->map, conn_id);

    if (*s == packet_val)
        return 0;

    if (packet_val) {
        mdata->true_count++;
        if (*s != NA)
            mdata->false_count--;
    } else {
        mdata->false_count++;
        if (*s != NA)
            mdata->true_count--;
    }
    *s = packet_val;

    val.val = mdata->true_count;
    sol_flow_send_irange_packet(node, SOL_FLOW_NODE_TYPE_BOOLEAN_COUNTER__OUT__TRUE, &val);

    val.val = mdata->false_count;
    sol_flow_send_irange_packet(node, SOL_FLOW_NODE_TYPE_BOOLEAN_COUNTER__OUT__FALSE, &val);

    return 0;
}

static int
counter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct counter_data *mdata = data;

    sol_vector_init(&mdata->map, sizeof(enum state));
    return 0;
}

static void
counter_close(struct sol_flow_node *node, void *data)
{
    struct counter_data *mdata = data;

    sol_vector_clear(&mdata->map);
}

/* FILTER ***********************************************************/

static int
filter_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    bool packet_val;
    uint16_t out_port;
    int r;

    r = sol_flow_packet_get_boolean(packet, &packet_val);
    SOL_INT_CHECK(r, < 0, r);
    if (packet_val)
        out_port = SOL_FLOW_NODE_TYPE_BOOLEAN_FILTER__OUT__TRUE;
    else
        out_port = SOL_FLOW_NODE_TYPE_BOOLEAN_FILTER__OUT__FALSE;
    return sol_flow_send_boolean_packet(node, out_port, packet_val);
}


// =============================================================================
// BOOLEAN BUFFER
// =============================================================================

struct boolean_buffer_data {
    struct sol_flow_node *node;
    struct sol_timeout *timer;
    bool *input_queue;
    bool (*normalize_cb)(const bool *const values, size_t len);
    size_t cur_len;
    size_t n_samples;
    uint32_t timeout;
    bool circular : 1;
    bool all_initialized : 1;
    bool changed : 1;
};

// =============================================================================
// Normalizing Functions
// =============================================================================

static bool
_normalize_all_true(const bool *const values, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (values[i] == false) {
            return false;
        }

    return true;
}

static bool
_normalize_all_false(const bool *const values, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (values[i] == true) {
            return false;
        }

    return true;
}

static bool
_normalize_any_true(const bool *const values, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (values[i] == true) {
            return true;
        }

    return false;
}

static bool
_normalize_any_false(const bool *const values, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (values[i] == false) {
            return true;
        }

    return false;
}

static int
_boolean_buffer_do(struct boolean_buffer_data *mdata)
{
    bool result;

    if (!mdata->cur_len)
        return 0;

    if (mdata->circular && !mdata->changed)
        return 0;

    if (mdata->circular && mdata->all_initialized)
        result = mdata->normalize_cb(mdata->input_queue, mdata->n_samples);
    else
        result = mdata->normalize_cb(mdata->input_queue, mdata->cur_len);

    mdata->changed = false;

    return sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_BOOLEAN_BUFFER__OUT__OUT, result);
}

static bool
_timeout(void *data)
{
    struct boolean_buffer_data *mdata = data;

    _boolean_buffer_do(mdata);

    if (!mdata->circular)
        mdata->cur_len = 0;

    return true;
}

static void
_reset_len(struct boolean_buffer_data *mdata)
{
    mdata->cur_len = 0;
}

static void
_reset_timer(struct boolean_buffer_data *mdata)
{
    if (mdata->timer) {
        sol_timeout_del(mdata->timer);
        mdata->timer = NULL;
    }
    if (mdata->timeout)
        mdata->timer = sol_timeout_add(mdata->timeout, _timeout, mdata);
}

static void
_reset(struct boolean_buffer_data *mdata)
{
    _reset_len(mdata);
    _reset_timer(mdata);
}

static int
boolean_buffer_reset(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    _reset(data);
    return 0;
}

static int
boolean_buffer_timeout(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    int32_t timeout;
    struct boolean_buffer_data *mdata = data;

    r = sol_flow_packet_get_irange_value(packet, &timeout);
    SOL_INT_CHECK(r, < 0, r);

    if (timeout < 0) {
        sol_flow_send_error_packet(node, EINVAL,
            "Invalid 'timeout' value: '%" PRId32 "'. Skipping it.", timeout);
        return 0;
    }

    mdata->timeout = timeout;

    if (mdata->timer) {
        sol_timeout_del(mdata->timer);
        mdata->timer = NULL;
    }
    if (mdata->timeout)
        mdata->timer = sol_timeout_add(mdata->timeout, _timeout, mdata);

    return 0;
}

static int
boolean_buffer_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    int r;
    struct boolean_buffer_data *mdata = data;

    r = sol_flow_packet_get_boolean(packet,
        &mdata->input_queue[mdata->cur_len]);
    SOL_INT_CHECK(r, < 0, r);

    mdata->cur_len++;
    mdata->changed = true;

    if (mdata->circular && mdata->all_initialized) {
        r = _boolean_buffer_do(mdata);
        _reset_timer(mdata);
        if (mdata->n_samples == mdata->cur_len) {
            _reset_len(mdata);
        }
    } else if (mdata->n_samples == mdata->cur_len) {
        mdata->all_initialized = true;
        r = _boolean_buffer_do(mdata);
        _reset(mdata);
    }

    return r;
}

static const struct sol_str_table_ptr table[] = {
    SOL_STR_TABLE_PTR_ITEM("all_true", _normalize_all_true),
    SOL_STR_TABLE_PTR_ITEM("all_false", _normalize_all_false),
    SOL_STR_TABLE_PTR_ITEM("any_true", _normalize_any_true),
    SOL_STR_TABLE_PTR_ITEM("any_false", _normalize_any_false),
    { }
};

static int
boolean_buffer_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct boolean_buffer_data *mdata = data;
    const struct sol_flow_node_type_boolean_buffer_options *def_opts;
    const struct sol_flow_node_type_boolean_buffer_options *opts =
        (const struct sol_flow_node_type_boolean_buffer_options *)options;

    mdata->node = node;
    def_opts = node->type->default_options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK
        (options, SOL_FLOW_NODE_TYPE_BOOLEAN_BUFFER_OPTIONS_API_VERSION,
        -EINVAL);

    mdata->n_samples = opts->samples;
    if (opts->samples <= 0) {
        SOL_WRN("Invalid samples (%" PRId32 "). Must be positive. "
            "Set to %" PRId32 ".", opts->samples, def_opts->samples);
        mdata->n_samples = def_opts->samples;
    }

    mdata->timeout = opts->timeout;
    if (opts->timeout < 0) {
        SOL_WRN("Invalid timeout (%" PRId32 "). Must be non negative. "
            "Set to 0.", opts->timeout);
        mdata->timeout = 0;
    }

    mdata->normalize_cb = sol_str_table_ptr_lookup_fallback
            (table, sol_str_slice_from_str(opts->operation), NULL);
    if (!mdata->normalize_cb) {
        SOL_WRN("Operation %s not supported. Setting operation to 'all_true'",
            opts->operation);
        mdata->normalize_cb = _normalize_all_true;
    }

    mdata->input_queue = calloc(mdata->n_samples, sizeof(*mdata->input_queue));
    SOL_NULL_CHECK(mdata->input_queue, -ENOMEM);

    mdata->circular = opts->circular;

    if (mdata->timeout > 0)
        mdata->timer = sol_timeout_add(mdata->timeout, _timeout, mdata);

    return 0;
}

static void
boolean_buffer_close(struct sol_flow_node *node, void *data)
{
    struct boolean_buffer_data *mdata = data;

    if (mdata->timer)
        sol_timeout_del(mdata->timer);

    free(mdata->input_queue);
}

#include "boolean-gen.c"
