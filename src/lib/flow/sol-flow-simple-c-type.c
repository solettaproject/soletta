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

#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>

#include "sol-flow-internal.h"
#include "sol-flow-simple-c-type.h"

struct simple_c_type_port_in {
    struct sol_flow_port_type_in base;
    char *name;
};

struct simple_c_type_port_out {
    struct sol_flow_port_type_out base;
    char *name;
};

struct simple_c_type_type_data {
    int (*func)(struct sol_flow_node *node, const struct sol_flow_simple_c_type_event *ev, void *data);
    struct sol_vector ports_in, ports_out;
};

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static bool
simple_c_type_create_description_ports_in(const struct simple_c_type_type_data *type_data, struct sol_flow_node_type_description *desc)
{
    struct sol_flow_port_description **ports_in;
    struct simple_c_type_port_in *port;
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
simple_c_type_create_description_ports_out(const struct simple_c_type_type_data *type_data, struct sol_flow_node_type_description *desc)
{
    struct sol_flow_port_description **ports_out;
    struct simple_c_type_port_out *port;
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
simple_c_type_create_description(struct sol_flow_node_type *type, const char *name)
{
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    struct sol_flow_node_type_description *desc;
    const struct simple_c_type_type_data *type_data = type->type_data;

    type->description = desc = calloc(1, sizeof(*desc));
    SOL_NULL_CHECK(type->description, false);

    SOL_SET_API_VERSION(desc->api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION; )

    desc->name = strdup(name);
    SOL_NULL_CHECK_GOTO(desc->name, error);

    if (!simple_c_type_create_description_ports_in(type_data, desc))
        goto error;

    if (!simple_c_type_create_description_ports_out(type_data, desc))
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
simple_c_type_destroy_description(struct sol_flow_node_type *type)
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

static const struct sol_flow_port_type_in *
simple_c_type_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct simple_c_type_type_data *type_data = type->type_data;

    return sol_vector_get(&(type_data->ports_in), port);
}

static const struct sol_flow_port_type_out *
simple_c_type_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct simple_c_type_type_data *type_data = type->type_data;

    return sol_vector_get(&(type_data->ports_out), port);
}

static int
simple_c_type_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_OPEN,
        .options = options,
    };

    return type_data->func(node, &ev, data);
}

static void
simple_c_type_close(struct sol_flow_node *node, void *data)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CLOSE,
    };

    type_data->func(node, &ev, data);
}

static void
simple_c_type_dispose(struct sol_flow_node_type *type)
{
    struct simple_c_type_type_data *type_data = (void *)type->type_data;
    struct simple_c_type_port_in *port_in;
    struct simple_c_type_port_out *port_out;
    uint16_t idx;

    simple_c_type_destroy_description(type);

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
simple_c_type_process_port_in(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct simple_c_type_port_in *p = sol_vector_get(&(type_data->ports_in), port);
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_PROCESS_PORT_IN,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
        .packet = packet,
    };

    return type_data->func(node, &ev, data);
}

static int
simple_c_type_connect_port_in(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct simple_c_type_port_in *p = sol_vector_get(&(type_data->ports_in), port);
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CONNECT_PORT_IN,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static int
simple_c_type_disconnect_port_in(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct simple_c_type_port_in *p = sol_vector_get(&(type_data->ports_in), port);
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_DISCONNECT_PORT_IN,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static int
simple_c_type_connect_port_out(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct simple_c_type_port_out *p = sol_vector_get(&(type_data->ports_out), port);
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_CONNECT_PORT_OUT,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static int
simple_c_type_disconnect_port_out(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct simple_c_type_type_data *type_data = node->type->type_data;
    struct simple_c_type_port_out *p = sol_vector_get(&(type_data->ports_out), port);
    struct sol_flow_simple_c_type_event ev = {
        .type = SOL_FLOW_SIMPLE_C_TYPE_EVENT_TYPE_DISCONNECT_PORT_OUT,
        .port = port,
        .conn_id = conn_id,
        .port_name = p->name,
    };

    return type_data->func(node, &ev, data);
}

static struct sol_flow_node_type *
simple_c_type_new_full_inner(const char *name, size_t private_data_size, uint16_t options_size,
    int (*func)(struct sol_flow_node *node, const struct sol_flow_simple_c_type_event *ev, void *data),
    va_list args)
{
    struct sol_vector ports_in = SOL_VECTOR_INIT(struct simple_c_type_port_in);
    struct sol_vector ports_out = SOL_VECTOR_INIT(struct simple_c_type_port_out);
    struct sol_flow_node_type *type;
    struct simple_c_type_type_data *type_data;
    struct simple_c_type_port_in *port_in;
    struct simple_c_type_port_out *port_out;
    const char *port_name;
    uint16_t idx;
    bool ok = true;

    while ((port_name = va_arg(args, const char *)) != NULL) {
        const struct sol_flow_packet_type *pt = va_arg(args, void *);
        int direction = va_arg(args, int);

        SOL_NULL_CHECK_GOTO(pt, error);
#ifndef SOL_NO_API_VERSION
        SOL_INT_CHECK_GOTO(pt->api_version,
            != SOL_FLOW_PACKET_TYPE_API_VERSION, error);
#endif

        if (direction == SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_IN) {
            port_in = sol_vector_append(&ports_in);
            SOL_NULL_CHECK_GOTO(port_in, error);

            port_in->name = strdup(port_name);
            SOL_NULL_CHECK_GOTO(port_in->name, error);

            SOL_SET_API_VERSION(port_in->base.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION; )
            port_in->base.packet_type = pt;
            port_in->base.connect = simple_c_type_connect_port_in;
            port_in->base.disconnect = simple_c_type_disconnect_port_in;
            port_in->base.process = simple_c_type_process_port_in;
        } else if (direction == SOL_FLOW_SIMPLE_C_TYPE_PORT_TYPE_OUT) {
            port_out = sol_vector_append(&ports_out);
            SOL_NULL_CHECK_GOTO(port_out, error);

            port_out->name = strdup(port_name);
            SOL_NULL_CHECK_GOTO(port_out->name, error);

            SOL_SET_API_VERSION(port_out->base.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION; )
            port_out->base.packet_type = pt;
            port_out->base.connect = simple_c_type_connect_port_out;
            port_out->base.disconnect = simple_c_type_disconnect_port_out;
        } else {
            SOL_WRN("'%s' port '%s' (type %p %s) unexpected direction %d",
                name, port_name, pt, pt->name ? pt->name : "?", direction);
            ok = false;
            break;
        }
    }

    if (!ok)
        goto error;

    if (ports_in.len == 0 && ports_out.len == 0) {
        SOL_WRN("cannot create node type %s without ports for func=%p",
            name, func);
        return NULL;
    }

    type = calloc(1, sizeof(*type));
    SOL_NULL_CHECK_GOTO(type, error);

    SOL_SET_API_VERSION(type->api_version = SOL_FLOW_NODE_TYPE_API_VERSION; )
    type->data_size = private_data_size;

    type->type_data = type_data = calloc(1, sizeof(*type_data));
    SOL_NULL_CHECK_GOTO(type->type_data, error_type_data);
    type_data->func = func;
    type_data->ports_in = ports_in;
    type_data->ports_out = ports_out;

    type->ports_in_count = ports_in.len;
    type->ports_out_count = ports_out.len;

    type->get_port_in = simple_c_type_get_port_in;
    type->get_port_out = simple_c_type_get_port_out;
    type->open = simple_c_type_open;
    type->close = simple_c_type_close;
    type->dispose_type = simple_c_type_dispose;
    type->options_size = options_size;

    if (!simple_c_type_create_description(type, name))
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

SOL_API struct sol_flow_node_type *
sol_flow_simple_c_type_new_full(const char *name, size_t private_data_size, uint16_t options_size,
    int (*func)(struct sol_flow_node *node, const struct sol_flow_simple_c_type_event *ev,
    void *data), ...)
{
    struct sol_flow_node_type *type;
    va_list ap;

    SOL_NULL_CHECK(func, NULL);
#ifndef SOL_NO_API_VERSION
    SOL_INT_CHECK(options_size, < sizeof(struct sol_flow_node_options), NULL);
#endif

    va_start(ap, func);
    type = simple_c_type_new_full_inner(name, private_data_size, options_size, func, ap);
    va_end(ap);

    return type;
}

SOL_API uint16_t
sol_flow_simple_c_type_get_port_out_index(const struct sol_flow_node_type *type, const char *port_out_name)
{
    const struct simple_c_type_type_data *type_data;
    struct simple_c_type_port_out *port;
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
sol_flow_simple_c_type_get_port_in_index(const struct sol_flow_node_type *type, const char *port_in_name)
{
    const struct simple_c_type_type_data *type_data;
    struct simple_c_type_port_in *port;
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

