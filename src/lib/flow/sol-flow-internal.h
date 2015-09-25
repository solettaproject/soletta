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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_sol_flow_log_domain
#endif

#include "sol-log-internal.h"
#include "sol-flow.h"
#include "sol-flow-metatype.h"
#include "sol-vector.h"
#include "sol-util.h"

extern struct sol_log_domain _sol_flow_log_domain;

#ifdef SOL_FLOW_INSPECTOR_ENABLED
#include "sol-flow-inspector.h"
extern const struct sol_flow_inspector *_sol_flow_inspector;
#endif

static inline void
inspector_did_open_node(const struct sol_flow_node *node, const struct sol_flow_node_options *options)
{
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (!_sol_flow_inspector || !_sol_flow_inspector->did_open_node)
        return;
    _sol_flow_inspector->did_open_node(_sol_flow_inspector, node, options);
#endif
}

static inline void
inspector_will_close_node(const struct sol_flow_node *node)
{
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (!_sol_flow_inspector || !_sol_flow_inspector->will_close_node)
        return;
    _sol_flow_inspector->will_close_node(_sol_flow_inspector, node);
#endif
}

static inline void
inspector_did_connect_port(const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id)
{
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (!_sol_flow_inspector || !_sol_flow_inspector->did_connect_port)
        return;
    _sol_flow_inspector->did_connect_port(_sol_flow_inspector, src_node, src_port, src_conn_id, dst_node, dst_port, dst_conn_id);
#endif
}

static inline void
inspector_will_disconnect_port(const struct sol_flow_node *src_node, uint16_t src_port, uint16_t src_conn_id, const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id)
{
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (!_sol_flow_inspector || !_sol_flow_inspector->will_disconnect_port)
        return;
    _sol_flow_inspector->will_disconnect_port(_sol_flow_inspector, src_node, src_port, src_conn_id, dst_node, dst_port, dst_conn_id);
#endif
}

/* Update libsoletta-gdb.py before changing the function and parameters below. */
static inline void
inspector_will_send_packet(const struct sol_flow_node *src_node, uint16_t src_port, const struct sol_flow_packet *packet)
{
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (!_sol_flow_inspector || !_sol_flow_inspector->will_send_packet)
        return;
    _sol_flow_inspector->will_send_packet(_sol_flow_inspector, src_node, src_port, packet);
#endif
}

/* Update libsoletta-gdb.py before changing the function and parameters below. */
static inline void
inspector_will_deliver_packet(const struct sol_flow_node *dst_node, uint16_t dst_port, uint16_t dst_conn_id, const struct sol_flow_packet *packet)
{
#ifdef SOL_FLOW_INSPECTOR_ENABLED
    if (!_sol_flow_inspector || !_sol_flow_inspector->will_deliver_packet)
        return;
    _sol_flow_inspector->will_deliver_packet(_sol_flow_inspector, dst_node, dst_port, dst_conn_id, packet);
#endif
}

struct sol_flow_node {
    const struct sol_flow_node_type *type;
    struct sol_flow_node *parent;
    char *id;

    /* Extra information set by the parent. */
    void *parent_data;

    void *data[];
};

/* Update libsoletta-gdb.py before changing the function and parameters below. */
int sol_flow_node_init(struct sol_flow_node *node, struct sol_flow_node *parent, const char *name, const struct sol_flow_node_type *type, const struct sol_flow_node_options *options);

/* Update libsoletta-gdb.py before changing the function and parameters below. */
void sol_flow_node_fini(struct sol_flow_node *node);

extern const struct sol_flow_node_options sol_flow_node_options_empty;

#define SOL_FLOW_NODE_CHECK(handle, ...)                 \
    do {                                                \
        if (!(handle)) {                                \
            SOL_WRN("" # handle " == NULL");             \
            return __VA_ARGS__;                         \
        }                                               \
        if (!(handle)->type) {                          \
            SOL_WRN("" # handle "->type == NULL");       \
            return __VA_ARGS__;                         \
        }                                               \
    } while (0)

#define SOL_FLOW_NODE_CHECK_GOTO(handle, label)          \
    do {                                                \
        if (!(handle)) {                                \
            SOL_WRN("" # handle " == NULL");             \
            goto label;                                 \
        }                                               \
        if (!(handle)->type) {                          \
            SOL_WRN("" # handle "->type == NULL");       \
            goto label;                                 \
        }                                               \
    } while (0)

#define SOL_FLOW_NODE_TYPE_CHECK(handle, _type, ...)     \
    do {                                                \
        if (!(handle)) {                                \
            SOL_WRN("" # handle " == NULL");             \
            return __VA_ARGS__;                         \
        }                                               \
        if ((handle)->type != _type) {                  \
            SOL_WRN("" # handle "->type != " # _type);   \
            return __VA_ARGS__;                         \
        }                                               \
    } while (0)

#define SOL_FLOW_NODE_TYPE_IS_CONTAINER_CHECK(handle, ...)               \
    do {                                                                \
        if (!(handle)->type) {                                          \
            SOL_WRN("" # handle "->type == NULL");                       \
            return __VA_ARGS__;                                         \
        }                                                               \
        if (((handle)->type->flags & SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER) == 0) { \
            SOL_WRN("" # handle "->type isn't a container type");        \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

#define SOL_FLOW_NODE_TYPE_IS_CONTAINER_CHECK_GOTO(handle, label)        \
    do {                                                                \
        if (!(handle)->type) {                                          \
            SOL_WRN("" # handle "->type == NULL");                       \
            goto label;                                                 \
        }                                                               \
        if (((handle)->type->flags & SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER) == 0) { \
            SOL_WRN("" # handle "->type isn't a container type");        \
            goto label;                                                 \
        }                                                               \
    } while (0)

#define SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, expected, ...)      \
    do {                                                                \
        SOL_NULL_CHECK(options, __VA_ARGS__);                            \
        if (((const struct sol_flow_node_options *)options)->sub_api != (expected)) { \
            SOL_WRN("" # options "(%p)->sub_api(%hu) != "                \
                "" # expected "(%hu)",                               \
                (options),                                           \
                ((const struct sol_flow_node_options *)options)->sub_api, \
                (expected));                                         \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

#define SOL_FLOW_NODE_OPTIONS_API_CHECK(options, expected, ...)          \
    do {                                                                \
        SOL_NULL_CHECK(options, __VA_ARGS__);                            \
        if (((const struct sol_flow_node_options *)options)->api_version != (expected)) { \
            SOL_WRN("Invalid " # options " %p API version(%hu), "        \
                "expected " # expected "(%hu)",                      \
                (options),                                           \
                ((const struct sol_flow_node_options *)options)->api_version, \
                (expected));                                         \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

#define SOL_FLOW_NODE_TYPE_DESCRIPTION_API_CHECK(description, expected, ...)  \
    do {                                                                \
        SOL_NULL_CHECK(description, __VA_ARGS__);                        \
        if (((const struct sol_flow_node_type_description *)description)->api_version != (expected)) { \
            SOL_WRN("Invalid " # description " %p API version(%lu), "    \
                "expected " # expected "(%hu)",                      \
                (description),                                       \
                ((const struct sol_flow_node_type_description *)description)->api_version, \
                (expected));                                         \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

#define SOL_FLOW_NODE_TYPE_API_CHECK(type, expected, ...)                  \
    do {                                                                  \
        SOL_NULL_CHECK(type, __VA_ARGS__);                                 \
        if (((const struct sol_flow_node_type *)type)->api_version != (expected)) { \
            SOL_WRN("Invalid " # type " %p API version(%hu), "             \
                "expected " # expected "(%hu)",                        \
                (type),                                                \
                ((const struct sol_flow_node_type *)type)->api_version, \
                (expected));                                           \
            return __VA_ARGS__;                                           \
        }                                                                 \
    } while (0)

#define SOL_FLOW_PORT_TYPE_OUT_API_CHECK(out, expected, ...)               \
    do {                                                                  \
        SOL_NULL_CHECK(out, __VA_ARGS__);                                  \
        if (((const struct sol_flow_port_type_out *)out)->api_version != (expected)) { \
            SOL_WRN("Invalid " # out " %p API version(%hu), "              \
                "expected " # expected "(%hu)",                        \
                (out),                                                 \
                ((const struct sol_flow_port_type_out *)out)->api_version, \
                (expected));                                           \
            return __VA_ARGS__;                                           \
        }                                                                 \
    } while (0)

#define SOL_FLOW_PORT_TYPE_IN_API_CHECK(in, expected, ...)                  \
    do {                                                                   \
        SOL_NULL_CHECK(in, __VA_ARGS__);                                    \
        if (((const struct sol_flow_port_type_in *)in)->api_version != (expected)) { \
            SOL_WRN("Invalid " # in " %p API version(%hu), "                \
                "expected " # expected "(%hu)",                         \
                (in),                                                   \
                ((const struct sol_flow_port_type_in *)in)->api_version, \
                (expected));                                            \
            return __VA_ARGS__;                                            \
        }                                                                  \
    } while (0)

#define SOL_FLOW_RESOLVER_API_CHECK(resolver, expected, ...)             \
    do {                                                                \
        SOL_NULL_CHECK(resolver, __VA_ARGS__);                           \
        if (((const struct sol_flow_resolver *)resolver)->api_version != (expected)) { \
            SOL_WRN("Invalid " # resolver " %p API version(%lu), "       \
                "expected " # expected "(%lu)",                     \
                (resolver),                                         \
                ((const struct sol_flow_resolver *)resolver)->api_version, \
                (expected));                                        \
            return __VA_ARGS__;                                         \
        }                                                               \
    } while (0)

struct sol_flow_builder;
int sol_flow_builder_add_node_taking_options(
    struct sol_flow_builder *builder,
    const char *name,
    const struct sol_flow_node_type *type,
    const struct sol_flow_node_options *options);

#ifdef ENABLE_DYNAMIC_MODULES
void sol_flow_modules_cache_shutdown(void);

sol_flow_metatype_create_type_func get_dynamic_create_type_func(const struct sol_str_slice name);
void loaded_metatype_cache_shutdown(void);
#endif
