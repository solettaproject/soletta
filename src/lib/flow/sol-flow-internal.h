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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_sol_flow_log_domain
#endif

#include "sol-log-internal.h"
#include "sol-flow.h"
#include "sol-flow-metatype.h"
#include "sol-vector.h"
#include "sol-util-internal.h"

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

#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, expected, ...) \
    do { \
        SOL_NULL_CHECK(options, __VA_ARGS__); \
        if (((const struct sol_flow_node_options *)options)->sub_api != (expected)) { \
            SOL_WRN("" # options "(%p)->sub_api(%" PRIu16 ") != " \
                "" # expected "(%" PRIu16 ")", \
                (options), \
                ((const struct sol_flow_node_options *)options)->sub_api, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_NODE_OPTIONS_API_CHECK(options, expected, ...) \
    do { \
        SOL_NULL_CHECK(options, __VA_ARGS__); \
        if (((const struct sol_flow_node_options *)options)->api_version != (expected)) { \
            SOL_WRN("Invalid " # options " %p API version(%" PRIu16 "), " \
                "expected " # expected "(%" PRIu16 ")", \
                (options), \
                ((const struct sol_flow_node_options *)options)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_NODE_TYPE_DESCRIPTION_API_CHECK(description, expected, ...) \
    do { \
        SOL_NULL_CHECK(description, __VA_ARGS__); \
        if (((const struct sol_flow_node_type_description *)description)->api_version != (expected)) { \
            SOL_WRN("Invalid " # description " %p API version(%" PRIu16 "), " \
                "expected " # expected "(%" PRIu16 ")", \
                (description), \
                ((const struct sol_flow_node_type_description *)description)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_NODE_TYPE_API_CHECK(type, expected, ...) \
    do { \
        SOL_NULL_CHECK(type, __VA_ARGS__); \
        if (((const struct sol_flow_node_type *)type)->api_version != (expected)) { \
            SOL_WRN("Invalid " # type " %p API version(%" PRIu16 "), " \
                "expected " # expected "(%" PRIu16 ")", \
                (type), \
                ((const struct sol_flow_node_type *)type)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_PORT_TYPE_OUT_API_CHECK(out, expected, ...) \
    do { \
        SOL_NULL_CHECK(out, __VA_ARGS__); \
        if (((const struct sol_flow_port_type_out *)out)->api_version != (expected)) { \
            SOL_WRN("Invalid " # out " %p API version(%" PRIu16 "), " \
                "expected " # expected "(%" PRIu16 ")", \
                (out), \
                ((const struct sol_flow_port_type_out *)out)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_PORT_TYPE_IN_API_CHECK(in, expected, ...) \
    do { \
        SOL_NULL_CHECK(in, __VA_ARGS__); \
        if (((const struct sol_flow_port_type_in *)in)->api_version != (expected)) { \
            SOL_WRN("Invalid " # in " %p API version(%" PRIu16 "), " \
                "expected " # expected "(%" PRIu16 ")", \
                (in), \
                ((const struct sol_flow_port_type_in *)in)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)

#define SOL_FLOW_RESOLVER_API_CHECK(resolver, expected, ...) \
    do { \
        SOL_NULL_CHECK(resolver, __VA_ARGS__); \
        if (((const struct sol_flow_resolver *)resolver)->api_version != (expected)) { \
            SOL_WRN("Invalid " # resolver " %p API version(%" PRIu16 "), " \
                "expected " # expected "(%" PRIu16 ")", \
                (resolver), \
                ((const struct sol_flow_resolver *)resolver)->api_version, \
                (expected)); \
            return __VA_ARGS__; \
        } \
    } while (0)
#else
#define SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, expected, ...)
#define SOL_FLOW_NODE_OPTIONS_API_CHECK(options, expected, ...)
#define SOL_FLOW_NODE_TYPE_DESCRIPTION_API_CHECK(description, expected, ...)
#define SOL_FLOW_NODE_TYPE_API_CHECK(type, expected, ...)
#define SOL_FLOW_PORT_TYPE_OUT_API_CHECK(out, expected, ...)
#define SOL_FLOW_PORT_TYPE_IN_API_CHECK(in, expected, ...)
#define SOL_FLOW_RESOLVER_API_CHECK(resolver, expected, ...)
#endif

struct sol_flow_builder;
int sol_flow_builder_add_node_taking_options(
    struct sol_flow_builder *builder,
    const char *name,
    const struct sol_flow_node_type *type,
    const struct sol_flow_node_options *options);

#ifdef ENABLE_DYNAMIC_MODULES
const struct sol_flow_metatype *get_dynamic_metatype(const struct sol_str_slice name);
void loaded_metatype_cache_shutdown(void);
#endif

void sol_flow_packet_type_composed_shutdown(void);
