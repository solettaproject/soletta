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

#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>

#include "sol-flow-internal.h"
#include "sol-flow-simplectype.h"

struct simplectype_port_in {
    struct sol_flow_port_type_in base;
    char *name;
};

struct simplectype_port_out {
    struct sol_flow_port_type_out base;
    char *name;
};

struct simplectype_type_data {
    int (*func)(struct sol_flow_node *node, const struct sol_flow_simplectype_event *ev, void *data);
    struct sol_vector ports_in, ports_out;
};

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static bool
simplectype_create_description_ports_in(const struct simplectype_type_data *type_data, struct sol_flow_node_type_description *desc)
{
    struct sol_flow_port_description **ports_in;
    struct simplectype_port_in *port;
    uint16_t idx;

    ports_in = calloc(type_data->ports_in.len + 1,
        sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK(ports_in, false);
    desc->ports_in = (const struct sol_flow_port_description *const *)ports_in;

    SOL_VECTOR_FOREACH_IDX (&(type_data->ports_in), port, idx) {
        struct sol_flow_port_description *pd;

        pd = calloc(1, sizeof(*pd));
        SOL_NULL_CHECK_GOTO(pd, error_port);

        pd->name = port->name;
        pd->array_size = 0;
        pd->base_port_idx = idx;
        ports_in[idx] = pd;
    }
    return true;

error_port:
    while (idx > 0) {
        idx--;
        free(ports_in[idx]);
    }
    free(ports_in);
    desc->ports_in = NULL;
    return false;
}

static bool
simplectype_create_description_ports_out(const struct simplectype_type_data *type_data, struct sol_flow_node_type_description *desc)
{
    struct sol_flow_port_description **ports_out;
    struct simplectype_port_out *port;
    uint16_t idx;

    ports_out = calloc(type_data->ports_out.len + 1,
        sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK(ports_out, false);
    desc->ports_out = (const struct sol_flow_port_description *const *)ports_out;

    SOL_VECTOR_FOREACH_IDX (&(type_data->ports_out), port, idx) {
        struct sol_flow_port_description *pd;

        pd = calloc(1, sizeof(*pd));
        SOL_NULL_CHECK_GOTO(pd, error_port);

        pd->name = port->name;
        ports_out[idx] = pd;
    }
    return true;

error_port:
    while (idx > 0) {
        idx--;
        free(ports_out[idx]);
    }
    free(ports_out);
    desc->ports_out = NULL;
    return false;
}
#endif

static bool
simplectype_create_description(struct sol_flow_node_type *type, const char *name)
{
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    struct sol_flow_node_type_description *desc;
    const struct simplectype_type_data *type_data = type->type_data;

    type->description = desc = calloc(1, sizeof(*desc));
    SOL_NULL_CHECK(type->description, false);

    desc->api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION;

    desc->name = strdup(name);
    SOL_NULL_CHECK_GOTO(desc->name, error);

    if (!simplectype_create_description_ports_in(type_data, desc))
        goto error;

    if (!simplectype_create_description_ports_out(type_data, desc))
        goto error;

    return true;

error:
    free((void *)desc->name);
    free((void *)desc->ports_in);
    free((void *)desc->ports_out);
    return false;

#else
    return true;
#endif
}

static void
simplectype_destroy_description(struct sol_flow_node_type *type)
{
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    const struct sol_flow_node_type_description *desc = type->description;

    SOL_NULL_CHECK(desc);

    if (desc->ports_in) {
        struct sol_flow_port_description **ports;

        ports = (struct sol_flow_port_description **)desc->ports_in;
        for (; *ports != NULL; ports++)
            free(*ports);
        free((void *)desc->ports_in);
    }

    if (desc->ports_out) {
        struct sol_flow_port_description **ports;

        ports = (struct sol_flow_port_description **)desc->ports_out;
        for (; *ports != NULL; ports++)
            free(*ports);
        free((void *)desc->ports_out);
    }

    free((void *)desc->name);
    free((void *)desc);
#endif
}

static void
simplectype_get_ports_counts(const struct sol_flow_node_type *type, uint16_t *ports_in_count, uint16_t *ports_out_count)
{
    const struct simplectype_type_data *type_data = type->type_data;

    if (ports_in_count)
        *ports_in_count = type_data->ports_in.len;
    if (ports_out_count)
        *ports_out_count = type_data->ports_out.len;
}

static const struct sol_flow_port_type_in *
simplectype_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct simplectype_type_data *type_data = type->type_data;

    return sol_vector_get(&(type_data->ports_in), port);
}

static const struct sol_flow_port_type_out *
simplectype_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct simplectype_type_data *type_data = type->type_data;

    return sol_vector_get(&(type_data->ports_out), port);
}

static int
simplectype_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_OPEN,
        .options = options,
    };

    return type_data->func(node, &ev, data);
}

static void
simplectype_close(struct sol_flow_node *node, void *data)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_CLOSE,
    };

    type_data->func(node, &ev, data);
}

static void
simplectype_dispose(struct sol_flow_node_type *type)
{
    struct simplectype_type_data *type_data = (void *)type->type_data;
    struct simplectype_port_in *port_in;
    struct simplectype_port_out *port_out;
    uint16_t idx;

    simplectype_destroy_description(type);

    SOL_VECTOR_FOREACH_IDX (&(type_data->ports_in), port_in, idx) {
        free(port_in->name);
    }
    sol_vector_clear(&(type_data->ports_in));

    SOL_VECTOR_FOREACH_IDX (&(type_data->ports_out), port_out, idx) {
        free(port_out->name);
    }
    sol_vector_clear(&(type_data->ports_out));

    free(type_data);
    free(type);
}

static int
simplectype_port_in_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct simplectype_port_in *p = sol_vector_get(&(type_data->ports_in), port);
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_PROCESS,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
        .packet = packet,
    };

    return type_data->func(node, &ev, data);
}

static int
simplectype_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct simplectype_port_in *p = sol_vector_get(&(type_data->ports_in), port);
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_CONNECT,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static int
simplectype_port_in_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct simplectype_port_in *p = sol_vector_get(&(type_data->ports_in), port);
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_IN_DISCONNECT,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static int
simplectype_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct simplectype_port_out *p = sol_vector_get(&(type_data->ports_out), port);
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_OUT_CONNECT,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static int
simplectype_port_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simplectype_type_data *type_data = node->type->type_data;
    struct simplectype_port_out *p = sol_vector_get(&(type_data->ports_out), port);
    struct sol_flow_simplectype_event ev = {
        .type = SOL_FLOW_SIMPLECTYPE_EVENT_TYPE_PORT_OUT_DISCONNECT,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

SOL_API struct sol_flow_node_type *
sol_flow_simplectype_new_full(const char *name, size_t private_data_size, int (*func)(struct sol_flow_node *node, const struct sol_flow_simplectype_event *ev, void *data), ...)
{
    struct sol_vector ports_in = SOL_VECTOR_INIT(struct simplectype_port_in);
    struct sol_vector ports_out = SOL_VECTOR_INIT(struct simplectype_port_out);
    struct sol_flow_node_type *type;
    struct simplectype_type_data *type_data;
    struct simplectype_port_in *port_in;
    struct simplectype_port_out *port_out;
    const char *port_name;
    uint16_t idx;
    va_list ap;
    bool ok = true;

    SOL_NULL_CHECK(func, NULL);

    va_start(ap, func);
    while ((port_name = va_arg(ap, const char *)) != NULL) {
        const struct sol_flow_packet_type *pt = va_arg(ap, void *);
        int direction = va_arg(ap, int);

        SOL_NULL_CHECK_GOTO(pt, error);
        SOL_INT_CHECK_GOTO(pt->api_version,
            != SOL_FLOW_PACKET_TYPE_API_VERSION, error);

        if (direction == SOL_FLOW_SIMPLECTYPE_PORT_TYPE_IN) {
            port_in = sol_vector_append(&ports_in);
            SOL_NULL_CHECK_GOTO(port_in, error);

            port_in->name = strdup(port_name);
            SOL_NULL_CHECK_GOTO(port_in->name, error);

            port_in->base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION;
            port_in->base.packet_type = pt;
            port_in->base.connect = simplectype_port_in_connect;
            port_in->base.disconnect = simplectype_port_in_disconnect;
            port_in->base.process = simplectype_port_in_process;
        } else if (direction == SOL_FLOW_SIMPLECTYPE_PORT_TYPE_OUT) {
            port_out = sol_vector_append(&ports_out);
            SOL_NULL_CHECK_GOTO(port_out, error);

            port_out->name = strdup(port_name);
            SOL_NULL_CHECK_GOTO(port_out->name, error);

            port_out->base.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION;
            port_out->base.packet_type = pt;
            port_out->base.connect = simplectype_port_out_connect;
            port_out->base.disconnect = simplectype_port_out_disconnect;
        } else {
            SOL_WRN("'%s' port '%s' (type %p %s) unexpected direction %d",
                name, port_name, pt, pt->name ? pt->name : "?", direction);
            goto error;
        }
    }
    va_end(ap);

    if (!ok)
        goto error;

    if (ports_in.len == 0 && ports_out.len == 0) {
        SOL_WRN("cannot create node type %s without ports for func=%p",
            name, func);
        return NULL;
    }

    type = calloc(1, sizeof(*type));
    SOL_NULL_CHECK_GOTO(type, error);

    type->api_version = SOL_FLOW_NODE_TYPE_API_VERSION;
    type->data_size = private_data_size;

    type->type_data = type_data = calloc(1, sizeof(*type_data));
    SOL_NULL_CHECK_GOTO(type->type_data, error_type_data);
    type_data->func = func;
    type_data->ports_in = ports_in;
    type_data->ports_out = ports_out;

    type->get_ports_counts = simplectype_get_ports_counts;
    type->get_port_in = simplectype_get_port_in;
    type->get_port_out = simplectype_get_port_out;
    type->open = simplectype_open;
    type->close = simplectype_close;
    type->dispose_type = simplectype_dispose;

    if (!simplectype_create_description(type, name))
        goto error_desc;

    return type;

error_desc:
    free(type_data);
error_type_data:
    free(type);
error:

    SOL_VECTOR_FOREACH_IDX (&ports_in, port_in, idx) {
        free(port_in->name);
    }
    sol_vector_clear(&ports_in);

    SOL_VECTOR_FOREACH_IDX (&ports_out, port_out, idx) {
        free(port_out->name);
    }
    sol_vector_clear(&ports_out);
    return NULL;
}

SOL_API uint16_t
sol_flow_simplectype_get_port_out_index(const struct sol_flow_node_type *type, const char *port_out_name)
{
    const struct simplectype_type_data *type_data;
    struct simplectype_port_out *port;
    uint16_t idx;

    SOL_NULL_CHECK(type, UINT16_MAX);
    SOL_NULL_CHECK(port_out_name, UINT16_MAX);

    type_data = type->type_data;
    SOL_VECTOR_FOREACH_IDX (&(type_data->ports_out), port, idx) {
        if (streq(port->name, port_out_name))
            return idx;
    }

    return UINT16_MAX;
}

SOL_API uint16_t
sol_flow_simplectype_get_port_in_index(const struct sol_flow_node_type *type, const char *port_in_name)
{
    const struct simplectype_type_data *type_data;
    struct simplectype_port_in *port;
    uint16_t idx;

    SOL_NULL_CHECK(type, UINT16_MAX);
    SOL_NULL_CHECK(port_in_name, UINT16_MAX);

    type_data = type->type_data;
    SOL_VECTOR_FOREACH_IDX (&(type_data->ports_in), port, idx) {
        if (streq(port->name, port_in_name))
            return idx;
    }

    return UINT16_MAX;
}

