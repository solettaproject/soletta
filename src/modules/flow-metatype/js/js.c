/*
 * This file is part of the Soletta Project
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
#include <float.h>
#include <stdio.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-metatype-js");

#include "duktape.h"

#include "sol-arena.h"
#include "sol-flow-metatype.h"
#include "sol-log.h"
#include "sol-util-internal.h"
#include "js_code_start.h"

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
 *               and its callback functions that will be trigged on the occurrence
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
    duk_context *duk_ctx;
};

struct flow_js_port_description_context {
    struct sol_vector *in;
    struct sol_vector *out;
    struct sol_buffer *buf;
    struct sol_str_slice name_prefix;
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

typedef int (*js_add_port)(const char *name, const char *type_name, bool is_input, void *data);

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

static struct sol_flow_packet *
pop_boolean(duk_context *ctx)
{
    bool value;

    value = duk_require_boolean(ctx, -1);

    return sol_flow_packet_new_boolean(value);
}

static struct sol_flow_packet *
pop_byte(duk_context *ctx)
{
    unsigned char value;

    value = duk_require_int(ctx, -1);

    return sol_flow_packet_new_byte(value);
}

static struct sol_flow_packet *
pop_float(duk_context *ctx)
{
    struct sol_drange value;

    if (duk_is_number(ctx, 1)) {
        value.val = duk_require_number(ctx, -1);

        value.min = -DBL_MAX;
        value.max = DBL_MAX;
        value.step = DBL_MIN;
    } else {
        duk_require_object_coercible(ctx, -1);

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

    return sol_flow_packet_new_drange(&value);
}

static struct sol_flow_packet *
pop_int(duk_context *ctx)
{
    struct sol_irange value;

    if (duk_is_number(ctx, 1)) {
        value.val = duk_require_int(ctx, -1);

        value.min = INT32_MIN;
        value.max = INT32_MAX;
        value.step = 1;
    } else {
        duk_require_object_coercible(ctx, -1);

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

    return sol_flow_packet_new_irange(&value);
}

static struct sol_flow_packet *
pop_rgb(duk_context *ctx)
{
    struct sol_rgb value;

    duk_require_object_coercible(ctx, -1);

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

    return sol_flow_packet_new_rgb(&value);
}

static struct sol_flow_packet *
pop_string(duk_context *ctx)
{
    const char *value;

    value = duk_require_string(ctx, -1);

    return sol_flow_packet_new_string(value);
}

static struct sol_flow_packet *
pop_timestamp(duk_context *ctx)
{
    struct timespec timestamp;

    duk_require_object_coercible(ctx, -1);

    duk_get_prop_string(ctx, -1, "tv_sec");
    duk_get_prop_string(ctx, -2, "tv_nsec");

    timestamp.tv_sec = duk_require_number(ctx, -2);
    timestamp.tv_nsec = duk_require_number(ctx, -1);

    duk_pop_n(ctx, 2);

    return sol_flow_packet_new_timestamp(&timestamp);
}

static struct sol_flow_packet *
pop_direction_vector(duk_context *ctx)
{
    struct sol_direction_vector dir;

    duk_require_object_coercible(ctx, -1);

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

    return sol_flow_packet_new_direction_vector(&dir);
}

static struct sol_flow_packet *
pop_location(duk_context *ctx)
{
    struct sol_location loc;

    duk_require_object_coercible(ctx, -1);

    duk_get_prop_string(ctx, -1, "lat");
    duk_get_prop_string(ctx, -2, "lon");
    duk_get_prop_string(ctx, -3, "alt");

    loc.lat = duk_require_number(ctx, -3);
    loc.lon = duk_require_number(ctx, -2);
    loc.alt = duk_require_number(ctx, -1);

    duk_pop_n(ctx, 3);

    return sol_flow_packet_new_location(&loc);
}

static struct sol_flow_packet *
pop_blob(duk_context *ctx)
{
    void *mem, *cpy;
    size_t size;
    struct sol_blob *blob;
    struct sol_flow_packet *packet;

    mem = duk_require_buffer(ctx, -1, &size);
    cpy = malloc(size);
    SOL_NULL_CHECK(cpy, NULL);
    memcpy(cpy, mem, size);
    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, cpy, size);

    if (!blob) {
        free(cpy);
        return NULL;
    }

    packet = sol_flow_packet_new_blob(blob);

    sol_blob_unref(blob);
    return packet;
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

static struct sol_flow_packet *
pop_http_response(duk_context *ctx)
{
    int code;
    struct sol_blob *content;
    const char *url, *content_type;
    struct sol_vector cookies, headers;
    void *mem, *cpy;
    size_t size;
    struct sol_flow_packet *packet;

    sol_vector_init(&cookies, sizeof(struct sol_key_value));
    sol_vector_init(&headers, sizeof(struct sol_key_value));

    duk_require_object_coercible(ctx, -1);

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
    SOL_NULL_CHECK(cpy, NULL);
    memcpy(cpy, mem, size);
    content = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, cpy, size);
    SOL_NULL_CHECK_GOTO(content, err_exit);

    packet = sol_flow_packet_new_http_response(code, url,
        content_type, content, &cookies, &headers);

    sol_blob_unref(content);
    sol_vector_clear(&cookies);
    sol_vector_clear(&headers);
    return packet;

err_exit:
    sol_vector_clear(&cookies);
    sol_vector_clear(&headers);
    free(cpy);
    return NULL;
}

static struct sol_flow_packet *
pop_json(duk_context *ctx,
    const struct sol_flow_packet_type *packet_type)
{
    const char *value;
    struct sol_blob *blob;
    struct sol_flow_packet *packet;
    char *cpy;

    value = duk_require_string(ctx, -1);
    cpy = strdup(value);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, cpy, strlen(cpy));
    if (!blob) {
        free(cpy);
        return NULL;
    }
    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)
        packet = sol_flow_packet_new_json_object(blob);
    else
        packet = sol_flow_packet_new_json_array(blob);
    sol_blob_unref(blob);
    return packet;
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

static struct sol_flow_packet *
create_packet(const struct sol_flow_packet_type *packet_type, duk_context *ctx)
{
    if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)
        return pop_boolean(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE)
        return pop_byte(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)
        return pop_float(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)
        return pop_int(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_RGB)
        return pop_rgb(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_STRING)
        return pop_string(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BLOB)
        return pop_blob(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_LOCATION)
        return pop_location(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP)
        return pop_timestamp(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR)
        return pop_direction_vector(ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT ||
        packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)
        return pop_json(ctx, packet_type);
    if (packet_type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE)
        return pop_http_response(ctx);

    /* TODO: Create a way to let the user define custom packets. Maybe we could
     * use the same techniques we do for option parsing, and provide an object
     * with an array of fields, offsets and values in basic C types. */

    SOL_WRN("Couldn't handle unknown port type %s.", packet_type->name);
    return NULL;
}

static int
send_composed_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx, const struct sol_flow_packet_type *composed_type)
{
    int r;
    uint16_t i, len;
    const struct sol_flow_packet_type **composed_members;
    struct sol_flow_packet **packets;

    r = sol_flow_packet_get_composed_members_packet_types(composed_type,
        &composed_members, &len);
    SOL_INT_CHECK(r, < 0, r);

    packets = calloc(len, sizeof(struct sol_flow_packet *));
    SOL_NULL_CHECK(packets, -ENOMEM);

    duk_require_object_coercible(ctx, -1);
    r = -ENOMEM;
    for (i = 0; i < len; i++) {
        duk_get_prop_index(ctx, 1, i);
        packets[i] = create_packet(composed_members[i], ctx);
        SOL_NULL_CHECK_GOTO(packets[i], exit);
        duk_pop(ctx);
    }

    r = sol_flow_send_composed_packet(node, port, composed_type, packets);

    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR,
            "Couldn't send boolean packet on '%s' port.",
            get_out_port_name(node, port));
    }

exit:
    for (i = 0; i < len; i++) {
        if (!packets[i])
            break;
        sol_flow_packet_del(packets[i]);
    }
    free(packets);
    return r;
}

static int
send_simple_packet(struct sol_flow_node *node, uint16_t port,
    duk_context *ctx, const struct sol_flow_packet_type *type)
{
    struct sol_flow_packet *packet;
    int r;

    packet = create_packet(type, ctx);
    SOL_NULL_CHECK(packet, -ENOMEM);
    r = sol_flow_send_packet(node, port, packet);

    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR,
            "Couldn't send boolean packet on '%s' port.",
            get_out_port_name(node, port));
    }

    return 0;
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

    if (sol_flow_packet_is_composed_type(port->type.packet_type))
        return send_composed_packet(node, port_number, ctx,
            port->type.packet_type);
    return send_simple_packet(node, port_number, ctx,
        port->type.packet_type);
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
fetch_ports_methods(duk_context *duk_ctx, const char *prop,
    uint16_t ports_len, uint16_t base, uint16_t methods_len, uint16_t *methods_index)
{
    uint16_t i;

    if (ports_len == 0)
        return true;

    duk_get_prop_string(duk_ctx, -1, prop);

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'%s' property of object 'node' should be an array.", prop);
        return false;
    }

    duk_push_global_stash(duk_ctx);

    for (i = 0; i < ports_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -2, i)) {
            SOL_ERR("Couldn't get input port information from 'ports.%s[%d]'.", prop, i);
            return false;
        }

        /* This is in order to get port methods references in one call.
         *
         * We have 3 methods for each input port and 2 for output ports. We put all in the stash,
         * even with 'undefined' values, if the method is not implemented on JS.
         *
         * We calculate the index by the following:
         *
         * base + port_index * methods_length + method_index
         *
         * base - where should it start, for input ports it should be 0.
         * port_index - the index of the JS 'in'/'out' array entry.
         * method_index - the index of the method for input/output ports.
         */

        duk_get_prop_string(duk_ctx, -1, "connect");
        duk_put_prop_index(duk_ctx, -3, base + i * methods_len + methods_index[0]);

        duk_get_prop_string(duk_ctx, -1, "disconnect");
        duk_put_prop_index(duk_ctx, -3, base + i * methods_len + methods_index[1]);

        if (methods_len >= 3) {
            duk_get_prop_string(duk_ctx, -1, "process");
            duk_put_prop_index(duk_ctx, -3, base + i * methods_len + methods_index[2]);
        }

        duk_pop(duk_ctx); /* array entry */
    }

    duk_pop_2(duk_ctx); /* in array and global_stash value */

    return true;
}

static bool
setup_ports_methods(duk_context *duk_ctx, uint16_t ports_in_len, uint16_t ports_out_len)
{
    /* We're using duktape global stash to keep reference to some JS
     * port methods: connect(), disconnect() and process() in order
     * to call it directly when receive a port number.
     */

    uint16_t methods_in_index[] = { PORTS_IN_CONNECT_INDEX,
                                    PORTS_IN_DISCONNECT_INDEX, PORTS_IN_PROCESS_INDEX };
    uint16_t methods_out_index[] = { PORTS_OUT_CONNECT_INDEX, PORTS_OUT_DISCONNECT_INDEX };

    if (!fetch_ports_methods(duk_ctx, "in", ports_in_len, 0,
        PORTS_IN_METHODS_LENGTH, methods_in_index))
        return false;

    if (!fetch_ports_methods(duk_ctx, "out", ports_out_len,
        ports_in_len * PORTS_IN_METHODS_LENGTH,
        PORTS_OUT_METHODS_LENGTH, methods_out_index))
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
push_boolean(const struct sol_flow_packet *packet,
    duk_context *duk_ctx)
{
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    duk_push_boolean(duk_ctx, value);

    return 0;
}

static int
push_byte(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    unsigned char value;
    int r;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    duk_push_int(duk_ctx, value);

    return 0;
}

static int
push_error(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    const char *value_msg;
    int r, value_code;

    r = sol_flow_packet_get_error(packet, &value_code, &value_msg);
    SOL_INT_CHECK(r, < 0, r);

    duk_push_int(duk_ctx, value_code);
    duk_push_string(duk_ctx, value_msg);

    return 0;
}

static int
push_float(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_drange value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    obj_idx = duk_push_object(duk_ctx);
    duk_push_number(duk_ctx, value.val);
    duk_put_prop_string(duk_ctx, obj_idx, "val");
    duk_push_number(duk_ctx, value.min);
    duk_put_prop_string(duk_ctx, obj_idx, "min");
    duk_push_number(duk_ctx, value.max);
    duk_put_prop_string(duk_ctx, obj_idx, "max");
    duk_push_number(duk_ctx, value.step);
    duk_put_prop_string(duk_ctx, obj_idx, "step");

    return 0;
}

static int
push_int(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_irange value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    obj_idx = duk_push_object(duk_ctx);
    duk_push_int(duk_ctx, value.val);
    duk_put_prop_string(duk_ctx, obj_idx, "val");
    duk_push_int(duk_ctx, value.min);
    duk_put_prop_string(duk_ctx, obj_idx, "min");
    duk_push_int(duk_ctx, value.max);
    duk_put_prop_string(duk_ctx, obj_idx, "max");
    duk_push_int(duk_ctx, value.step);
    duk_put_prop_string(duk_ctx, obj_idx, "step");

    return 0;
}

static int
push_rgb(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_rgb value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_rgb(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    obj_idx = duk_push_object(duk_ctx);
    duk_push_int(duk_ctx, value.red);
    duk_put_prop_string(duk_ctx, obj_idx, "red");
    duk_push_int(duk_ctx, value.green);
    duk_put_prop_string(duk_ctx, obj_idx, "green");
    duk_push_int(duk_ctx, value.blue);
    duk_put_prop_string(duk_ctx, obj_idx, "blue");
    duk_push_int(duk_ctx, value.red_max);
    duk_put_prop_string(duk_ctx, obj_idx, "red_max");
    duk_push_int(duk_ctx, value.green_max);
    duk_put_prop_string(duk_ctx, obj_idx, "green_max");
    duk_push_int(duk_ctx, value.blue_max);
    duk_put_prop_string(duk_ctx, obj_idx, "blue_max");

    return 0;
}

static int
push_string(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    const char *value;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    duk_push_string(duk_ctx, value);

    return 0;
}

static int
push_timestamp(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct timespec timestamp;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_timestamp(packet, &timestamp);
    SOL_INT_CHECK(r, < 0, r);

    obj_idx = duk_push_object(duk_ctx);

    duk_push_number(duk_ctx, timestamp.tv_sec);
    duk_put_prop_string(duk_ctx, obj_idx, "tv_sec");

    duk_push_number(duk_ctx, timestamp.tv_nsec);
    duk_put_prop_string(duk_ctx, obj_idx, "tv_nsec");

    return 0;
}

static int
push_direction_vector(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_direction_vector dir;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_direction_vector(packet, &dir);
    SOL_INT_CHECK(r, < 0, r);

    obj_idx = duk_push_object(duk_ctx);

    duk_push_number(duk_ctx, dir.x);
    duk_put_prop_string(duk_ctx, obj_idx, "x");

    duk_push_number(duk_ctx, dir.y);
    duk_put_prop_string(duk_ctx, obj_idx, "y");

    duk_push_number(duk_ctx, dir.z);
    duk_put_prop_string(duk_ctx, obj_idx, "z");

    duk_push_number(duk_ctx, dir.min);
    duk_put_prop_string(duk_ctx, obj_idx, "min");

    duk_push_number(duk_ctx, dir.max);
    duk_put_prop_string(duk_ctx, obj_idx, "max");

    return 0;
}

static void
copy_blob_to_stack(const struct sol_blob *blob, duk_context *duk_ctx)
{
    void *mem;

    mem = duk_push_fixed_buffer(duk_ctx, blob->size);
    memcpy(mem, blob->mem, blob->size);
}

static int
push_blob(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_blob(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    /* FIXME: Should we add the other fields, like parent, ref count and size? */
    /* FIXME: If we bump the version use duk_push_external_buffer() */
    copy_blob_to_stack(blob, duk_ctx);
    return 0;
}

static int
push_location(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_location loc;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_location(packet, &loc);
    SOL_INT_CHECK(r, < 0, r);

    obj_idx = duk_push_object(duk_ctx);

    duk_push_number(duk_ctx, loc.lat);
    duk_put_prop_string(duk_ctx, obj_idx, "lat");

    duk_push_number(duk_ctx, loc.lon);
    duk_put_prop_string(duk_ctx, obj_idx, "lon");

    duk_push_number(duk_ctx, loc.alt);
    duk_put_prop_string(duk_ctx, obj_idx, "alt");
    return 0;
}

static int
push_json_array(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_json_array(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    duk_push_lstring(duk_ctx, (const char *)blob->mem, blob->size);
    return 0;
}

static int
push_json_object(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
    struct sol_blob *blob;
    int r;

    r = sol_flow_packet_get_json_object(packet, &blob);
    SOL_INT_CHECK(r, < 0, r);

    duk_push_lstring(duk_ctx, (const char *)blob->mem, blob->size);
    return 0;
}

static void
add_sol_key_valueto_js_array(const struct sol_vector *vector,
    duk_context *duk_ctx, duk_idx_t request_idx, const char *prop_name)
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
push_http_response(const struct sol_flow_packet *packet, duk_context *duk_ctx)
{
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

    obj_idx = duk_push_object(duk_ctx);

    duk_push_number(duk_ctx, code);
    duk_put_prop_string(duk_ctx, obj_idx, "response_code");

    duk_push_string(duk_ctx, url);
    duk_put_prop_string(duk_ctx, obj_idx, "url");

    duk_push_string(duk_ctx, content_type);
    duk_put_prop_string(duk_ctx, obj_idx, "content-type");

    copy_blob_to_stack(content, duk_ctx);
    duk_put_prop_string(duk_ctx, obj_idx, "content");

    add_sol_key_valueto_js_array(&cookies, duk_ctx, obj_idx, "cookies");
    add_sol_key_valueto_js_array(&headers, duk_ctx, obj_idx, "headers");

    return 0;
}

static int
process_simple_packet(const struct sol_flow_packet *packet,
    duk_context *duk_ctx)
{
    const struct sol_flow_packet_type *packet_type =
        sol_flow_packet_get_type(packet);

    if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)
        return push_boolean(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE)
        return push_byte(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_ERROR)
        return push_error(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)
        return push_float(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)
        return push_int(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_RGB)
        return push_rgb(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_STRING)
        return push_string(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BLOB)
        return push_blob(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_LOCATION)
        return push_location(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_TIMESTAMP)
        return push_timestamp(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR)
        return push_direction_vector(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_OBJECT)
        return push_json_object(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_JSON_ARRAY)
        return push_json_array(packet, duk_ctx);
    if (packet_type == SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE)
        return push_http_response(packet, duk_ctx);

    return -EINVAL;
}

/* process() methods on JS may throw exceptions. */
static int
flow_js_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct flow_js_data *mdata = data;
    int r;

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    r = process_simple_packet(packet, mdata->duk_ctx);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);

err_exit:
    duk_pop_n(mdata->duk_ctx, 3);
    return r;
}

static int
flow_js_composed_port_process(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct flow_js_data *mdata = data;
    int r;
    uint16_t i, len;
    struct sol_flow_packet **children;
    duk_idx_t array_idx;

    r = sol_flow_packet_get_composed_members(packet, &children, &len);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    array_idx = duk_push_array(mdata->duk_ctx);

    for (i = 0; i < len; i++) {
        r = process_simple_packet(children[i], mdata->duk_ctx);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);
        duk_put_prop_index(mdata->duk_ctx, array_idx, i);
    }

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);

err_exit:
    duk_pop_n(mdata->duk_ctx, 4); //Remove array and boilerplate_pre stuff.
    return r;
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

static int
add_port_for_meta_type_description(const char *name, const char *type_name,
    bool is_input, void *data)
{
    struct flow_js_type *type = data;
    struct flow_js_port_in *port_in_type;
    struct flow_js_port_out *port_out_type;
    const struct sol_flow_packet_type *packet_type;

    packet_type = sol_flow_packet_type_from_string(sol_str_slice_from_str(type_name));
    SOL_NULL_CHECK(packet_type, -EINVAL);

    if (is_input) {
        port_in_type = sol_vector_append(&type->ports_in);
        SOL_NULL_CHECK(port_in_type, -ENOMEM);

        SOL_SET_API_VERSION(port_in_type->type.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION; )
        port_in_type->type.packet_type = packet_type;
        port_in_type->type.process =
            sol_flow_packet_is_composed_type(packet_type) ?
            flow_js_composed_port_process : flow_js_port_process;
        port_in_type->type.connect = flow_js_port_in_connect;
        port_in_type->type.disconnect = flow_js_port_in_disconnect;

        port_in_type->name = sol_arena_strdup(type->str_arena, name);
        SOL_NULL_CHECK(port_in_type->name, -ENOMEM);

        port_in_type->type_name = sol_arena_strdup(type->str_arena, type_name);
        SOL_NULL_CHECK(port_in_type->type_name, -ENOMEM);
    } else {
        port_out_type = sol_vector_append(&type->ports_out);
        SOL_NULL_CHECK(port_out_type, -ENOMEM);

        SOL_SET_API_VERSION(port_out_type->type.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION; )
        port_out_type->type.packet_type = packet_type;
        port_out_type->type.connect = flow_js_port_out_connect;
        port_out_type->type.disconnect = flow_js_port_out_disconnect;

        port_out_type->name = sol_arena_strdup(type->str_arena, name);
        SOL_NULL_CHECK(port_out_type->name, -ENOMEM);

        port_out_type->type_name = sol_arena_strdup(type->str_arena, type_name);
        SOL_NULL_CHECK(port_out_type->type_name, -ENOMEM);
    }
    return 0;
}

static int
add_port_for_generated_code(const char *name, const char *type_name,
    bool is_input, void *data)
{
    int r = -ENOMEM;
    const char *port_type_name, *process_func;
    struct sol_vector *vector;
    struct flow_js_port_description_context *ctx = data;
    struct sol_flow_metatype_port_description *port_desc;
    const struct sol_flow_packet_type *packet_type;

    if (is_input) {
        vector = ctx->in;
        port_type_name = "in";
        packet_type = sol_flow_packet_type_from_string(sol_str_slice_from_str(type_name));
        SOL_NULL_CHECK(packet_type, -EINVAL);

        if (sol_flow_packet_is_composed_type(packet_type))
            process_func = ".base.process = js_metatype_composed_port_process,\n";
        else
            process_func = ".base.process = js_metatype_simple_port_process,\n";
    } else {
        vector = ctx->out;
        port_type_name = "out";
        process_func = "";
    }
    port_desc = sol_vector_append(vector);
    SOL_NULL_CHECK_GOTO(port_desc, err_exit);

    port_desc->name = strdup(name);
    SOL_NULL_CHECK_GOTO(port_desc->name, err_name);
    port_desc->type = strdup(type_name);
    SOL_NULL_CHECK_GOTO(port_desc->type, err_type);
    port_desc->array_size = 0;
    port_desc->idx = vector->len - 1;

    if (ctx->buf) {
        r = sol_buffer_append_printf(ctx->buf,
            "static struct js_metatype_port_%s js_metatype_%.*s_%s_port = {\n"
            "    SOL_SET_API_VERSION(.base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION, )\n"
            "    .base.connect = js_metatype_port_%s_connect,\n"
            "    .base.disconnect = js_metatype_port_%s_disconnect,\n"
            "    %s"
            "    .name = \"%s\"\n"
            "};\n", port_type_name, SOL_STR_SLICE_PRINT(ctx->name_prefix), port_desc->name,
            port_type_name, port_type_name, process_func, port_desc->name);
        SOL_INT_CHECK_GOTO(r, < 0, err_code);
    }

    return 0;

err_code:
    free(port_desc->type);
err_type:
    free(port_desc->name);
err_name:
    (void)sol_vector_del_element(vector, port_desc);
err_exit:
    return r;
}

static int
setup_port_properties(duk_context *duk_ctx, const char *prop_name,
    bool is_input, js_add_port add_port, void *add_port_data)
{
    uint16_t array_len, i;
    int r;

    if (!duk_has_prop_string(duk_ctx, -1, prop_name))
        return 0;

    duk_get_prop_string(duk_ctx, -1, prop_name);

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'in' property of variable 'ports' should be an array.");
        return -EINVAL;
    }

    if (!duk_get_prop_string(duk_ctx, -1, "length")) {
        SOL_ERR("Couldn't get 'in' length from 'ports' variable.");
        return -EINVAL;
    }

    array_len = duk_require_int(duk_ctx, -1);
    duk_pop(duk_ctx); /* length value */

    if (array_len == 0)
        return 0;

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

        r = add_port(duk_require_string(duk_ctx, -2),
            duk_require_string(duk_ctx, -1), is_input, add_port_data);
        SOL_INT_CHECK(r, < 0, r);

        duk_pop_3(duk_ctx);
    }

    duk_pop(duk_ctx); /* in value */

    return 0;
}

static int
setup_ports(const struct sol_buffer *buf, js_add_port add_port,
    void *add_port_data)
{
    duk_context *duk_ctx;
    int r;

    duk_ctx = duk_create_heap_default();
    if (!duk_ctx) {
        SOL_ERR("Failed to create a Duktape heap");
        return -ENOMEM;
    }

    if (duk_peval_lstring(duk_ctx, buf->data, buf->used) != 0) {
        SOL_ERR("Failed to parse javascript content: %s", duk_safe_to_string(duk_ctx, -1));
        duk_destroy_heap(duk_ctx);
        return -EINVAL;
    }
    duk_pop(duk_ctx); /* duk_peval_lstring() result */

    duk_push_global_object(duk_ctx);

    if (!duk_get_prop_string(duk_ctx, -1, "node")) {
        SOL_ERR("'node' variable not found in javascript file.");
        duk_destroy_heap(duk_ctx);
        return -EINVAL;
    }

    r = setup_port_properties(duk_ctx, "in", true, add_port, add_port_data);
    SOL_INT_CHECK_GOTO(r, < 0, exit);
    r = setup_port_properties(duk_ctx, "out", false, add_port, add_port_data);
    SOL_INT_CHECK_GOTO(r, < 0, exit);
exit:
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

static int
flow_js_type_init(struct flow_js_type *type, const struct sol_buffer *buf)
{
    char *js_content_buf;
    int r;

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

    type->str_arena = sol_arena_new();
    SOL_NULL_CHECK(type->str_arena, -ENOMEM);

    sol_vector_init(&type->ports_out, sizeof(struct flow_js_port_out));
    sol_vector_init(&type->ports_in, sizeof(struct flow_js_port_in));

    r = setup_ports(buf, add_port_for_meta_type_description, type);
    SOL_INT_CHECK(r, < 0, r);

    type->base.ports_in_count = type->ports_in.len;
    type->base.ports_out_count = type->ports_out.len;

    js_content_buf = strndup(buf->data, buf->used);
    SOL_NULL_CHECK(js_content_buf, -ENOMEM);

    type->js_content_buf = js_content_buf;
    type->js_content_buf_len = buf->used;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (setup_description(type) < 0)
        SOL_WRN("Failed to setup description");
#endif

    return 0;
}

static struct sol_flow_node_type *
sol_flow_js_new_type(const struct sol_buffer *buf)
{
    struct flow_js_type *type;
    int r;

    SOL_LOG_INTERNAL_INIT_ONCE;

    type = calloc(1, sizeof(struct flow_js_type));
    SOL_NULL_CHECK(type, NULL);

    r = flow_js_type_init(type, buf);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return &type->base;

err_exit:
    flow_js_type_fini(type);
    free(type);
    return NULL;
}

static int
read_file_contents(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *buf)
{
    const char *filename;

    filename = strndupa(ctx->contents.data, ctx->contents.len);
    return ctx->read_file(ctx, filename, buf);
}

static int
js_create_type(
    const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type)
{
    struct sol_buffer buf;
    struct sol_flow_node_type *result;
    int err;

    if (read_file_contents(ctx, &buf) < 0)
        return -EINVAL;

    result = sol_flow_js_new_type(&buf);
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

static int
setup_js_ports_description(const struct sol_buffer *buf,
    struct sol_vector *in, struct sol_vector *out, struct sol_buffer *out_buf,
    const struct sol_str_slice name_prefix)
{
    struct flow_js_port_description_context port_ctx;

    sol_vector_init(in, sizeof(struct sol_flow_metatype_port_description));
    sol_vector_init(out, sizeof(struct sol_flow_metatype_port_description));
    port_ctx.in = in;
    port_ctx.out = out;
    port_ctx.buf = out_buf;
    port_ctx.name_prefix = name_prefix;

    return setup_ports(buf, add_port_for_generated_code, &port_ctx);
}

static int
js_ports_description(const struct sol_flow_metatype_context *ctx,
    struct sol_vector *in, struct sol_vector *out)
{
    int err;
    struct sol_buffer buf;
    struct sol_str_slice empty = SOL_STR_SLICE_EMPTY;

    SOL_NULL_CHECK(ctx, -EINVAL);
    SOL_NULL_CHECK(out, -EINVAL);
    SOL_NULL_CHECK(in, -EINVAL);

    err = read_file_contents(ctx, &buf);
    SOL_INT_CHECK(err, < 0, err);

    return setup_js_ports_description(&buf, in, out, NULL, empty);
}

static int
js_generate_start(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *out)
{
    SOL_NULL_CHECK(ctx, -EINVAL);
    SOL_NULL_CHECK(out, -EINVAL);

    return sol_buffer_append_slice(out,
        sol_str_slice_from_str(JS_CODE_START));
}

static int
setup_get_port_function(struct sol_buffer *out, struct sol_vector *ports,
    const struct sol_str_slice prefix, const char *port_type)
{
    int r;
    uint16_t i;
    struct sol_flow_metatype_port_description *port;

    r = sol_buffer_append_printf(out,
        "static const struct sol_flow_port_type_%s *\n"
        "js_metatype_%.*s_get_%s_port(const struct sol_flow_node_type *type, uint16_t port)\n"
        "{\n", port_type, SOL_STR_SLICE_PRINT(prefix), port_type);
    SOL_INT_CHECK(r, < 0, r);

    SOL_VECTOR_FOREACH_IDX (ports, port, i) {
        r = sol_buffer_append_printf(out, "    if (port == %u)\n"
            "        return &js_metatype_%.*s_%s_port.base;\n",
            i, SOL_STR_SLICE_PRINT(prefix), port->name);
        SOL_INT_CHECK(r, < 0, r);
    }

    return sol_buffer_append_slice(out, sol_str_slice_from_str("    return NULL;\n}\n"));
}

static int
setup_composed_packet(struct sol_buffer *out, const struct sol_str_slice prefix,
    const struct sol_str_slice types, const char *port_name)
{
    int r;
    struct sol_vector tokens;
    struct sol_str_slice *token;
    uint16_t i;

    r = sol_buffer_append_slice(out,
        sol_str_slice_from_str("        const struct sol_flow_packet_type *types[] = {"));
    SOL_INT_CHECK(r, < 0, r);

    tokens = sol_str_slice_split(types, ",", 0);

    SOL_VECTOR_FOREACH_IDX (&tokens, token, i) {
        r = sol_buffer_append_printf(out, "%s,",
            sol_flow_get_packet_type_name(*token));
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sol_buffer_append_printf(out, "NULL};\n"
        "        js_metatype_%.*s_%s_port.base.packet_type = sol_flow_packet_type_composed_new(types);\n",
        SOL_STR_SLICE_PRINT(prefix), port_name);

exit:
    sol_vector_clear(&tokens);
    return r;
}

static int
setup_packet_type(struct sol_buffer *out, struct sol_vector *ports,
    const struct sol_str_slice prefix)
{
    int r;
    uint16_t i;
    struct sol_flow_metatype_port_description *port;
    const struct sol_flow_packet_type *packet_type;


    SOL_VECTOR_FOREACH_IDX (ports, port, i) {
        packet_type = sol_flow_packet_type_from_string(sol_str_slice_from_str(port->type));
        SOL_NULL_CHECK(packet_type, -EINVAL);
        r = sol_buffer_append_printf(out, "    if (!js_metatype_%.*s_%s_port.base.packet_type) {\n",
            SOL_STR_SLICE_PRINT(prefix), port->name);
        SOL_INT_CHECK(r, < 0, r);

        if (!sol_flow_packet_is_composed_type(packet_type)) {
            r = sol_buffer_append_printf(out,
                "        js_metatype_%.*s_%s_port.base.packet_type = %s;\n",
                SOL_STR_SLICE_PRINT(prefix), port->name,
                sol_flow_get_packet_type_name(
                sol_str_slice_from_str(port->type)));
        } else {
            struct sol_str_slice types;
            //Removing the composed: prefix
            types.data = port->type + 9;
            types.len = strlen(port->type) - 9;
            r = setup_composed_packet(out, prefix, types, port->name);
        }
        SOL_INT_CHECK(r, < 0, r);

        r = sol_buffer_append_slice(out, sol_str_slice_from_str("    }\n"));
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
setup_init_function(struct sol_buffer *out, struct sol_vector *in_ports,
    struct sol_vector *out_ports, const struct sol_str_slice prefix)
{
    int r;

    r = sol_buffer_append_printf(out,
        "static void\njs_metatype_%.*s_init(void)\n{\n",
        SOL_STR_SLICE_PRINT(prefix));
    SOL_INT_CHECK(r, < 0, r);

    r = setup_packet_type(out, in_ports, prefix);
    SOL_INT_CHECK(r, < 0, r);
    r = setup_packet_type(out, out_ports, prefix);
    SOL_INT_CHECK(r, < 0, r);

    return sol_buffer_append_slice(out, sol_str_slice_from_str("}\n"));
}

static void
metatype_port_description_clear(struct sol_vector *port_descriptions)
{
    uint16_t i;
    struct sol_flow_metatype_port_description *port;

    SOL_VECTOR_FOREACH_IDX (port_descriptions, port, i) {
        free(port->name);
        free(port->type);
    }
    sol_vector_clear(port_descriptions);
}

static int
js_generate_body(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *out)
{
    struct sol_buffer buf;
    size_t i;
    struct sol_vector in_ports, out_ports;
    int r;

    r = read_file_contents(ctx, &buf);
    SOL_INT_CHECK(r, < 0, r);

    r = setup_js_ports_description(&buf, &in_ports, &out_ports, out, ctx->name);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out, "static const char %.*s_JS_CODE[] = {\n",
        SOL_STR_SLICE_PRINT(ctx->name));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    for (i = 0; i < buf.used; i++) {
        r = sol_buffer_append_printf(out, "%d,", ((char *)buf.data)[i]);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = sol_buffer_append_slice(out, sol_str_slice_from_str("};\n"));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out, "static int\n"
        "js_metatype_%.*s_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)\n"
        "{\n"
        "    duk_context **ctx = data;\n"
        "    return js_metatype_common_open(node, ctx, %.*s_JS_CODE, sizeof(%.*s_JS_CODE));\n"
        "}\n",
        SOL_STR_SLICE_PRINT(ctx->name), SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = setup_get_port_function(out, &in_ports, ctx->name, "in");
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = setup_get_port_function(out, &out_ports, ctx->name, "out");
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = setup_init_function(out, &in_ports, &out_ports, ctx->name);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out,
        "static const struct sol_flow_node_type %.*s = {\n"
        "   SOL_SET_API_VERSION(.api_version = SOL_FLOW_NODE_TYPE_API_VERSION, )\n"
        "   .options_size = sizeof(struct sol_flow_node_options),\n"
        "   .data_size = sizeof(duk_context **),\n"
        "   .ports_out_count = %u,\n"
        "   .ports_in_count = %u,\n"
        "   .dispose_type = NULL,\n"
        "   .open = js_metatype_%.*s_open,\n"
        "   .close = js_metatype_close,\n"
        "   .get_port_out = js_metatype_%.*s_get_out_port,\n"
        "   .get_port_in = js_metatype_%.*s_get_in_port,\n"
        "   .init_type = js_metatype_%.*s_init,\n"
        "};\n",
        SOL_STR_SLICE_PRINT(ctx->name),
        out_ports.len,
        in_ports.len,
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name),
        SOL_STR_SLICE_PRINT(ctx->name));

exit:
    metatype_port_description_clear(&in_ports);
    metatype_port_description_clear(&out_ports);
    return r;
}

static int
js_generate_end(const struct sol_flow_metatype_context *ctx,
    struct sol_buffer *out)
{
    return 0;
}

SOL_FLOW_METATYPE(JS,
    .name = "js",
    .create_type = js_create_type,
    .generate_type_start = js_generate_start,
    .generate_type_body = js_generate_body,
    .generate_type_end = js_generate_end,
    .ports_description = js_ports_description,
    );
