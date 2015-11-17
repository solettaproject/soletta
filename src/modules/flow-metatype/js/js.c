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
#include <float.h>
#include <stdio.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-metatype-js");

#include "duktape.h"

#include "sol-arena.h"
#include "sol-flow-metatype.h"
#include "sol-log.h"
#include "sol-str-table.h"
#include "sol-util.h"

/**
 * JS metatype allows the usage of Javascript language to create new
 * and customizable node types.
 *
 * A JS node type is specified with one object containing each input
 * and output port declarations (name and type) and its callback
 * functions that will be trigged on the occurrence of certain events
 * like input/output ports processes, open/close processes, so forth
 * and so on.
 *
 * The Javascript code must contain an object:
 *
 *     - 'node': This object will be used to declare input and output ports
 *               and its callback functions that will be trigged on the occurence
 *               of certain events like input/output ports processes, open/close
 *               processes, so forth and so on.
 *
 * e.g.  var node = {
 *           in: [
 *               {
 *                   name: 'IN',
 *                   type: 'int',
 *                   process: function(v) {
 *                       sendPacket("OUT", 42);
 *                   }
 *               }
 *           ],
 *           out: [ { name: 'OUT', type: 'int' } ]
 *       };
 *
 */

/* Contains information specific to a type based on JS. */
struct flow_js_type {
    struct sol_flow_node_type base;

    struct sol_vector ports_in;
    struct sol_vector ports_out;

    struct sol_arena *str_arena;

    char *js_content_buf;
    size_t js_content_buf_len;
};

struct flow_js_port_in {
    struct sol_flow_port_type_in type;
    char *name;
    char *type_name;
};

struct flow_js_port_out {
    struct sol_flow_port_type_out type;
    char *name;
    char *type_name;
};

/* Contains information specific to a node of a JS node type. */
struct flow_js_data {
    /* Each node keeps its own JavaScript context and global object. */
    struct duk_context *duk_ctx;
};

enum {
    PORTS_IN_CONNECT_INDEX,
    PORTS_IN_DISCONNECT_INDEX,
    PORTS_IN_PROCESS_INDEX,
    PORTS_IN_METHODS_LENGTH,
};

enum {
    PORTS_OUT_CONNECT_INDEX,
    PORTS_OUT_DISCONNECT_INDEX,
    PORTS_OUT_METHODS_LENGTH,
};

static const char *
get_in_port_name(const struct sol_flow_node *node, uint16_t port)
{
    const struct flow_js_type *type = (const struct flow_js_type *)sol_flow_node_get_type(node);
    const struct flow_js_port_in *p;

    p = sol_vector_get(&type->ports_in, port);
    SOL_NULL_CHECK_MSG(p, "", "Couldn't get input port %d name.", port);

    return p->name;
}

static const char *
get_out_port_name(const struct sol_flow_node *node, uint16_t port)
{
    const struct flow_js_type *type = (const struct flow_js_type *)sol_flow_node_get_type(node);
    const struct flow_js_port_out *p;

    p = sol_vector_get(&type->ports_out, port);
    SOL_NULL_CHECK_MSG(p, "", "Couldn't get output port %d name.", port);

    return p->name;
}

static duk_ret_t
send_boolean_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    bool value;
    int r;

    value = duk_require_boolean(ctx, 1);

    r = sol_flow_send_boolean_packet(node, port, value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send boolean packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return r;
}

static duk_ret_t
send_byte_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    unsigned char value;
    int r;

    value = duk_require_int(ctx, 1);

    r = sol_flow_send_byte_packet(node, port, value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send byte packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return r;
}

static duk_ret_t
send_float_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    struct sol_drange value;
    int r;

    if (duk_is_number(ctx, 1)) {
        value.val = duk_require_number(ctx, 1);

        value.min = -DBL_MAX;
        value.max = DBL_MAX;
        value.step = DBL_MIN;
    } else {
        duk_require_object_coercible(ctx, 1);

        duk_get_prop_string(ctx, -1, "val");
        duk_get_prop_string(ctx, -2, "min");
        duk_get_prop_string(ctx, -3, "max");
        duk_get_prop_string(ctx, -4, "step");

        value.val = duk_require_number(ctx, -4);
        value.min = duk_require_number(ctx, -3);
        value.max = duk_require_number(ctx, -2);
        value.step = duk_require_number(ctx, -1);

        duk_pop_n(ctx, 4); /* step, max, min, val values */
    }

    r = sol_flow_send_drange_packet(node, port, &value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send float packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return r;
}

static duk_ret_t
send_int_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    struct sol_irange value;
    int r;

    if (duk_is_number(ctx, 1)) {
        value.val = duk_require_int(ctx, 1);

        value.min = INT32_MIN;
        value.max = INT32_MAX;
        value.step = 1;
    } else {
        duk_require_object_coercible(ctx, 1);

        duk_get_prop_string(ctx, -1, "val");
        duk_get_prop_string(ctx, -2, "min");
        duk_get_prop_string(ctx, -3, "max");
        duk_get_prop_string(ctx, -4, "step");

        value.val = duk_require_int(ctx, -4);
        value.min = duk_require_int(ctx, -3);
        value.max = duk_require_int(ctx, -2);
        value.step = duk_require_int(ctx, -1);

        duk_pop_n(ctx, 4); /* step, max, min, val values */
    }

    r = sol_flow_send_irange_packet(node, port, &value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send int packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return r;
}

static duk_ret_t
send_rgb_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    struct sol_rgb value;
    int r;

    duk_require_object_coercible(ctx, 1);

    duk_get_prop_string(ctx, -1, "red");
    duk_get_prop_string(ctx, -2, "green");
    duk_get_prop_string(ctx, -3, "blue");
    duk_get_prop_string(ctx, -4, "red_max");
    duk_get_prop_string(ctx, -5, "green_max");
    duk_get_prop_string(ctx, -6, "blue_max");

    value.red = duk_require_int(ctx, -6);
    value.green = duk_require_int(ctx, -5);
    value.blue = duk_require_int(ctx, -4);
    value.red_max = duk_require_int(ctx, -3);
    value.green_max = duk_require_int(ctx, -2);
    value.blue_max = duk_require_int(ctx, -1);

    duk_pop_n(ctx, 6); /* blue_max, green_max, red_max, blue, green, red values */

    r = sol_flow_send_rgb_packet(node, port, &value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send rgb packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return r;
}

static duk_ret_t
send_string_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    const char *value;
    int r;

    value = duk_require_string(ctx, 1);

    r = sol_flow_send_string_packet(node, port, value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send string packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return r;
}


static duk_ret_t
send_timestamp_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx)
{
    struct timespec timestamp;
    int r;

    duk_require_object_coercible(ctx, 1);

    duk_get_prop_string(ctx, -1, "tv_sec");
    duk_get_prop_string(ctx, -2, "tv_nsec");

    timestamp.tv_sec = duk_require_number(ctx, -2);
    timestamp.tv_nsec = duk_require_number(ctx, -1);

    duk_pop_n(ctx, 2);

    r = sol_flow_send_timestamp_packet(node, port, &timestamp);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR,
            "Couldn't send string packet on '%s' port.",
            get_out_port_name(node, port));
    }
    return r;
}

static duk_ret_t
send_direction_vector_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx)
{
    struct sol_direction_vector dir;
    int r;

    duk_require_object_coercible(ctx, 1);

    duk_get_prop_string(ctx, -1, "x");
    duk_get_prop_string(ctx, -2, "y");
    duk_get_prop_string(ctx, -3, "z");
    duk_get_prop_string(ctx, -4, "min");
    duk_get_prop_string(ctx, -5, "max");

    dir.x = duk_require_number(ctx, -5);
    dir.y = duk_require_number(ctx, -4);
    dir.z = duk_require_number(ctx, -3);
    dir.min = duk_require_number(ctx, -2);
    dir.max = duk_require_number(ctx, -1);

    duk_pop_n(ctx, 5);

    r = sol_flow_send_direction_vector_packet(node, port, &dir);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR,
            "Couldn't send string packet on '%s' port.",
            get_out_port_name(node, port));
    }
    return r;
}

static duk_ret_t
send_location_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx)
{
    struct sol_location loc;
    int r;

    duk_require_object_coercible(ctx, 1);

    duk_get_prop_string(ctx, -1, "lat");
    duk_get_prop_string(ctx, -2, "lon");
    duk_get_prop_string(ctx, -3, "alt");

    loc.lat = duk_require_number(ctx, -3);
    loc.lon = duk_require_number(ctx, -2);
    loc.alt = duk_require_number(ctx, -1);

    duk_pop_n(ctx, 3);

    r = sol_flow_send_location_packet(node, port, &loc);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR,
            "Couldn't send string packet on '%s' port.",
            get_out_port_name(node, port));
    }
    return r;
}

static int
send_blob(struct sol_flow_node *node, uint16_t port,
    const void *mem, size_t size, const struct sol_flow_packet_type *type)
{
    struct sol_blob *blob;
    int r;

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, mem, size);
    SOL_NULL_CHECK(blob, -ENOMEM);

    if (type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)
        r = sol_flow_send_json_array_packet(node, port, blob);
    else if (type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)
        r = sol_flow_send_json_object_packet(node, port, blob);
    else
        r = sol_flow_send_blob_packet(node, port, blob);
    sol_blob_unref(blob);
    return r;
}

static duk_ret_t
send_blob_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    void *mem, *cpy;
    size_t size;
    int r;

    mem = duk_require_buffer(ctx, 1, &size);
    cpy = malloc(size);
    SOL_NULL_CHECK(cpy, -ENOMEM);
    memcpy(cpy, mem, size);
    r = send_blob(node, port, cpy, size, SOL_FLOW_PACKET_TYPE_BLOB);
    if (r < 0)
        free(cpy);
    return r;
}

static int
js_array_to_sol_key_value_vector(duk_context *ctx, struct sol_vector *vector,
    const char *prop_name)
{
    int length, i;
    struct sol_key_value *key_value;

    duk_get_prop_string(ctx, -1, prop_name);

    duk_require_object_coercible(ctx, -1);

    duk_get_prop_string(ctx, -1, "length");
    length = duk_require_int(ctx, -1);
    duk_pop(ctx);

    for (i = 0; i < length; i++) {
        duk_get_prop_index(ctx, -1, i);

        duk_require_object_coercible(ctx, -1);

        duk_get_prop_string(ctx, -1, "key");
        duk_get_prop_string(ctx, -2, "value");

        key_value = sol_vector_append(vector);
        SOL_NULL_CHECK(key_value, -ENOMEM);
        key_value->key = duk_require_string(ctx, -2);
        key_value->value = duk_require_string(ctx, -1);

        duk_pop_n(ctx, 3);
    }

    duk_pop(ctx);
    return 0;
}

static duk_ret_t
send_http_response_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx)
{
    int r;
    int code;
    struct sol_blob *content;
    const char *url, *content_type;
    struct sol_vector cookies, headers;
    void *mem, *cpy;
    size_t size;

    sol_vector_init(&cookies, sizeof(struct sol_key_value));
    sol_vector_init(&headers, sizeof(struct sol_key_value));

    duk_require_object_coercible(ctx, 1);

    duk_get_prop_string(ctx, -1, "response_code");
    duk_get_prop_string(ctx, -2, "url");
    duk_get_prop_string(ctx, -3, "content-type");
    duk_get_prop_string(ctx, -4, "content");

    code = duk_require_int(ctx, -4);
    url = duk_require_string(ctx, -3);
    content_type = duk_require_string(ctx, -2);
    mem = duk_require_buffer(ctx, -1, &size);

    duk_pop_n(ctx, 4);

    js_array_to_sol_key_value_vector(ctx, &cookies, "cookies");
    js_array_to_sol_key_value_vector(ctx, &headers, "headers");

    cpy = malloc(size);
    SOL_NULL_CHECK(cpy, -ENOMEM);
    memcpy(cpy, mem, size);
    content = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, cpy, size);
    SOL_NULL_CHECK_GOTO(content, err_exit);

    r = sol_flow_send_http_response_packet(node, port, code, url,
        content_type, content, &cookies, &headers);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR,
            "Couldn't send string packet on '%s' port.",
            get_out_port_name(node, port));
    }
    sol_blob_unref(content);
    return r;

err_exit:
    sol_vector_clear(&cookies);
    sol_vector_clear(&headers);
    free(cpy);
    return -ENOMEM;
}

static duk_ret_t
send_json_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx, const struct sol_flow_packet_type *type)
{
    const char *value;
    char *cpy;
    int r;

    value = duk_require_string(ctx, 1);
    cpy = strdup(value);
    SOL_NULL_CHECK(cpy, -ENOMEM);
    r = send_blob(node, port, cpy, strlen(value), type);
    if (r < 0)
        free(cpy);
    return r;
}

static struct sol_flow_node *
get_node_from_duk_ctx(duk_context *ctx)
{
    struct sol_flow_node *n;

    duk_push_global_object(ctx);

    duk_get_prop_string(ctx, -1, "\xFF" "Soletta_node_pointer");
    n = duk_require_pointer(ctx, -1);

    duk_pop_2(ctx); /* Soletta_node_pointer, global object values */

    return n;
}

static int
get_output_port_number(const struct flow_js_type *type, const char *port_name)
{
    struct flow_js_port_out *p;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&type->ports_out, p, i) {
        if (streq(p->name, port_name))
            return i;
    }

    return -EINVAL;
}

/* sendPacket() on Javascript may throw exceptions. */
static duk_ret_t
send_packet(duk_context *ctx)
{
    const struct flow_js_port_out *port;
    const struct flow_js_type *type;
    const char *port_name;
    struct sol_flow_node *node;
    int port_number;

    port_name = duk_require_string(ctx, 0);

    node = get_node_from_duk_ctx(ctx);
    if (!node) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send packet to '%s' port.", port_name);
        return 0;
    }

    type = (struct flow_js_type *)sol_flow_node_get_type(node);
    if (!type) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send packet to '%s' port.", port_name);
        return 0;
    }

    /* TODO: Check a cheaper way to do this, if we had hashes we could
     * use them here. */

    port_number = get_output_port_number(type, port_name);
    if (port_number < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "'%s' invalid port name.", port_name);
        return 0;
    }

    port = sol_vector_get(&type->ports_out, port_number);
    if (!port) {
        duk_error(ctx, DUK_ERR_ERROR, "'%s' invalid port name.", port_name);
        return 0;
    }

    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)
        return send_boolean_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_BYTE)
        return send_byte_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)
        return send_float_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)
        return send_int_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_RGB)
        return send_rgb_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_STRING)
        return send_string_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_BLOB)
        return send_blob_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_LOCATION)
        return send_location_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP)
        return send_timestamp_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR)
        return send_direction_vector_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT ||
        port->type.packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)
        return send_json_packet(node, port_number, ctx,
            port->type.packet_type);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE)
        return send_http_response_packet(node, port_number, ctx);

    /* TODO: Create a way to let the user define custom packets. Maybe we could
     * use the same techniques we do for option parsing, and provide an object
     * with an array of fields, offsets and values in basic C types. */

    duk_error(ctx, DUK_ERR_ERROR, "Couldn't handle unknown port type %s.", port->type_name);

    return 0;
}

/* sendErrorPacket() on Javascript may throw exceptions. */
static duk_ret_t
send_error_packet(duk_context *ctx)
{
    const char *value_msg = NULL;
    struct sol_flow_node *node;
    int value_code, r;

    value_code = duk_require_int(ctx, 0);

    if (duk_is_string(ctx, 1))
        value_msg = duk_require_string(ctx, 1);

    node = get_node_from_duk_ctx(ctx);
    if (!node) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send error packet.");
        return 0;
    }

    r = sol_flow_send_error_packet_str(node, value_code, value_msg);
    if (r < 0)
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send error packet.");

    return r;
}

static bool
setup_ports_in_methods(struct duk_context *duk_ctx, uint16_t ports_in_len, uint16_t base)
{
    uint16_t i;

    if (ports_in_len == 0)
        return true;

    duk_get_prop_string(duk_ctx, -1, "in");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'in' property of object 'node' should be an array.");
        return false;
    }

    duk_push_global_stash(duk_ctx);

    for (i = 0; i < ports_in_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -2, i)) {
            SOL_ERR("Couldn't get input port information from 'ports.in[%d]'.", i);
            return false;
        }

        /* This is in order to get port methods references in one call.
         *
         * We have 3 methods for each input port. We put all in the stash,
         * even with 'undefined' values, if the method is not implemented on JS.
         *
         * We calculate the index by the following:
         *
         * base + input_port_index * ports_in_methods_length + method_index
         *
         * base - where should it start, for input ports it should be 0.
         * input_port_index - the index of the JS 'in' array entry.
         * method_index - the index of the method for input ports.
         */

        duk_get_prop_string(duk_ctx, -1, "connect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_IN_METHODS_LENGTH + PORTS_IN_CONNECT_INDEX);

        duk_get_prop_string(duk_ctx, -1, "disconnect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_IN_METHODS_LENGTH + PORTS_IN_DISCONNECT_INDEX);

        duk_get_prop_string(duk_ctx, -1, "process");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_IN_METHODS_LENGTH + PORTS_IN_PROCESS_INDEX);

        duk_pop(duk_ctx); /* array entry */
    }

    duk_pop_2(duk_ctx); /* in array and global_stash value */

    return true;
}

static bool
setup_ports_out_methods(struct duk_context *duk_ctx, uint16_t ports_out_len, uint16_t base)
{
    uint16_t i;

    if (ports_out_len == 0)
        return true;

    duk_get_prop_string(duk_ctx, -1, "out");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'out' property of object 'node' should be an array.");
        return false;
    }

    duk_push_global_stash(duk_ctx);

    for (i = 0; i < ports_out_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -2, i)) {
            SOL_ERR("Couldn't get output port information from 'ports.out[%d]'.", i);
            return false;
        }

        /* This is in order to get port methods references in one call.
         *
         * We have 2 methods for each output port. We put all in the stash,
         * even with 'undefined' values, if the method is not implemented on JS.
         *
         * We calculate the index by the following:
         *
         * base + output_port_index * ports_out_methods_length + method_index
         *
         * base - where should it start, for output ports it should be the size of input ports * number of input methods.
         * input_port_index - the index of the JS 'in' array entry.
         * method_index - the index of the method for output ports.
         */

        duk_get_prop_string(duk_ctx, -1, "connect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_OUT_METHODS_LENGTH + PORTS_OUT_CONNECT_INDEX);

        duk_get_prop_string(duk_ctx, -1, "disconnect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_OUT_METHODS_LENGTH + PORTS_OUT_DISCONNECT_INDEX);

        duk_pop(duk_ctx); /* array entry */
    }

    duk_pop_2(duk_ctx); /* out array and global_stash value */

    return true;
}

static bool
setup_ports_methods(duk_context *duk_ctx, uint16_t ports_in_len, uint16_t ports_out_len)
{
    /* We're using duktape global stash to keep reference to some JS
     * port methods: connect(), disconnect() and process() in order
     * to call it directly when receive a port number.
     */

    if (!setup_ports_in_methods(duk_ctx, ports_in_len, 0))
        return false;

    if (!setup_ports_out_methods(duk_ctx, ports_out_len, ports_in_len * PORTS_IN_METHODS_LENGTH))
        return false;

    return true;
}

/* open() method on JS may throw exceptions. */
static int
flow_js_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct flow_js_type *type = (struct flow_js_type *)sol_flow_node_get_type(node);
    struct flow_js_data *mdata = data;

    mdata->duk_ctx = duk_create_heap_default();
    if (!mdata->duk_ctx) {
        SOL_ERR("Failed to create a Duktape heap");
        return -1;
    }

    /* TODO: Check if there's a "already parsed" representation that we can use. */
    if (duk_peval_lstring(mdata->duk_ctx, type->js_content_buf, type->js_content_buf_len) != 0) {
        SOL_ERR("Failed to read from javascript content buffer: %s", duk_safe_to_string(mdata->duk_ctx, -1));
        duk_destroy_heap(mdata->duk_ctx);
        return -1;
    }
    duk_pop(mdata->duk_ctx); /* duk_peval_lstring() result */

    duk_push_global_object(mdata->duk_ctx);

    /* "Soletta_node_pointer" is a hidden property. \xFF is used to give one extra level of hiding */
    duk_push_string(mdata->duk_ctx, "\xFF" "Soletta_node_pointer");
    duk_push_pointer(mdata->duk_ctx, node);
    duk_def_prop(mdata->duk_ctx, -3,
        DUK_DEFPROP_HAVE_VALUE |
        DUK_DEFPROP_HAVE_WRITABLE |
        DUK_DEFPROP_HAVE_ENUMERABLE |
        DUK_DEFPROP_HAVE_CONFIGURABLE);

    duk_push_c_function(mdata->duk_ctx, send_packet, 2);
    duk_put_prop_string(mdata->duk_ctx, -2, "sendPacket");

    duk_push_c_function(mdata->duk_ctx, send_error_packet, 2);
    duk_put_prop_string(mdata->duk_ctx, -2, "sendErrorPacket");

    /* From this point node JS object is always in the top of the stack. */
    duk_get_prop_string(mdata->duk_ctx, -1, "node");

    if (!setup_ports_methods(mdata->duk_ctx, type->ports_in.len, type->ports_out.len)) {
        SOL_ERR("Failed to handle ports methods: %s", duk_safe_to_string(mdata->duk_ctx, -1));
        duk_destroy_heap(mdata->duk_ctx);
        return -1;
    }

    if (!duk_has_prop_string(mdata->duk_ctx, -1, "open"))
        return 0;

    duk_push_string(mdata->duk_ctx, "open");
    if (duk_pcall_prop(mdata->duk_ctx, -2, 0) != DUK_EXEC_SUCCESS) {
        duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Javascript open() function error: %s\n",
            duk_safe_to_string(mdata->duk_ctx, -1));
    }

    duk_pop(mdata->duk_ctx); /* open() result */

    return 0;
}

/* close() method on JS may throw exceptions. */
static void
flow_js_close(struct sol_flow_node *node, void *data)
{
    struct flow_js_data *mdata = (struct flow_js_data *)data;

    if (duk_has_prop_string(mdata->duk_ctx, -1, "close")) {
        duk_push_string(mdata->duk_ctx, "close");

        if (duk_pcall_prop(mdata->duk_ctx, -2, 0) != DUK_EXEC_SUCCESS) {
            duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Javascript close() function error: %s\n",
                duk_safe_to_string(mdata->duk_ctx, -1));
        }

        duk_pop(mdata->duk_ctx); /* close() result */
    }

    duk_destroy_heap(mdata->duk_ctx);
}

static int
process_boilerplate_pre(duk_context *ctx, struct sol_flow_node *node, uint16_t port)
{
    duk_push_global_stash(ctx);

    if (!duk_get_prop_index(ctx, -1, port * PORTS_IN_METHODS_LENGTH + PORTS_IN_PROCESS_INDEX)) {
        SOL_ERR("Couldn't handle '%s' process().", get_in_port_name(node, port));
        duk_pop_2(ctx); /* get_prop() value and global_stash */
        return -1;
    }

    if (duk_is_null_or_undefined(ctx, -1)) {
        SOL_WRN("'%s' process() callback not implemented in javascript, ignoring incoming packets for this port",
            get_in_port_name(node, port));
        duk_pop_2(ctx); /* get_prop() value and global_stash */
        return 0;
    }

    /* In order to use 'node' object as 'this' binding. */
    duk_dup(ctx, -3);

    return 1;
}

static int
process_boilerplate_post(duk_context *ctx, struct sol_flow_node *node, uint16_t port, uint16_t js_method_nargs)
{
    if (duk_pcall_method(ctx, js_method_nargs) != DUK_EXEC_SUCCESS) {
        duk_error(ctx, DUK_ERR_ERROR, "Javascript %s process() function error: %s\n",
            get_in_port_name(node, port), duk_safe_to_string(ctx, -1));
        duk_pop_2(ctx); /* process() result and global_stash */
        return -1;
    }

    duk_pop_2(ctx); /* process() result and global_stash */

    return 0;
}

static int
boolean_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_boolean(mdata->duk_ctx, value);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
byte_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    unsigned char value;
    int r;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_int(mdata->duk_ctx, value);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
error_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    const char *value_msg;
    int r, value_code;

    r = sol_flow_packet_get_error(packet, &value_code, &value_msg);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_int(mdata->duk_ctx, value_code);
    duk_push_string(mdata->duk_ctx, value_msg);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 2);
}

static int
float_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    struct sol_drange value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);
    duk_push_number(mdata->duk_ctx, value.val);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "val");
    duk_push_number(mdata->duk_ctx, value.min);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "min");
    duk_push_number(mdata->duk_ctx, value.max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "max");
    duk_push_number(mdata->duk_ctx, value.step);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "step");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
int_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    struct sol_irange value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);
    duk_push_int(mdata->duk_ctx, value.val);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "val");
    duk_push_int(mdata->duk_ctx, value.min);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "min");
    duk_push_int(mdata->duk_ctx, value.max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "max");
    duk_push_int(mdata->duk_ctx, value.step);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "step");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
rgb_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    struct sol_rgb value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_rgb(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);
    duk_push_int(mdata->duk_ctx, value.red);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "red");
    duk_push_int(mdata->duk_ctx, value.green);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "green");
    duk_push_int(mdata->duk_ctx, value.blue);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "blue");
    duk_push_int(mdata->duk_ctx, value.red_max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "red_max");
    duk_push_int(mdata->duk_ctx, value.green_max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "green_max");
    duk_push_int(mdata->duk_ctx, value.blue_max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "blue_max");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
string_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    const char *value;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_string(mdata->duk_ctx, value);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
timestamp_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = data;
    struct timespec timestamp;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_timestamp(packet, &timestamp);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);

    duk_push_number(mdata->duk_ctx, timestamp.tv_sec);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "tv_sec");

    duk_push_number(mdata->duk_ctx, timestamp.tv_nsec);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "tv_nsec");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
direction_vector_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = data;
    struct sol_direction_vector dir;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &dir);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);

    duk_push_number(mdata->duk_ctx, dir.x);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "x");

    duk_push_number(mdata->duk_ctx, dir.y);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "y");

    duk_push_number(mdata->duk_ctx, dir.z);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "z");

    duk_push_number(mdata->duk_ctx, dir.min);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "min");

    duk_push_number(mdata->duk_ctx, dir.max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "max");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static void
push_blob(const struct sol_blob *blob, struct duk_context *duk_ctx)
{
    void *mem;

    mem = duk_push_fixed_buffer(duk_ctx, blob->size);
    memcpy(mem, blob->mem, blob->size);
}

static int
blob_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct flow_js_data *mdata = data;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    /* FIXME: Should we add the other fields, like parent, ref count and size? */
    /* FIXME: If we bump the version use duk_push_external_buffer() */
    push_blob(blob, mdata->duk_ctx);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
location_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = data;
    struct sol_location loc;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_location(packet, &loc);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);

    duk_push_number(mdata->duk_ctx, loc.lat);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "lat");

    duk_push_number(mdata->duk_ctx, loc.lon);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "lon");

    duk_push_number(mdata->duk_ctx, loc.alt);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "alt");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
json_array_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = data;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_json_array(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_lstring(mdata->duk_ctx, (const char *)blob->mem, blob->size);
    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
json_object_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = data;
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_json_object(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_lstring(mdata->duk_ctx, (const char *)blob->mem, blob->size);
    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static void
add_sol_key_valueto_js_array(const struct sol_vector *vector,
    struct duk_context *duk_ctx, duk_idx_t request_idx, const char *prop_name)
{
    uint16_t i;
    duk_idx_t obj_idx, array_idx;
    struct sol_key_value *key_value;

    array_idx = duk_push_array(duk_ctx);

    SOL_VECTOR_FOREACH_IDX (vector, key_value, i) {
        obj_idx = duk_push_object(duk_ctx);
        duk_push_string(duk_ctx, key_value->key);
        duk_put_prop_string(duk_ctx, obj_idx, "key");
        duk_push_string(duk_ctx, key_value->value);
        duk_put_prop_string(duk_ctx, obj_idx, "value");
        duk_put_prop_index(duk_ctx, array_idx, i);
    }

    duk_put_prop_string(duk_ctx, request_idx, prop_name);
}

static int
http_response_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = data;
    const char *url, *content_type;
    const struct sol_blob *content;
    struct sol_vector cookies, headers;
    duk_idx_t obj_idx;
    int r, code;

    sol_vector_init(&cookies, sizeof(struct sol_key_value));
    sol_vector_init(&headers, sizeof(struct sol_key_value));
    r = sol_flow_packet_get_http_response(packet, &code, &url, &content_type,
        &content, &cookies, &headers);
    SOL_INT_CHECK(r, < 0, r);
    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);

    duk_push_number(mdata->duk_ctx, code);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "response_code");

    duk_push_string(mdata->duk_ctx, url);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "url");

    duk_push_string(mdata->duk_ctx, content_type);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "content-type");

    push_blob(content, mdata->duk_ctx);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "content");

    add_sol_key_valueto_js_array(&cookies, mdata->duk_ctx, obj_idx, "cookies");
    add_sol_key_valueto_js_array(&headers, mdata->duk_ctx, obj_idx, "headers");

    r = process_boilerplate_post(mdata->duk_ctx, node, port, 1);

    return r;
}

/* process() methods on JS may throw exceptions. */
static int
flow_js_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    const struct sol_flow_packet_type *packet_type;

    packet_type = sol_flow_packet_get_type(packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)
        return boolean_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE)
        return byte_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_ERROR)
        return error_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)
        return float_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)
        return int_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_RGB)
        return rgb_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_STRING)
        return string_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BLOB)
        return blob_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_LOCATION)
        return location_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP)
        return timestamp_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR)
        return direction_vector_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)
        return json_object_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)
        return json_array_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE)
        return http_response_process(node, data, port, conn_id, packet);

    return -EINVAL;
}

/* connect() and disconnect() port methods on JS may throw exceptions. */
static int
handle_js_port_activity(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    uint16_t base, uint16_t methods_length, uint16_t method_index)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;

    duk_push_global_stash(mdata->duk_ctx);

    if (!duk_get_prop_index(mdata->duk_ctx, -1, base + port * methods_length + method_index)) {
        duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Couldn't handle '%s' %s().",
            get_in_port_name(node, port),
            method_index == PORTS_IN_CONNECT_INDEX ? "connect" : "disconnect");
        duk_pop_2(mdata->duk_ctx); /* get_prop() value and global_stash */
        return -1;
    }

    if (duk_is_null_or_undefined(mdata->duk_ctx, -1)) {
        duk_pop_2(mdata->duk_ctx); /* get_prop() value and global_stash */
        return 0;
    }

    if (duk_pcall(mdata->duk_ctx, 0) != DUK_EXEC_SUCCESS) {
        duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Javascript function error: %s\n",
            duk_safe_to_string(mdata->duk_ctx, -1));
        duk_pop_2(mdata->duk_ctx); /* method() result and global_stash */
        return -1;
    }

    duk_pop_2(mdata->duk_ctx); /* method() result and global_stash */

    return 0;
}

static int
flow_js_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    return handle_js_port_activity(node, data, port, conn_id, 0, PORTS_IN_METHODS_LENGTH, PORTS_IN_CONNECT_INDEX);
}

static int
flow_js_port_in_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    return handle_js_port_activity(node, data, port, conn_id, 0, PORTS_IN_METHODS_LENGTH, PORTS_IN_DISCONNECT_INDEX);
}

static int
flow_js_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct flow_js_type *type = (const struct flow_js_type *)sol_flow_node_get_type(node);

    return handle_js_port_activity(node, data, port, conn_id,
        type->ports_in.len * PORTS_IN_METHODS_LENGTH, PORTS_OUT_METHODS_LENGTH, PORTS_OUT_CONNECT_INDEX);
}

static int
flow_js_port_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct flow_js_type *type = (const struct flow_js_type *)sol_flow_node_get_type(node);

    return handle_js_port_activity(node, data, port, conn_id,
        type->ports_in.len * PORTS_IN_METHODS_LENGTH, PORTS_OUT_METHODS_LENGTH, PORTS_OUT_DISCONNECT_INDEX);
}

static const struct sol_flow_port_type_in *
flow_js_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct flow_js_type *js_type = (struct flow_js_type *)type;
    const struct flow_js_port_in *p = sol_vector_get(&js_type->ports_in, port);

    return p ? &p->type : NULL;
}

static const struct sol_flow_port_type_out *
flow_js_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct flow_js_type *js_type = (struct flow_js_type *)type;
    const struct flow_js_port_out *p = sol_vector_get(&js_type->ports_out, port);

    return p ? &p->type : NULL;
}

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static const struct sol_flow_node_type_description sol_flow_node_type_js_description = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION, )
    .name = "js",
    .category = "js",
    .symbol = "SOL_FLOW_NODE_TYPE_JS",
    .options_symbol = "sol_flow_node_type_js_options",
    .version = NULL,
    /* TODO: Add a way for the user specify description, author, url and license. */
};

static int
setup_description(struct flow_js_type *type)
{
    struct sol_flow_node_type_description *desc;
    struct sol_flow_port_description **p;
    struct flow_js_port_in *port_type_in;
    struct flow_js_port_out *port_type_out;
    int i, j;

    type->base.description = calloc(1, sizeof(struct sol_flow_node_type_description));
    SOL_NULL_CHECK(type->base.description, -ENOMEM);

    type->base.description = memcpy((struct sol_flow_node_type_description *)type->base.description,
        &sol_flow_node_type_js_description, sizeof(struct sol_flow_node_type_description));

    desc = (struct sol_flow_node_type_description *)type->base.description;

    /* Extra slot for NULL sentinel at the end. */
    desc->ports_in = calloc(type->ports_in.len + 1, sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_in, fail_ports_in);

    p = (struct sol_flow_port_description **)desc->ports_in;
    for (i = 0; i < type->ports_in.len; i++) {
        p[i] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK_GOTO(p[i], fail_ports_in_desc);

        port_type_in = sol_vector_get(&type->ports_in, i);

        p[i]->name = port_type_in->name;
        p[i]->description = "Input port";
        p[i]->data_type = port_type_in->type_name;
        p[i]->array_size = 0;
        p[i]->base_port_idx = i;
        p[i]->required = false;
    }

    /* Extra slot for NULL sentinel at the end. */
    desc->ports_out = calloc(type->ports_out.len + 1, sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_out, fail_ports_in_desc);

    p = (struct sol_flow_port_description **)desc->ports_out;
    for (j = 0; j < type->ports_out.len; j++) {
        p[j] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK_GOTO(p[j], fail_ports_out_desc);

        port_type_out = sol_vector_get(&type->ports_out, j);

        p[j]->name = port_type_out->name;
        p[j]->description = "Output port";
        p[j]->data_type = port_type_out->type_name;
        p[j]->array_size = 0;
        p[j]->base_port_idx = j;
        p[j]->required = false;
    }

    return 0;

fail_ports_out_desc:
    if (j > 0) {
        for (; j >= 0; j--) {
            free((struct sol_flow_port_description *)desc->ports_out[j]);
        }
    }
    free((struct sol_flow_port_description **)desc->ports_out);
fail_ports_in_desc:
    if (i > 0) {
        for (; i >= 0; i--) {
            free((struct sol_flow_port_description *)desc->ports_in[i]);
        }
    }
    free((struct sol_flow_port_description **)desc->ports_in);
fail_ports_in:
    free(desc);
    return -ENOMEM;
}

static void
free_description(struct flow_js_type *type)
{
    struct sol_flow_node_type_description *desc;
    uint16_t i;

    desc = (struct sol_flow_node_type_description *)type->base.description;

    for (i = 0; i < type->ports_in.len; i++)
        free((struct sol_flow_port_description *)desc->ports_in[i]);
    free((struct sol_flow_port_description **)desc->ports_in);

    for (i = 0; i < type->ports_out.len; i++)
        free((struct sol_flow_port_description *)desc->ports_out[i]);
    free((struct sol_flow_port_description **)desc->ports_out);

    free(desc);
}
#endif

static const struct sol_flow_packet_type *
get_packet_type(const char *type)
{
    /* We're using 'if statements' instead of 'sol_str_table_ptr' because we couldn't create the table
     * as 'const static' since the packet types are declared in another file (found only in linkage time),
     * and creating the table all the time would give us a bigger overhead than 'if statements' */

    if (!strcasecmp(type, "boolean"))
        return SOL_FLOW_PACKET_TYPE_BOOLEAN;
    if (!strcasecmp(type, "byte"))
        return SOL_FLOW_PACKET_TYPE_BYTE;
    if (!strcasecmp(type, "drange") || !strcasecmp(type, "float"))
        return SOL_FLOW_PACKET_TYPE_DRANGE;
    if (!strcasecmp(type, "error"))
        return SOL_FLOW_PACKET_TYPE_ERROR;
    if (!strcasecmp(type, "irange") || !strcasecmp(type, "int"))
        return SOL_FLOW_PACKET_TYPE_IRANGE;
    if (!strcasecmp(type, "rgb"))
        return SOL_FLOW_PACKET_TYPE_RGB;
    if (!strcasecmp(type, "string"))
        return SOL_FLOW_PACKET_TYPE_STRING;
    if (!strcasecmp(type, "blob"))
        return SOL_FLOW_PACKET_TYPE_BLOB;
    if (!strcasecmp(type, "location"))
        return SOL_FLOW_PACKET_TYPE_LOCATION;
    if (!strcasecmp(type, "timestamp"))
        return SOL_FLOW_PACKET_TYPE_TIMESTAMP;
    if (!strcasecmp(type, "direction-vector"))
        return SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR;
    if (!strcasecmp(type, "json-object"))
        return SOL_FLOW_PACKET_TYPE_JSON_OBJECT;
    if (!strcasecmp(type, "json-array"))
        return SOL_FLOW_PACKET_TYPE_JSON_ARRAY;
    if (!strcasecmp(type, "http-response"))
        return SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE;

    return NULL;
}

static bool
setup_ports_in(struct duk_context *duk_ctx, struct sol_arena *str_arena, struct sol_vector *ports_in)
{
    const char *name, *type_name;
    const struct sol_flow_packet_type *packet_type;
    struct flow_js_port_in *port_type;
    uint16_t array_len, i;

    if (!duk_has_prop_string(duk_ctx, -1, "in"))
        return true;

    duk_get_prop_string(duk_ctx, -1, "in");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'in' property of variable 'ports' should be an array.");
        return false;
    }

    if (!duk_get_prop_string(duk_ctx, -1, "length")) {
        SOL_ERR("Couldn't get 'in' length from 'ports' variable.");
        return false;
    }

    array_len = duk_require_int(duk_ctx, -1);
    duk_pop(duk_ctx); /* length value */

    if (array_len == 0)
        return true;

    sol_vector_init(ports_in, sizeof(struct flow_js_port_in));

    for (i = 0; i < array_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -1, i)) {
            SOL_WRN("Couldn't get input port information from 'ports.in[%d]', ignoring this port creation...", i);
            duk_pop(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -1, "name")) {
            SOL_WRN("Input port 'name' property is missing on 'ports.in[%d]', ignoring this port creation... "
                "e.g. '{ name:'IN', type:'boolean' }'", i);
            duk_pop_2(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -2, "type")) {
            SOL_WRN("Input port 'type' property is missing on 'ports.in[%d]', ignoring this port creation... "
                "e.g. '{ name:'IN', type:'boolean' }'", i);
            duk_pop_3(duk_ctx);
            continue;
        }

        name = duk_require_string(duk_ctx, -2);
        type_name = duk_require_string(duk_ctx, -1);

        packet_type = get_packet_type(type_name);
        if (!packet_type) {
            SOL_WRN("Input port type '%s' is an invalid packet type on 'ports.in[%d]', ignoring this port creation...", type_name, i);
            duk_pop_3(duk_ctx);
            continue;
        }

        port_type = sol_vector_append(ports_in);
        SOL_NULL_CHECK(port_type, false);

        SOL_SET_API_VERSION(port_type->type.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION; )
        port_type->type.packet_type = packet_type;
        port_type->type.process = flow_js_port_process;
        port_type->type.connect = flow_js_port_in_connect;
        port_type->type.disconnect = flow_js_port_in_disconnect;

        port_type->name = sol_arena_strdup(str_arena, name);
        SOL_NULL_CHECK(port_type->name, false);

        port_type->type_name = sol_arena_strdup(str_arena, type_name);
        SOL_NULL_CHECK(port_type->type_name, false);

        duk_pop_3(duk_ctx);
    }

    duk_pop(duk_ctx); /* in value */

    return true;
}

static bool
setup_ports_out(struct duk_context *duk_ctx, struct sol_arena *str_arena, struct sol_vector *ports_out)
{
    const char *name, *type_name;
    const struct sol_flow_packet_type *packet_type;
    struct flow_js_port_out *port_type;
    uint16_t array_len, i;

    if (!duk_has_prop_string(duk_ctx, -1, "out"))
        return true;

    duk_get_prop_string(duk_ctx, -1, "out");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'out' property of variable 'ports' should be an array.");
        return false;
    }

    if (!duk_get_prop_string(duk_ctx, -1, "length")) {
        SOL_ERR("Couldn't get 'out' length from 'ports' variable.");
        return false;
    }

    array_len = duk_require_int(duk_ctx, -1);
    duk_pop(duk_ctx); /* length value */

    if (array_len == 0)
        return true;

    sol_vector_init(ports_out, sizeof(struct flow_js_port_out));

    for (i = 0; i < array_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -1, i)) {
            SOL_WRN("Couldn't get output port information from 'ports.out[%d]', ignoring this port creation...", i);
            duk_pop(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -1, "name")) {
            SOL_WRN("Output port 'name' property is missing on 'ports.out[%d]', ignoring this port creation... "
                "e.g. '{ name:'OUT', type:'boolean' }'", i);
            duk_pop_2(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -2, "type")) {
            SOL_WRN("Output port 'type' property is missing on 'ports.out[%d]', ignoring this port creation... "
                "e.g. '{ name:'OUT', type:'boolean' }'", i);
            duk_pop_3(duk_ctx);
            continue;
        }

        name = duk_require_string(duk_ctx, -2);
        type_name = duk_require_string(duk_ctx, -1);

        packet_type = get_packet_type(type_name);
        if (!packet_type) {
            SOL_WRN("Output port type '%s' is an invalid packet type on 'ports.out[%d]', ignoring this port creation...", type_name, i);
            duk_pop_3(duk_ctx);
            continue;
        }

        port_type = sol_vector_append(ports_out);
        SOL_NULL_CHECK(port_type, false);

        SOL_SET_API_VERSION(port_type->type.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION; )
        port_type->type.packet_type = packet_type;
        port_type->type.connect = flow_js_port_out_connect;
        port_type->type.disconnect = flow_js_port_out_disconnect;

        port_type->name = sol_arena_strdup(str_arena, name);
        SOL_NULL_CHECK(port_type->name, false);

        port_type->type_name = sol_arena_strdup(str_arena, type_name);
        SOL_NULL_CHECK(port_type->type_name, false);

        duk_pop_3(duk_ctx);
    }

    duk_pop(duk_ctx); /* out value */

    return true;
}

static bool
setup_ports(struct flow_js_type *type, const char *buf, size_t len)
{
    struct duk_context *duk_ctx;

    duk_ctx = duk_create_heap_default();
    if (!duk_ctx) {
        SOL_ERR("Failed to create a Duktape heap");
        return false;
    }

    if (duk_peval_lstring(duk_ctx, buf, len) != 0) {
        SOL_ERR("Failed to parse javascript content: %s", duk_safe_to_string(duk_ctx, -1));
        duk_destroy_heap(duk_ctx);
        return false;
    }
    duk_pop(duk_ctx); /* duk_peval_lstring() result */

    duk_push_global_object(duk_ctx);

    if (!duk_get_prop_string(duk_ctx, -1, "node")) {
        SOL_ERR("'node' variable not found in javascript file.");
        duk_destroy_heap(duk_ctx);
        return false;
    }

    type->str_arena = sol_arena_new();
    if (!type->str_arena) {
        SOL_ERR("Couldn't create sol_arena.");
        duk_destroy_heap(duk_ctx);
        return false;
    }

    if (!setup_ports_in(duk_ctx, type->str_arena, &type->ports_in)) {
        duk_destroy_heap(duk_ctx);
        return false;
    }

    if (!setup_ports_out(duk_ctx, type->str_arena, &type->ports_out)) {
        duk_destroy_heap(duk_ctx);
        return false;
    }

    duk_destroy_heap(duk_ctx);
    return true;
}

static void
flow_js_type_fini(struct flow_js_type *type)
{
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (type->base.description)
        free_description(type);
#endif

    if (type->str_arena)
        sol_arena_del(type->str_arena);

    sol_vector_clear(&type->ports_in);
    sol_vector_clear(&type->ports_out);

    free(type->js_content_buf);
}

static void
flow_dispose_type(struct sol_flow_node_type *type)
{
    struct flow_js_type *js_type;

    SOL_NULL_CHECK(type);

    js_type = (struct flow_js_type *)type;
    flow_js_type_fini(js_type);
    free(js_type);
}

static bool
flow_js_type_init(struct flow_js_type *type, const char *buf, size_t len)
{
    char *js_content_buf;

    *type = (const struct flow_js_type) {
        .base = {
            SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )
            .data_size = sizeof(struct flow_js_data),
            .open = flow_js_open,
            .close = flow_js_close,
            .get_port_in = flow_js_get_port_in,
            .get_port_out = flow_js_get_port_out,
            .dispose_type = flow_dispose_type,
            .options_size = sizeof(struct sol_flow_node_options),
        },
    };

    if (!setup_ports(type, buf, len))
        return false;

    type->base.ports_in_count = type->ports_in.len;
    type->base.ports_out_count = type->ports_out.len;

    js_content_buf = strndup(buf, len);
    SOL_NULL_CHECK(js_content_buf, false);

    type->js_content_buf = js_content_buf;
    type->js_content_buf_len = len;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (setup_description(type) < 0)
        SOL_WRN("Failed to setup description");
#endif

    return true;
}

static struct sol_flow_node_type *
sol_flow_js_new_type(const char *buf, size_t len)
{
    struct flow_js_type *type;

    SOL_LOG_INTERNAL_INIT_ONCE;

    type = calloc(1, sizeof(struct flow_js_type));
    SOL_NULL_CHECK(type, NULL);

    if (!flow_js_type_init(type, buf, len)) {
        flow_js_type_fini(type);
        free(type);
        return NULL;
    }

    return &type->base;
}

static int
js_create_type(
    const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type)
{
    const char *buf, *filename;
    struct sol_flow_node_type *result;
    size_t size;
    int err;

    filename = strndupa(ctx->contents.data, ctx->contents.len);
    err = ctx->read_file(ctx, filename, &buf, &size);
    if (err < 0)
        return -EINVAL;

    result = sol_flow_js_new_type(buf, size);
    if (!result)
        return -EINVAL;

    err = ctx->store_type(ctx, result);
    if (err < 0) {
        sol_flow_node_type_del(result);
        return -err;
    }

    *type = result;
    return 0;
}

SOL_FLOW_METATYPE(JS,
    .name = "js",
    .create_type = js_create_type,
    .generate_type_start = NULL,
    .generate_type_body = NULL,
    .generate_type_end = NULL,
    .ports_description = NULL,
    );
