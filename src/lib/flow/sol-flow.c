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
#include <stdlib.h>
#include <time.h>

#include "sol-flow-internal.h"
#include "sol-flow-resolver.h"
#include "sol-util.h"

SOL_LOG_INTERNAL_DECLARE(_sol_flow_log_domain, "flow");

int sol_flow_init(void);
void sol_flow_shutdown(void);

int
sol_flow_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);
    return 0;
}

void
sol_flow_shutdown(void)
{
#ifdef ENABLE_DYNAMIC_MODULES
    loaded_metatype_cache_shutdown();
#endif
}

#ifdef SOL_FLOW_INSPECTOR_ENABLED
const struct sol_flow_inspector *_sol_flow_inspector;

SOL_API bool
sol_flow_set_inspector(const struct sol_flow_inspector *inspector)
{
    if (inspector) {
        if (inspector->api_version != SOL_FLOW_INSPECTOR_API_VERSION) {
            SOL_WRN("inspector(%p)->api_version(%lu) != "
                "SOL_FLOW_INSPECTOR_API_VERSION(%lu)",
                inspector, inspector->api_version,
                SOL_FLOW_INSPECTOR_API_VERSION);
            return false;
        }
    }
    _sol_flow_inspector = inspector;
    return true;
}
#endif

SOL_API void *
sol_flow_node_get_private_data(const struct sol_flow_node *node)
{
    SOL_FLOW_NODE_CHECK(node, NULL);
    return (void *)node->data;
}

SOL_API const char *
sol_flow_node_get_id(const struct sol_flow_node *node)
{
    SOL_FLOW_NODE_CHECK(node, NULL);
    return node->id;
}

SOL_API const struct sol_flow_node *
sol_flow_node_get_parent(const struct sol_flow_node *node)
{
    SOL_FLOW_NODE_CHECK(node, NULL);
    return node->parent;
}

/* Update libsoletta-gdb.py before changing the function and parameters below. */
int
sol_flow_node_init(struct sol_flow_node *node, struct sol_flow_node *parent, const char *name, const struct sol_flow_node_type *type, const struct sol_flow_node_options *options)
{
    struct sol_flow_node_container_type *parent_type = NULL;

    SOL_NULL_CHECK(type, -EINVAL);
    SOL_NULL_CHECK(node, -EINVAL);

    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, -EINVAL);

    SOL_FLOW_NODE_OPTIONS_API_CHECK(options, SOL_FLOW_NODE_OPTIONS_API_VERSION, -EINVAL);

    if (type->init_type)
        type->init_type();

    node->type = type;

    if (parent) {
        SOL_FLOW_NODE_TYPE_IS_CONTAINER_CHECK(parent, -EINVAL);
        node->parent = parent;

        parent_type = (struct sol_flow_node_container_type *)parent->type;
        if (parent_type->add)
            parent_type->add(parent, node);
    }

    if (name)
        node->id = strdup(name);

    if (type->open) {
        int r = type->open(node, node->type->data_size ? node->data : NULL, options);
        if (r < 0) {
            if (parent_type) {
                if (parent_type->remove)
                    parent_type->remove(parent, node);
            }
            SOL_WRN("failed to create node of type=%p: %s",
                type, sol_util_strerrora(-r));
            free(node->id);
            node->id = NULL;
            return r;
        }
    }

    inspector_did_open_node(node, options);
    return 0;
}

const struct sol_flow_node_options sol_flow_node_options_empty = {
    .api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION,
    .sub_api = 0,
};

SOL_API struct sol_flow_node *
sol_flow_node_new(struct sol_flow_node *parent, const char *id, const struct sol_flow_node_type *type, const struct sol_flow_node_options *options)
{
    struct sol_flow_node *node;
    int err;

    SOL_NULL_CHECK(type, NULL);
    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, NULL);

    node = calloc(1, sizeof(*node) + type->data_size);
    SOL_NULL_CHECK(node, NULL);

    if (!options)
        options = type->default_options;
    if (!options)
        options = &sol_flow_node_options_empty;

    err = sol_flow_node_init(node, parent, id, type, options);
    if (err < 0) {
        free(node);
        node = NULL;
        errno = -err;
    }

    return node;
}

/* Update libsoletta-gdb.py before changing the function and parameters below. */
void
sol_flow_node_fini(struct sol_flow_node *node)
{
    struct sol_flow_node *parent;

    SOL_FLOW_NODE_CHECK(node);

    inspector_will_close_node(node);

    if (node->type->close)
        node->type->close(node, node->type->data_size ? node->data : NULL);

    parent = node->parent;
    if (parent) {
        struct sol_flow_node_container_type *parent_type;
        parent_type = (struct sol_flow_node_container_type *)parent->type;
        if (parent_type->remove)
            parent_type->remove(parent, node);
    }

    /* since it enters on sol_flow_node_init(), we must match here */
    free(node->id);
    /* Force SOL_FLOW_NODE_CHECK() to fail even if handle is still reacheable. */
    node->type = NULL;
}

SOL_API void
sol_flow_node_del(struct sol_flow_node *node)
{
    SOL_FLOW_NODE_CHECK(node);
    sol_flow_node_fini(node);
    free(node);
}

SOL_API const struct sol_flow_node_type *
sol_flow_node_get_type(const struct sol_flow_node *node)
{
    SOL_FLOW_NODE_CHECK(node, NULL);
    return node->type;
}

SOL_API int
sol_flow_send_packet(struct sol_flow_node *src, uint16_t src_port, struct sol_flow_packet *packet)
{
    struct sol_flow_node *parent;
    struct sol_flow_node_container_type *parent_type;
    int ret;

    SOL_FLOW_NODE_CHECK_GOTO(src, err);
    parent = src->parent;

    if (!parent) {
        sol_flow_packet_del(packet);
        return 0;
    }

    inspector_will_send_packet(src, src_port, packet);

    /* TODO: consider caching send pointer into parent's node data to
     * reduce indirection. */
    SOL_FLOW_NODE_TYPE_IS_CONTAINER_CHECK_GOTO(parent, err);
    parent_type = (struct sol_flow_node_container_type *)parent->type;

    ret = parent_type->send(parent, src, src_port, packet);
    if (ret != 0)
        sol_flow_packet_del(packet);
    return ret;

err:
    sol_flow_packet_del(packet);
    return -EINVAL;
}

#define SOL_FLOW_SEND_PACKET(_type) \
    do { \
        struct sol_flow_packet *out_packet; \
 \
        out_packet = sol_flow_packet_new_ ## _type(value); \
        SOL_NULL_CHECK(out_packet, -ENOMEM); \
 \
        return sol_flow_send_packet(src, src_port, out_packet); \
    } while (0)

SOL_API int
sol_flow_send_boolean_packet(struct sol_flow_node *src, uint16_t src_port, unsigned char value)
{
    SOL_FLOW_SEND_PACKET(boolean);
}

SOL_API int
sol_flow_send_blob_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value)
{
    SOL_FLOW_SEND_PACKET(blob);
}

SOL_API int
sol_flow_send_json_object_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value)
{
    SOL_FLOW_SEND_PACKET(json_object);
}

SOL_API int
sol_flow_send_json_array_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value)
{
    SOL_FLOW_SEND_PACKET(json_array);
}

SOL_API int
sol_flow_send_byte_packet(struct sol_flow_node *src, uint16_t src_port, unsigned char value)
{
    SOL_FLOW_SEND_PACKET(byte);
}

SOL_API int
sol_flow_send_drange_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_drange *value)
{
    SOL_FLOW_SEND_PACKET(drange);
}

SOL_API int
sol_flow_send_drange_value_packet(struct sol_flow_node *src, uint16_t src_port, double value)
{
    SOL_FLOW_SEND_PACKET(drange_value);
}

SOL_API int
sol_flow_send_rgb_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_rgb *value)
{
    SOL_FLOW_SEND_PACKET(rgb);
}

SOL_API int
sol_flow_send_rgb_components_packet(struct sol_flow_node *src, uint16_t src_port, uint32_t red, uint32_t green, uint32_t blue)
{
    struct sol_flow_packet *out_packet;

    out_packet = sol_flow_packet_new_rgb_components(red, green, blue);
    SOL_NULL_CHECK(out_packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, out_packet);
}

SOL_API int
sol_flow_send_direction_vector_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_direction_vector *value)
{
    SOL_FLOW_SEND_PACKET(direction_vector);
}

SOL_API int
sol_flow_send_direction_vector_components_packet(struct sol_flow_node *src, uint16_t src_port, double x, double y, double z)
{
    struct sol_flow_packet *out_packet;

    out_packet = sol_flow_packet_new_direction_vector_components(x, y, z);
    SOL_NULL_CHECK(out_packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, out_packet);
}

SOL_API int
sol_flow_send_location_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_location *value)
{
    SOL_FLOW_SEND_PACKET(location);
}

SOL_API int
sol_flow_send_timestamp_packet(struct sol_flow_node *src, uint16_t src_port, const struct timespec *value)
{
    SOL_FLOW_SEND_PACKET(timestamp);
}

SOL_API int
sol_flow_send_irange_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_irange *value)
{
    SOL_FLOW_SEND_PACKET(irange);
}

SOL_API int
sol_flow_send_irange_value_packet(struct sol_flow_node *src, uint16_t src_port, int32_t value)
{
    SOL_FLOW_SEND_PACKET(irange_value);
}

SOL_API int
sol_flow_send_string_packet(struct sol_flow_node *src, uint16_t src_port, const char *value)
{
    SOL_FLOW_SEND_PACKET(string);
}

SOL_API int
sol_flow_send_string_slice_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_str_slice value)
{
    SOL_FLOW_SEND_PACKET(string_slice);
}

#undef SOL_FLOW_SEND_PACKET

SOL_API int
sol_flow_send_string_take_packet(struct sol_flow_node *src, uint16_t src_port, char *value)
{
    struct sol_flow_packet *string_packet;

    string_packet = sol_flow_packet_new_string_take(value);
    SOL_NULL_CHECK(string_packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, string_packet);
}

SOL_API int
sol_flow_send_empty_packet(struct sol_flow_node *src, uint16_t src_port)
{
    struct sol_flow_packet *out_packet;

    out_packet = sol_flow_packet_new_empty();
    SOL_NULL_CHECK(out_packet, -ENOMEM);

    return sol_flow_send_packet(src, src_port, out_packet);
}

SOL_API int
sol_flow_send_error_packet(struct sol_flow_node *src, int code, const char *msg_fmt, ...)
{
    struct sol_flow_packet *packet;
    va_list args;
    char *msg = NULL;

    if (msg_fmt) {
        int r;
        va_start(args, msg_fmt);
        r = vasprintf(&msg, msg_fmt, args);
        if (r < 0) {
            SOL_WRN("Failed to alloc error msg");
            msg = NULL;
        }
        va_end(args);
    }

    packet = sol_flow_packet_new_error(code, msg);
    free(msg);
    SOL_NULL_CHECK(packet, -ENOMEM);

    return sol_flow_send_packet(src, SOL_FLOW_NODE_PORT_ERROR, packet);
}

SOL_API int
sol_flow_send_error_packet_errno(struct sol_flow_node *src, int code)
{
    if (code < 0)
        code = -code;
    return sol_flow_send_error_packet(src, code, "%s (errno %d)", sol_util_strerrora(code), code);
}

SOL_API int
sol_flow_send_error_packet_str(struct sol_flow_node *src, int code, const char *str)
{
    return sol_flow_send_error_packet(src, code, "%s", str);
}

static struct sol_flow_port_type_out port_error = {
    .api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION,
};

/* If this function or parameters change, update data/gdb/libsoletta-gdb.py. */
SOL_API const struct sol_flow_port_type_in *
sol_flow_node_type_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    return type->get_port_in(type, port);
}

/* If this function or parameters change, update data/gdb/libsoletta-gdb.py. */
SOL_API const struct sol_flow_port_type_out *
sol_flow_node_type_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    if (!port_error.packet_type)
        port_error.packet_type = SOL_FLOW_PACKET_TYPE_ERROR;

    if (port == SOL_FLOW_NODE_PORT_ERROR)
        return &port_error;

    return type->get_port_out(type, port);
}

SOL_API void
sol_flow_node_type_del(struct sol_flow_node_type *type)
{
    if (!type || !type->dispose_type)
        return;
    type->dispose_type(type);
}

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
#include "sol-flow-builtins-gen.h"

SOL_API void
sol_flow_foreach_builtin_node_type(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data)
{
#if (SOL_FLOW_BUILTIN_NODE_TYPE_COUNT > 0)
    const void *const *itr;
    unsigned int i;

    SOL_NULL_CHECK(cb);
    for (i = 0, itr = SOL_FLOW_BUILTIN_NODE_TYPE_ALL;
        i <  SOL_FLOW_BUILTIN_NODE_TYPE_COUNT;
        i++, itr++) {
        const struct sol_flow_node_type * (*type_func)(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data) = *itr;
        const struct sol_flow_node_type *t = type_func(cb, data);
        if (t && !cb((void *)data, t)) {
            break;
        }
    }
#endif
}

static const struct sol_flow_port_description *
sol_flow_node_type_get_port_description(const struct sol_flow_port_description *const *ports, uint16_t port)
{
    const struct sol_flow_port_description *const *desc;
    uint16_t next = 0;

    for (desc = ports; *desc; desc++) {
        next += (*desc)->array_size ? : 1;
        if (port < next)
            return *desc;
    }

    return NULL;
}

SOL_API const struct sol_flow_port_description *
sol_flow_node_get_port_in_description(const struct sol_flow_node_type *type, uint16_t port)
{
    SOL_NULL_CHECK(type, NULL);

    SOL_NULL_CHECK(type->description, NULL);
    SOL_NULL_CHECK(type->description->ports_in, NULL);
    return sol_flow_node_type_get_port_description(type->description->ports_in, port);
}

SOL_API const struct sol_flow_port_description *
sol_flow_node_get_port_out_description(const struct sol_flow_node_type *type, uint16_t port)
{
    SOL_NULL_CHECK(type, NULL);

    SOL_NULL_CHECK(type->description, NULL);
    SOL_NULL_CHECK(type->description->ports_out, NULL);
    return sol_flow_node_type_get_port_description(type->description->ports_out, port);
}
#endif
