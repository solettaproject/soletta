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
get_packet_type(const char *type)
{
    if (!strcmp(type, "int"))
        return SOL_FLOW_PACKET_TYPE_IRANGE;
    if (!strcmp(type, "float"))
        return SOL_FLOW_PACKET_TYPE_DRANGE;
    if (!strcmp(type, "string"))
        return SOL_FLOW_PACKET_TYPE_STRING;
    if (!strcmp(type, "boolean"))
        return SOL_FLOW_PACKET_TYPE_BOOLEAN;
    if (!strcmp(type, "byte"))
        return SOL_FLOW_PACKET_TYPE_BYTE;
    if (!strcmp(type, "blob"))
        return SOL_FLOW_PACKET_TYPE_BLOB;
    if (!strcmp(type, "rgb"))
        return SOL_FLOW_PACKET_TYPE_RGB;
    if (!strcmp(type, "location"))
        return SOL_FLOW_PACKET_TYPE_LOCATION;
    if (!strcmp(type, "timestamp"))
        return SOL_FLOW_PACKET_TYPE_TIMESTAMP;
    if (!strcmp(type, "direction-vector"))
        return SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR;
    if (!strcmp(type, "error"))
        return SOL_FLOW_PACKET_TYPE_ERROR;
    return NULL;
}

static int
simple_port_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct composed_node_data *cdata = data;
    struct sol_flow_packet *composed;
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

    composed = sol_flow_packet_new(cdata->composed_type, cdata->inputs);
    SOL_NULL_CHECK(composed, -ENOMEM);
    return sol_flow_send_packet(node, 0, composed);
}

static int
composed_port_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    uint16_t len, i;
    struct sol_flow_packet **children, *out_packet;

    r = sol_flow_packet_get_composed_members_len(
        sol_flow_packet_get_type(packet), &len);
    SOL_INT_CHECK(r, < 0, r);

    children = alloca(len * sizeof(struct sol_flow_packet *));

    r = sol_flow_packet_get(packet, children);
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
setup_simple_ports(struct sol_vector *in_ports, const struct sol_str_slice contents, bool is_input)
{
    struct sol_vector tokens;
    struct sol_str_slice *slice, pending_slice;
    struct composed_node_port_type *port_type;
    const struct sol_flow_packet_type *packet_type;
    struct sol_buffer buf;
    char *token, *name, *type;
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

    name = type = token = NULL;
    SOL_VECTOR_FOREACH_IDX (&tokens, slice, i) {
        token = strndup(slice->data, slice->len);
        if (!token) {
            r = -ENOMEM;
            SOL_ERR("Could not alloc memory for the token");
            goto err_exit;
        }

        if (sscanf(token, "%m[^(](%m[^)])", &name, &type) != 2) {
            r = -errno;
            SOL_ERR("Could not parse the arguments list");
            goto err_exit;
        }

        port_type = sol_vector_append(in_ports);
        if (!port_type) {
            r = -ENOMEM;
            SOL_ERR("Could not create a port");
            goto err_exit;
        }

        packet_type = get_packet_type(type);

        if (!packet_type) {
            r = -EINVAL;
            SOL_ERR("It's not possible to use %s as a port type.", type);
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
        free(type);
        free(token);
        name = type = token = NULL;
    }

    sol_vector_clear(&tokens);
    sol_buffer_fini(&buf);
    return 0;

err_exit:
    free(type);
    free(token);
    free(name);
    sol_vector_clear(&tokens);
    sol_buffer_fini(&buf);
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
        composed_port->type.in.packet_type = composed_type;
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
