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

#include <string.h>
#include <alloca.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include "sol-str-slice.h"
#include "sol-vector.h"
#include "sol-util.h"
#include "sol-flow-packet.h"
#include "sol-types.h"
#include "sol-flow-composed.h"
#include "sol-log.h"
#include "sol-str-table.h"

#define DELIM ("|")
#define INPUT_PORT_NAME ("IN")
#define OUTPUT_PORT_NAME ("OUT")

struct composed_node_type {
    struct sol_flow_node_type base;
    struct sol_vector in_ports;
    struct sol_vector out_ports;
};

struct composed_node_port_type {
    char *name;
    bool is_input;
    union {
        struct sol_flow_port_type_in in;
        struct sol_flow_port_type_out out;
    } type;
};

struct composed_node_data {
    uint16_t inputs_len;
    const struct sol_flow_packet_type *composed_type;
    struct sol_flow_packet **inputs;
};

static void
composed_node_close(struct sol_flow_node *node, void *data)
{
    struct composed_node_data *cdata = data;
    uint16_t i;

    for (i = 0; i < cdata->inputs_len; i++)
        sol_flow_packet_del(cdata->inputs[i]);
    free(cdata->inputs);
}

static int
composed_node_open(struct sol_flow_node *node, void *data,
    const struct sol_flow_node_options *options)
{
    struct composed_node_data *cdata = data;
    const struct composed_node_type *self;
    const struct composed_node_port_type *port_type;

    self = (const struct composed_node_type *)
        sol_flow_node_get_type(node);

    cdata->inputs_len = self->in_ports.len;
    cdata->inputs = calloc(cdata->inputs_len, sizeof(struct sol_flow_packet *));
    SOL_NULL_CHECK(cdata->inputs, -ENOMEM);
    port_type = sol_vector_get(&self->out_ports, 0);
    cdata->composed_type = port_type->type.out.packet_type;

    return 0;
}

static void
composed_node_type_dispose(struct sol_flow_node_type *type)
{
    struct composed_node_type *self = (struct composed_node_type *)type;
    struct composed_node_port_type *port_type;
    uint16_t i;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    struct sol_flow_node_type_description *desc;

    desc = (struct sol_flow_node_type_description *)self->base.description;
    if (desc) {
        if (desc->ports_in) {
            for (i = 0; i < self->in_ports.len; i++)
                free((struct sol_flow_port_description *)desc->ports_in[i]);
            free((struct sol_flow_port_description **)desc->ports_in);
        }
        if (desc->ports_out) {
            for (i = 0; i < self->out_ports.len; i++)
                free((struct sol_flow_port_description *)desc->ports_out[i]);
            free((struct sol_flow_port_description **)desc->ports_out);
        }
        free(desc);
    }
#endif

    SOL_VECTOR_FOREACH_IDX (&self->in_ports, port_type, i)
        free(port_type->name);
    SOL_VECTOR_FOREACH_IDX (&self->out_ports, port_type, i)
        free(port_type->name);

    sol_vector_clear(&self->in_ports);
    sol_vector_clear(&self->out_ports);
    free(self);
}

static const struct sol_flow_packet_type *
get_packet_type(const struct sol_str_slice type)
{
    if (sol_str_slice_str_eq(type, "int"))
        return SOL_FLOW_PACKET_TYPE_IRANGE;
    if (sol_str_slice_str_eq(type, "float"))
        return SOL_FLOW_PACKET_TYPE_DRANGE;
    if (sol_str_slice_str_eq(type, "string"))
        return SOL_FLOW_PACKET_TYPE_STRING;
    if (sol_str_slice_str_eq(type, "boolean"))
        return SOL_FLOW_PACKET_TYPE_BOOLEAN;
    if (sol_str_slice_str_eq(type, "byte"))
        return SOL_FLOW_PACKET_TYPE_BYTE;
    if (sol_str_slice_str_eq(type, "blob"))
        return SOL_FLOW_PACKET_TYPE_BLOB;
    if (sol_str_slice_str_eq(type, "rgb"))
        return SOL_FLOW_PACKET_TYPE_RGB;
    if (sol_str_slice_str_eq(type, "location"))
        return SOL_FLOW_PACKET_TYPE_LOCATION;
    if (sol_str_slice_str_eq(type, "timestamp"))
        return SOL_FLOW_PACKET_TYPE_TIMESTAMP;
    if (sol_str_slice_str_eq(type, "direction-vector"))
        return SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR;
    if (sol_str_slice_str_eq(type, "error"))
        return SOL_FLOW_PACKET_TYPE_ERROR;
    if (sol_str_slice_str_eq(type, "json-object"))
        return SOL_FLOW_PACKET_TYPE_JSON_OBJECT;
    if (sol_str_slice_str_eq(type, "json-array"))
        return SOL_FLOW_PACKET_TYPE_JSON_ARRAY;
    if (sol_str_slice_str_eq(type, "http-request"))
        return SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE;
    return NULL;
}

static const char *
get_packet_type_as_string(const struct sol_str_slice type)
{
    static const struct sol_str_table_ptr map[] = {
        SOL_STR_TABLE_PTR_ITEM("int", "SOL_FLOW_PACKET_TYPE_IRANGE"),
        SOL_STR_TABLE_PTR_ITEM("float", "SOL_FLOW_PACKET_TYPE_DRANGE"),
        SOL_STR_TABLE_PTR_ITEM("string", "SOL_FLOW_PACKET_TYPE_STRING"),
        SOL_STR_TABLE_PTR_ITEM("boolean", "SOL_FLOW_PACKET_TYPE_BOOLEAN"),
        SOL_STR_TABLE_PTR_ITEM("byte", "SOL_FLOW_PACKET_TYPE_BYTE"),
        SOL_STR_TABLE_PTR_ITEM("blob", "SOL_FLOW_PACKET_TYPE_BLOB"),
        SOL_STR_TABLE_PTR_ITEM("rgb", "SOL_FLOW_PACKET_TYPE_RGB"),
        SOL_STR_TABLE_PTR_ITEM("location", "SOL_FLOW_PACKET_TYPE_LOCATION"),
        SOL_STR_TABLE_PTR_ITEM("timestamp", "SOL_FLOW_PACKET_TYPE_TIMESTAMP"),
        SOL_STR_TABLE_PTR_ITEM("direction-vector",
            "SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR"),
        SOL_STR_TABLE_PTR_ITEM("error", "SOL_FLOW_PACKET_TYPE_ERROR"),
        SOL_STR_TABLE_PTR_ITEM("json-object", "SOL_FLOW_PACKET_TYPE_JSON_OBJECT"),
        SOL_STR_TABLE_PTR_ITEM("json-array", "SOL_FLOW_PACKET_TYPE_JSON_ARRAY"),
        SOL_STR_TABLE_PTR_ITEM("http-request", "SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE")
    };

    return sol_str_table_ptr_lookup_fallback(map, type, NULL);
}

static int
simple_port_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct composed_node_data *cdata = data;
    uint16_t i;

    if (cdata->inputs[port]) {
        sol_flow_packet_del(cdata->inputs[port]);
        cdata->inputs[port] = NULL;
    }

    cdata->inputs[port] = sol_flow_packet_dup(packet);
    SOL_NULL_CHECK(cdata->inputs[port], -ENOMEM);

    for (i = 0; i < cdata->inputs_len; i++) {
        if (!cdata->inputs[i])
            break;
    }

    if (i != cdata->inputs_len)
        return 0;

    return sol_flow_send_composed_packet(node, 0, cdata->composed_type,
        cdata->inputs);
}

static int
composed_port_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    uint16_t len, i;
    struct sol_flow_packet **children, *out_packet;

    r = sol_flow_packet_get_composed_members(packet, &children, &len);
    SOL_INT_CHECK(r, < 0, r);

    for (i = 0; i < len; i++) {
        out_packet = sol_flow_packet_dup(children[i]);
        SOL_NULL_CHECK(out_packet, -ENOMEM);
        r = sol_flow_send_packet(node, i, out_packet);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
get_name_and_type_from_token(const struct sol_str_slice *token, char **name,
    struct sol_str_slice *type)
{
    char *start, *end;

    start = memchr(token->data, '(', token->len);
    SOL_NULL_CHECK(start, -EINVAL);

    end = memrchr(token->data, ')', token->len);
    SOL_NULL_CHECK(end, -EINVAL);

    *name = strndup(token->data, start - token->data);
    SOL_NULL_CHECK(*name, -ENOMEM);

    type->data = start + 1;
    type->len = end - start - 1;
    return 0;
}

static int
setup_simple_ports(struct sol_vector *in_ports, const struct sol_str_slice contents, bool is_input)
{
    struct sol_vector tokens;
    struct sol_str_slice *slice, type_slice, pending_slice;
    struct composed_node_port_type *port_type;
    const struct sol_flow_packet_type *packet_type;
    struct sol_buffer buf;
    char *name;
    size_t i_slice;
    uint16_t i;
    int r;

    sol_buffer_init(&buf);

    pending_slice.data = contents.data;
    pending_slice.len = 0;

    for (i_slice = 0; i_slice < contents.len; i_slice++) {
        if (isspace(contents.data[i_slice])) {
            if (pending_slice.len != 0) {
                r = sol_buffer_append_slice(&buf, pending_slice);
                if (r) {
                    SOL_ERR("Could not append a slice in the buffer");
                    sol_buffer_fini(&buf);
                    return r;
                }
            }
            pending_slice.data = contents.data + i_slice + 1;
            pending_slice.len = 0;
        } else
            pending_slice.len++;
    }

    if (pending_slice.len > 0) {
        r = sol_buffer_append_slice(&buf, pending_slice);
        if (r) {
            SOL_ERR("Could not append slice to the buffer");
            sol_buffer_fini(&buf);
            return r;
        }
    }

    tokens = sol_util_str_split(sol_buffer_get_slice(&buf), DELIM, 0);

    if (tokens.len < 2) {
        SOL_ERR("A composed node must have at least two ports. Contents:%.*s",
            SOL_STR_SLICE_PRINT(contents));
        sol_vector_clear(&tokens);
        sol_buffer_fini(&buf);
        return -EINVAL;
    }

    SOL_VECTOR_FOREACH_IDX (&tokens, slice, i) {
        name = NULL;
        r = get_name_and_type_from_token(slice, &name, &type_slice);
        SOL_INT_CHECK_GOTO(r, < 0, err_exit);

        port_type = sol_vector_append(in_ports);
        if (!port_type) {
            r = -ENOMEM;
            SOL_ERR("Could not create a port");
            goto err_exit;
        }

        packet_type = get_packet_type(type_slice);

        if (!packet_type) {
            r = -EINVAL;
            SOL_ERR("It's not possible to use %.*s as a port type.",
                SOL_STR_SLICE_PRINT(type_slice));
            goto err_exit;
        }

        if (is_input) {
            port_type->type.in.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION;
            port_type->type.in.packet_type = packet_type;
            port_type->type.in.process = simple_port_process;
        } else {
            port_type->type.out.api_version =
                SOL_FLOW_PORT_TYPE_OUT_API_VERSION;
            port_type->type.out.packet_type = packet_type;
        }

        port_type->name = name;
        port_type->is_input = is_input;
    }

    sol_vector_clear(&tokens);
    sol_buffer_fini(&buf);
    return 0;

err_exit:
    free(name);
    return r;
}

static int
setup_composed_port(struct sol_vector *simple_ports,
    struct composed_node_port_type *composed_port, bool is_splitter)
{
    struct composed_node_port_type *simple_port;
    const struct sol_flow_packet_type *composed_type;
    const struct sol_flow_packet_type **types;
    uint16_t i;

    types = alloca(sizeof(struct sol_flow_packet_type *) *
        (simple_ports->len + 1));

    SOL_VECTOR_FOREACH_IDX (simple_ports, simple_port, i)
        types[i] = simple_port->is_input ? simple_port->type.in.packet_type :
            simple_port->type.out.packet_type;

    types[i] = NULL;
    composed_type = sol_flow_packet_type_composed_new(types);
    SOL_NULL_CHECK(composed_type, -ENOMEM);

    if (is_splitter) {
        composed_port->name = strdup(INPUT_PORT_NAME);
        composed_port->type.in.process = composed_port_process;
        composed_port->type.in.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION;
        composed_port->type.in.packet_type = composed_type;
    } else {
        composed_port->name = strdup(OUTPUT_PORT_NAME);
        composed_port->type.out.api_version =
            SOL_FLOW_PORT_TYPE_OUT_API_VERSION;
        composed_port->type.out.packet_type = composed_type;
    }

    SOL_NULL_CHECK(composed_port->name, -ENOMEM);
    return 0;
}

static const struct sol_flow_port_type_in *
composed_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    struct composed_node_type *self = (struct composed_node_type *)type;
    struct composed_node_port_type *port_type;

    port_type = sol_vector_get(&self->in_ports, port);
    SOL_NULL_CHECK(port_type, NULL);
    return &port_type->type.in;
}

static const struct sol_flow_port_type_out *
composed_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    struct composed_node_type *self = (struct composed_node_type *)type;
    struct composed_node_port_type *port_type;

    port_type = sol_vector_get(&self->out_ports, port);
    SOL_NULL_CHECK(port_type, NULL);
    return &port_type->type.out;
}

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static const struct sol_flow_node_type_description sol_flow_node_type_composed_description = {
    .api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION,
    .name = "composed",
    .category = "composed",
    .symbol = "SOL_FLOW_NODE_TYPE_COMPOSED",
    .options_symbol = NULL,
    .version = NULL,
};

static int
setup_port_description(struct sol_vector *ports,
    struct sol_flow_port_description **p_desc, bool required)
{
    uint16_t i;
    struct composed_node_port_type *port_type;

    SOL_VECTOR_FOREACH_IDX (ports, port_type, i) {
        p_desc[i] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK(p_desc[i], -ENOMEM);
        p_desc[i]->name = port_type->name;
        p_desc[i]->description = port_type->is_input ?
            "Input port" : "Output port";
        p_desc[i]->data_type = port_type->is_input ?
            port_type->type.in.packet_type->name :
            port_type->type.out.packet_type->name;
        p_desc[i]->array_size = 0;
        p_desc[i]->base_port_idx = i;
        p_desc[i]->required = required;
    }
    return 0;
}

static int
setup_description(struct composed_node_type *self)
{
    struct sol_flow_port_description **p;
    struct sol_flow_node_type_description *desc;
    uint16_t i;
    int r;

    desc = malloc(sizeof(struct sol_flow_node_type_description));
    SOL_NULL_CHECK(desc, -ENOMEM);

    memcpy(desc, &sol_flow_node_type_composed_description,
        sizeof(struct sol_flow_node_type_description));

    desc->ports_in = calloc(self->in_ports.len + 1,
        sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_in, err_ports_in);

    r = setup_port_description(&self->in_ports,
        (struct sol_flow_port_description **)desc->ports_in, true);
    SOL_INT_CHECK_GOTO(r, < 0, err_ports_in_desc);

    desc->ports_out = calloc(self->out_ports.len + 1,
        sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_out, err_ports_out);

    r = setup_port_description(&self->out_ports,
        (struct sol_flow_port_description **)desc->ports_out, false);
    SOL_INT_CHECK_GOTO(r, < 0, err_ports_out_desc);

    self->base.description = desc;

    return 0;

err_ports_out_desc:
    p = (struct sol_flow_port_description **)desc->ports_out;
    for (i = 0; p[i]; i++)
        free(p[i]);
    free((struct sol_flow_port_description **)desc->ports_out);
err_ports_out:
err_ports_in_desc:
    p = (struct sol_flow_port_description **)desc->ports_in;
    for (i = 0; p[i]; i++)
        free(p[i]);
    free((struct sol_flow_port_description **)desc->ports_in);
err_ports_in:
    free(desc);
    return -ENOMEM;
}

#endif

static int
create_type(const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type,
    bool is_splitter)
{
    struct composed_node_type *self;
    struct sol_vector *composed_vector, *simple_ports;
    struct composed_node_port_type *composed_port;
    int r;

    self = calloc(1, sizeof(struct composed_node_type));
    SOL_NULL_CHECK(self, -ENOMEM);

    self->base = (struct sol_flow_node_type) {
        .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
        .data_size = is_splitter ? 0 : sizeof(struct composed_node_data),
        .dispose_type = composed_node_type_dispose,
        .get_port_in = composed_get_port_in,
        .get_port_out = composed_get_port_out,
        .options_size = sizeof(struct sol_flow_node_options),
        .open = is_splitter ? NULL : composed_node_open,
        .close = is_splitter ? NULL : composed_node_close
    };

    sol_vector_init(&self->in_ports, sizeof(struct composed_node_port_type));
    sol_vector_init(&self->out_ports, sizeof(struct composed_node_port_type));

    if (!is_splitter) {
        r = setup_simple_ports(&self->in_ports, ctx->contents, true);
        composed_vector = &self->out_ports;
        simple_ports = &self->in_ports;
    } else {
        r = setup_simple_ports(&self->out_ports, ctx->contents, false);
        composed_vector = &self->in_ports;
        simple_ports = &self->out_ports;
    }

    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    composed_port = sol_vector_append(composed_vector);
    SOL_NULL_CHECK_GOTO(composed_port, err_exit);

    r = setup_composed_port(simple_ports, composed_port, is_splitter);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    self->base.ports_in_count = self->in_ports.len;
    self->base.ports_out_count = self->out_ports.len;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    r = setup_description(self);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
#endif

    r = ctx->store_type(ctx, &self->base);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);
    *type = &self->base;
    return 0;

err_exit:
    sol_flow_node_type_del(&self->base);
    return r;
}

int
create_composed_constructor_type(const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type)
{
    return create_type(ctx, type, false);
}

int
create_composed_splitter_type(const struct sol_flow_metatype_context *ctx,
    struct sol_flow_node_type **type)
{
    return create_type(ctx, type, true);
}

static int
setup_ports_vector(struct sol_vector *tokens, struct sol_vector *ports,
    bool use_type_symbol)
{
    int r;
    uint16_t i;
    struct sol_str_slice *slice, type_slice;
    struct sol_flow_metatype_port_description *port;

    sol_vector_init(ports, sizeof(struct sol_flow_metatype_port_description));
    SOL_VECTOR_FOREACH_IDX (tokens, slice, i) {
        port = sol_vector_append(ports);
        SOL_NULL_CHECK(port, -ENOMEM);
        r = get_name_and_type_from_token(slice, &port->name, &type_slice);
        SOL_INT_CHECK(r, < 0, r);
        port->type = use_type_symbol ?
            (char *)get_packet_type_as_string(type_slice) :
            strndup(type_slice.data, type_slice.len);
        SOL_NULL_CHECK(port->type, -EINVAL);
        port->idx = i;
        port->array_size = 0;
    }

    return 0;
}

static int
generate_metatype_data(struct sol_buffer *out)
{
    return sol_buffer_append_printf(out,
        "struct composed_data {\n"
        "  uint16_t inputs_len;\n"
        "  const struct sol_flow_packet_type *composed_type;\n"
        "  struct sol_flow_packet **inputs;\n"
        "};\n");
}

static int
generate_metatype_close(struct sol_buffer *out)
{
    return sol_buffer_append_printf(out,
        "static void\n"
        "composed_metatype_close(struct sol_flow_node *node, void *data)\n"
        "{\n"
        "   struct composed_data *cdata = data;\n"
        "   uint16_t i;\n"
        "   for (i = 0; i < cdata->inputs_len; i++)\n"
        "      sol_flow_packet_del(cdata->inputs[i]);\n"
        "   free(cdata->inputs);\n"
        "}\n");
}

static int
get_ports_from_contents(const struct sol_str_slice contents,
    struct sol_vector *ports, bool use_type_symbol)
{
    struct sol_vector tokens;
    int r;

    tokens = sol_util_str_split(contents, DELIM, 0);
    if (tokens.len < 2) {
        SOL_ERR("Invalid contents:%.*s", SOL_STR_SLICE_PRINT(contents));
        sol_vector_clear(&tokens);
        return -EINVAL;
    }
    r = setup_ports_vector(&tokens, ports, use_type_symbol);
    sol_vector_clear(&tokens);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
generate_metatype_composed_process(struct sol_buffer *out)
{
    return sol_buffer_append_printf(out,
        "static int\n"
        "composed_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)\n"
        "{\n"
        "    int r;\n"
        "    uint16_t i, len;\n"
        "    struct sol_flow_packet **children, *out_packet;\n"
        "    r = sol_flow_packet_get_composed_members(packet, &children,"
        "        &len);\n"
        "    SOL_INT_CHECK(r, < 0, r);\n"
        "    for (i = 0; i < len; i++) {\n"
        "        out_packet = sol_flow_packet_dup(children[i]);\n"
        "        SOL_NULL_CHECK(out_packet, -ENOMEM);\n"
        "        r = sol_flow_send_packet(node, i, out_packet);\n"
        "        SOL_INT_CHECK(r, < 0, r);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
}

static int
generate_metatype_get_ports(struct sol_buffer *out,
    const char *in_out, const struct sol_str_slice name,
    const struct sol_str_slice body)
{
    return sol_buffer_append_printf(out,
        "static const struct sol_flow_port_type_%s *\n"
        "composed_metatype_%.*s_get_%s_port(const struct sol_flow_node_type *type, uint16_t port)\n"
        "{\n"
        "%.*s"
        "   return NULL;\n}\n", in_out, SOL_STR_SLICE_PRINT(name), in_out,
        SOL_STR_SLICE_PRINT(body));
}

static int
generate_composed_get_port_function(struct sol_buffer *out,
    const char *in_out, const struct sol_str_slice name,
    struct sol_flow_metatype_port_description *port)
{
    int r;
    struct sol_buffer buf;

    sol_buffer_init(&buf);

    r = sol_buffer_append_printf(&buf,
        "   if (port < 1)\n"
        "      return &metatype_composed_%.*s_%s_port;\n",
        SOL_STR_SLICE_PRINT(name), port->name);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = generate_metatype_get_ports(out, in_out, name,
        sol_buffer_get_slice(&buf));

exit:
    sol_buffer_fini(&buf);
    return r;
}

static int
generate_metatype_open(struct sol_buffer *out,
    const char *open_signature, const char *composed_signature,
    uint16_t ports)
{
    return sol_buffer_append_printf(out,
        "static int\n"
        "%s(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)\n"
        "{\n"
        "   struct composed_data *cdata = data;\n"
        "   cdata->inputs_len = %u;\n"
        "   cdata->inputs = calloc(cdata->inputs_len, sizeof(struct sol_flow_packet *));\n"
        "   SOL_NULL_CHECK(cdata->inputs, -ENOMEM);\n"
        "   cdata->composed_type = %s();\n"
        "   SOL_NULL_CHECK_GOTO(cdata->composed_type, err_exit);\n"
        "   return 0;\n"
        "err_exit:\n"
        "   free(cdata->inputs);\n"
        "   return -ENOMEM;\n"
        "}\n", open_signature, ports, composed_signature);
}

static int
generate_metatype_port(struct sol_buffer *out,
    const struct sol_str_slice name, const char *process_func,
    const char *port_type,
    struct sol_flow_metatype_port_description *port)
{
    char buf[PATH_MAX];

    if (process_func)
        (void)snprintf(buf, sizeof(buf), ".process = %s,\n", process_func);
    else
        buf[0] = 0;

    return sol_buffer_append_printf(out,
        "static struct sol_flow_port_type_%s metatype_composed_%.*s_%s_port = {\n"
        "   .api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION,\n"
        "   .connect = NULL,\n"
        "   %s"
        "   .disconnect = NULL\n"
        "};\n", port_type, SOL_STR_SLICE_PRINT(name),
        port->name, buf);
}

static int
generate_metatype_composed_packet_init(struct sol_buffer *out,
    const struct sol_str_slice name, struct sol_vector *tokens,
    char **signature)
{
    struct sol_buffer types, names;
    uint16_t i;
    struct sol_flow_metatype_port_description *port;
    int r;

    sol_buffer_init(&types);
    sol_buffer_init(&names);

    SOL_VECTOR_FOREACH_IDX (tokens, port, i) {
        r = sol_buffer_append_printf(&names, "%s_", port->type);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
        r = sol_buffer_append_printf(&types, "%s, ", port->type);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = asprintf(signature,
        "sol_flow_metatype_composed_packet_%.*s_%.*sinit",
        SOL_STR_SLICE_PRINT(name),
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&names)));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out,
        "static const struct sol_flow_packet_type *\n"
        "%s(void)\n"
        "{\n"
        "    static const struct sol_flow_packet_type *packet = NULL;\n"
        "    if (!packet) {\n"
        "       const struct sol_flow_packet_type *types[] = {%.*sNULL};\n"
        "       packet = sol_flow_packet_type_composed_new(types);\n"
        "    }\n"
        "    return packet;\n"
        "}\n", *signature, SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&types)));

    if (r != 0) {
        free(*signature);
        *signature = NULL;
    }

exit:
    sol_buffer_fini(&types);
    sol_buffer_fini(&names);
    return r;
}

static int
generate_metatype_init(struct sol_buffer *out, const struct sol_str_slice name,
    struct sol_vector *ports,
    struct sol_flow_metatype_port_description *composed_port,
    const char *composed_signature)
{
    uint16_t i;
    int r;
    struct sol_flow_metatype_port_description *port;

    r = sol_buffer_append_printf(out, "static void\n"
        "composed_metatype_%.*s_init(void)\n{\n", SOL_STR_SLICE_PRINT(name));

    SOL_INT_CHECK(r, < 0, r);
    SOL_VECTOR_FOREACH_IDX (ports, port, i) {
        r = sol_buffer_append_printf(out,
            "   if (!metatype_composed_%.*s_%s_port.packet_type)\n"
            "       metatype_composed_%.*s_%s_port.packet_type = %s;\n",
            SOL_STR_SLICE_PRINT(name), port->name,
            SOL_STR_SLICE_PRINT(name), port->name, port->type);
        SOL_INT_CHECK(r, < 0, r);
    }
    r = sol_buffer_append_printf(out,
        "   if (!metatype_composed_%.*s_%s_port.packet_type)\n"
        "       metatype_composed_%.*s_%s_port.packet_type = %s();\n",
        SOL_STR_SLICE_PRINT(name), composed_port->name,
        SOL_STR_SLICE_PRINT(name), composed_port->name, composed_signature);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_buffer_append_printf(out, "}\n");
    return r;
}

static int
generate_metatype_simple_process(struct sol_buffer *out)
{
    return sol_buffer_append_printf(out,
        "static int\n"
        "simple_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)\n"
        "{\n"
        "    struct composed_data *cdata = data;\n"
        "    uint16_t i;\n"
        "    if (cdata->inputs[port]) {\n"
        "        sol_flow_packet_del(cdata->inputs[port]);\n"
        "        cdata->inputs[port] = NULL;\n"
        "    }\n"
        "    cdata->inputs[port] = sol_flow_packet_dup(packet);\n"
        "    SOL_NULL_CHECK(cdata->inputs[port], -ENOMEM);\n"
        "    for (i = 0; i < cdata->inputs_len; i++) {\n"
        "        if (!cdata->inputs[i])\n"
        "            break;\n"
        "    }\n"
        "    if (i != cdata->inputs_len)\n"
        "        return 0;\n"
        "    return sol_flow_send_composed_packet(node, 0, "
        "        cdata->composed_type, cdata->inputs);\n"
        "}\n");
}

static int
composed_metatype_generate_type_code(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents,
    bool is_splitter)
{
    const char *data_size, *composed_port_name, *close_func,
    *composed_port_type, *simple_port_type, *simple_port_process,
    *composed_port_process;
    char *packet_signature;
    uint16_t ports_in, ports_out, i;
    struct sol_vector ports;
    struct sol_flow_metatype_port_description *port, composed_port;
    int r;
    char open_func[PATH_MAX];
    struct sol_buffer ports_body;

    memset(&composed_port, 0,
        sizeof(struct sol_flow_metatype_port_description));
    packet_signature = NULL;
    sol_buffer_init(&ports_body);

    SOL_NULL_CHECK(out, -EINVAL);

    r = get_ports_from_contents(contents, &ports, true);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = generate_metatype_composed_packet_init(out, name, &ports,
        &packet_signature);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if (is_splitter) {
        ports_out = ports.len;
        ports_in = 1;
        data_size = "0";
        composed_port_name = "IN";
        close_func = "NULL";
        (void)snprintf(open_func, sizeof(open_func), "NULL");
        composed_port_type = "in";
        simple_port_type = "out";
        simple_port_process = NULL;
        composed_port_process = "composed_port_process";
    } else {
        ports_out = 1;
        ports_in = ports.len;
        data_size = "sizeof(struct composed_data)";
        composed_port_name = "OUT";
        close_func = "composed_metatype_close";
        composed_port_type = "out";
        simple_port_type = "in";
        simple_port_process = "simple_port_process";
        composed_port_process = NULL;
        r = snprintf(open_func, sizeof(open_func),
            "composed_metatype_%.*s_open", SOL_STR_SLICE_PRINT(name));
        if (r < 0 || r >= PATH_MAX) {
            SOL_ERR("Could not create the open function name for %.*s",
                SOL_STR_SLICE_PRINT(name));
            goto exit;
        }
        r = generate_metatype_open(out, open_func, packet_signature, ports.len);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    SOL_VECTOR_FOREACH_IDX (&ports, port, i) {
        r = generate_metatype_port(out, name, simple_port_process,
            simple_port_type, port);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    composed_port.name = strdup(composed_port_name);
    SOL_NULL_CHECK_GOTO(composed_port.name, exit);
    r = generate_metatype_port(out, name, composed_port_process,
        composed_port_type, &composed_port);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = generate_metatype_init(out, name, &ports, &composed_port,
        packet_signature);
    SOL_INT_CHECK_GOTO(r, < 0, exit);
    r = generate_composed_get_port_function(out, composed_port_type, name,
        &composed_port);
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    SOL_VECTOR_FOREACH_IDX (&ports, port, i) {
        r = sol_buffer_append_printf(&ports_body,
            "   if (port == %u)\n"
            "      return &metatype_composed_%.*s_%s_port;\n",
            i, SOL_STR_SLICE_PRINT(name), port->name);
        SOL_INT_CHECK_GOTO(r, < 0, exit);
    }

    r = generate_metatype_get_ports(out, simple_port_type, name,
        sol_buffer_get_slice(&ports_body));
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    r = sol_buffer_append_printf(out,
        "static const struct sol_flow_node_type %.*s = {\n"
        "   .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,\n"
        "   .options_size = sizeof(struct sol_flow_node_options),\n"
        "   .data_size = %s,\n"
        "   .ports_out_count = %u,\n"
        "   .ports_in_count = %u,\n"
        "   .dispose_type = NULL,\n"
        "   .open = %s,\n"
        "   .close = %s,\n"
        "   .get_port_out = composed_metatype_%.*s_get_out_port,\n"
        "   .get_port_in = composed_metatype_%.*s_get_in_port,\n"
        "   .init_type = composed_metatype_%.*s_init,\n"
        "};\n",
        SOL_STR_SLICE_PRINT(name),
        data_size, ports_out, ports_in,
        open_func, close_func,
        SOL_STR_SLICE_PRINT(name),
        SOL_STR_SLICE_PRINT(name),
        SOL_STR_SLICE_PRINT(name));

exit:
    sol_buffer_fini(&ports_body);
    SOL_VECTOR_FOREACH_IDX (&ports, port, i) {
        free(port->name);
    }
    sol_vector_clear(&ports);
    free(composed_port.name);
    free(packet_signature);
    return r;
}

static void
clear_ports_vector(struct sol_vector *vector)
{
    struct sol_flow_metatype_port_description *port;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (vector, port, i) {
        free(port->name);
        free(port->type);
    }

    sol_vector_clear(vector);
}

static int
get_ports_description(const struct sol_str_slice contents,
    struct sol_vector *simple_ports, struct sol_vector *composed_ports,
    const char *composed_port_name)
{
    int r;
    const struct sol_flow_packet_type **types;
    const struct sol_flow_packet_type *composed_type;
    struct sol_flow_metatype_port_description *composed_port, *port;
    uint16_t i;

    SOL_NULL_CHECK(simple_ports, -EINVAL);
    SOL_NULL_CHECK(composed_ports, -EINVAL);

    sol_vector_init(composed_ports,
        sizeof(struct sol_flow_metatype_port_description));
    sol_vector_init(simple_ports,
        sizeof(struct sol_flow_metatype_port_description));

    r = get_ports_from_contents(contents, simple_ports, false);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    types = alloca((simple_ports->len + 1) *
        sizeof(struct sol_flow_packate_type *));

    SOL_VECTOR_FOREACH_IDX (simple_ports, port, i)
        types[i] = get_packet_type(sol_str_slice_from_str(port->type));

    types[i] = NULL;
    composed_type = sol_flow_packet_type_composed_new(types);
    SOL_NULL_CHECK_GOTO(composed_type, err_no_mem);

    composed_port = sol_vector_append(composed_ports);
    SOL_NULL_CHECK_GOTO(composed_port, err_no_mem);
    composed_port->name = strdup(composed_port_name);
    SOL_NULL_CHECK_GOTO(composed_port->name, err_no_mem);
    composed_port->array_size = 0;
    composed_port->idx = 0;
    composed_port->type = strdup(composed_type->name);
    SOL_NULL_CHECK_GOTO(composed_port->type, err_no_mem);

    return 0;
err_no_mem:
    r = -ENOMEM;
err_exit:
    clear_ports_vector(composed_ports);
    clear_ports_vector(simple_ports);
    return r;
}

int
composed_metatype_constructor_generate_code_start(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents)
{
    int r;

    SOL_NULL_CHECK(out, -EINVAL);

    r = generate_metatype_data(out);
    SOL_INT_CHECK(r, < 0, r);
    r = generate_metatype_close(out);
    SOL_INT_CHECK(r, < 0, r);
    r = generate_metatype_simple_process(out);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

int
composed_metatype_splitter_generate_code_start(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents)
{
    SOL_NULL_CHECK(out, -EINVAL);
    return generate_metatype_composed_process(out);
}

int
composed_metatype_constructor_generate_code_type(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents)
{
    return composed_metatype_generate_type_code(out, name, contents, false);
}

int
composed_metatype_splitter_generate_code_type(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents)
{
    return composed_metatype_generate_type_code(out, name, contents, true);
}

int
composed_metatype_splitter_generate_code_end(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents)
{
    return 0;
}

int
composed_metatype_constructor_generate_code_end(struct sol_buffer *out,
    const struct sol_str_slice name, const struct sol_str_slice contents)
{
    return 0;
}

int
composed_metatype_splitter_get_ports_description(
    const struct sol_str_slice contents, struct sol_vector *in,
    struct sol_vector *out)
{
    return get_ports_description(contents, out, in, "IN");
}

int
composed_metatype_constructor_get_ports_description(
    const struct sol_str_slice contents, struct sol_vector *in,
    struct sol_vector *out)
{
    return get_ports_description(contents, in, out, "OUT");
}
