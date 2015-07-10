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
#include <string.h>

#include "sol-arena.h"
#include "sol-flow-builder.h"
#include "sol-flow-internal.h"
#include "sol-flow-resolver.h"
#include "sol-flow-static.h"
#include "sol-str-table.h"

struct sol_flow_builder {
    struct sol_vector nodes;
    struct sol_ptr_vector conns;
    const struct sol_flow_resolver *resolver;

    struct sol_flow_static_node_spec *node_spec;
    struct sol_flow_static_conn_spec *conn_spec;
    struct sol_flow_node_type *node_type;

    struct sol_arena *str_arena;

    struct sol_vector exported_in;
    struct sol_vector exported_out;

    struct sol_ptr_vector ports_in_desc;
    struct sol_ptr_vector ports_out_desc;

    struct sol_vector options_description;
    size_t options_size;

    struct sol_flow_node_type_description type_desc;
};

struct sol_flow_builder_node_spec {
    struct sol_flow_static_node_spec spec;
    char *name;

    struct sol_vector exported_options;

    /* Whether builder owns the options for this node. */
    bool owns_opts;
};

struct sol_flow_builder_node_exported_option {
    uint16_t parent_offset, child_offset;
    uint16_t size;
    bool is_string;
};

struct sol_flow_builder_options {
    struct sol_flow_node_options base;
};

#define SOL_FLOW_BUILDER_OPTIONS_API_VERSION 1

static void
sol_flow_builder_init(struct sol_flow_builder *builder)
{
    sol_vector_init(&builder->nodes, sizeof(struct sol_flow_builder_node_spec));
    sol_ptr_vector_init(&builder->conns);
    sol_vector_init(&builder->exported_in, sizeof(struct sol_flow_static_port_spec));
    sol_vector_init(&builder->exported_out, sizeof(struct sol_flow_static_port_spec));
    sol_ptr_vector_init(&builder->ports_in_desc);
    sol_ptr_vector_init(&builder->ports_out_desc);
    sol_vector_init(&builder->options_description, sizeof(struct sol_flow_node_options_member_description));

    builder->str_arena = sol_arena_new();
    SOL_NULL_CHECK(builder->str_arena);

    sol_flow_builder_set_resolver(builder, NULL);

    builder->type_desc.api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION;
}

SOL_API struct sol_flow_builder *
sol_flow_builder_new(void)
{
    struct sol_flow_builder *builder;

    builder = calloc(1, sizeof(*builder));
    SOL_NULL_CHECK(builder, NULL);

    sol_flow_builder_init(builder);

    return builder;
}

SOL_API int
sol_flow_builder_del(struct sol_flow_builder *builder)
{
    struct sol_flow_builder_node_spec *builder_node_spec;
    struct sol_flow_static_conn_spec *conn_spec;
    struct sol_flow_port_description *port_desc;
    uint16_t i;

    SOL_NULL_CHECK(builder, -EBADR);

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->ports_in_desc, port_desc, i)
        free(port_desc);
    sol_ptr_vector_clear(&builder->ports_in_desc);

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->ports_out_desc, port_desc, i)
        free(port_desc);
    sol_ptr_vector_clear(&builder->ports_out_desc);

    sol_vector_clear(&builder->exported_in);
    sol_vector_clear(&builder->exported_out);

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->conns, conn_spec, i)
        free(conn_spec);
    sol_ptr_vector_clear(&builder->conns);

    SOL_VECTOR_FOREACH_IDX (&builder->nodes, builder_node_spec, i) {
        if (builder_node_spec->owns_opts && builder_node_spec->spec.opts)
            sol_flow_node_options_del(builder_node_spec->spec.type,
                (struct sol_flow_node_options *)builder_node_spec->spec.opts);
        sol_vector_clear(&builder_node_spec->exported_options);
    }
    sol_vector_clear(&builder->nodes);

    sol_vector_clear(&builder->options_description);

    free((void *)builder->type_desc.options);

    free(builder->node_spec);
    free(builder->conn_spec);

    if (builder->node_type)
        sol_flow_node_type_del(builder->node_type);

    sol_arena_del(builder->str_arena);

    free(builder);
    return 0;
}

SOL_API void
sol_flow_builder_set_resolver(struct sol_flow_builder *builder,
    const struct sol_flow_resolver *resolver)
{
    SOL_NULL_CHECK(builder);
    if (!resolver)
        resolver = sol_flow_get_default_resolver();
    builder->resolver = resolver;
}

static bool
set_type_description_symbols(struct sol_flow_builder *builder, const char *name)
{
    char *n;
    char buf[4096];
    int r;

    n = strdupa(name);
    for (char *p = n; *p; p++)
        *p = toupper(*p);

    r = snprintf(buf, sizeof(buf), "SOL_FLOW_NODE_TYPE_BUILDER_%s", n);
    if (r < 0 || r >= (int)sizeof(buf))
        return false;

    builder->type_desc.symbol = sol_arena_strdup(builder->str_arena, buf);
    SOL_NULL_CHECK(builder->type_desc.symbol, false);

    n = strdupa(name);
    for (char *p = n; *p; p++)
        *p = tolower(*p);

    r = snprintf(buf, sizeof(buf), "sol_flow_node_type_builder_%s_options", n);
    if (r < 0 || r >= (int)sizeof(buf))
        return false;

    builder->type_desc.options_symbol = sol_arena_strdup(builder->str_arena, buf);
    SOL_NULL_CHECK(builder->type_desc.options_symbol, false);

    return true;
}

SOL_API int
sol_flow_builder_set_type_description(struct sol_flow_builder *builder, const char *name, const char *category,
    const char *description, const char *author, const char *url, const char *license, const char *version)
{
    SOL_NULL_CHECK(builder, -EINVAL);

    if (builder->node_type) {
        SOL_WRN("Couldn't set builder node type description, node type created already");
        return -EEXIST;
    }

    if (strchr(name, ' ')) {
        SOL_WRN("Whitespaces are not allowed on builder type description name");
        return -EINVAL;
    }

    builder->type_desc.name = sol_arena_strdup(builder->str_arena, name);
    SOL_NULL_CHECK_GOTO(builder->type_desc.name, failure);

    builder->type_desc.category = sol_arena_strdup(builder->str_arena, category);
    SOL_NULL_CHECK_GOTO(builder->type_desc.category, failure);

    builder->type_desc.description = sol_arena_strdup(builder->str_arena, description);
    SOL_NULL_CHECK_GOTO(builder->type_desc.description, failure);

    builder->type_desc.author = sol_arena_strdup(builder->str_arena, author);
    SOL_NULL_CHECK_GOTO(builder->type_desc.author, failure);

    builder->type_desc.url = sol_arena_strdup(builder->str_arena, url);
    SOL_NULL_CHECK_GOTO(builder->type_desc.url, failure);

    builder->type_desc.license = sol_arena_strdup(builder->str_arena, license);
    SOL_NULL_CHECK_GOTO(builder->type_desc.license, failure);

    builder->type_desc.version = sol_arena_strdup(builder->str_arena, version);
    SOL_NULL_CHECK_GOTO(builder->type_desc.version, failure);

    if (!set_type_description_symbols(builder, name))
        goto failure;

    return 0;

failure:
    SOL_WRN("Couldn't set type description for builder");
    return -ENOMEM;
}

static uint16_t
find_port(const struct sol_flow_port_description *const *ports, const char *name, uint16_t *port_size)
{
    const struct sol_flow_port_description *const *itr;

    if (!ports)
        return UINT16_MAX;

    itr = ports;
    while (*itr) {
        if (!strcmp((*itr)->name, name)) {
            *port_size = (*itr)->array_size;
            return (*itr)->base_port_idx;
        }
        itr++;
    }

    return UINT16_MAX;
}

static uint16_t
find_port_in(const struct sol_flow_port_description *const *ports_in, const char *name, uint16_t *port_size)
{
    return find_port(ports_in, name, port_size);
}

static uint16_t
find_port_out(const struct sol_flow_port_description *const *ports_out, const char *name, uint16_t *port_size)
{
    if (!strcmp(name, SOL_FLOW_NODE_PORT_ERROR_NAME)) {
        *port_size = 0;
        return SOL_FLOW_NODE_PORT_ERROR;
    }
    return find_port(ports_out, name, port_size);
}

static bool
find_duplicated_port_names(const struct sol_flow_port_description *const *ports, bool output_port)
{
    const struct sol_flow_port_description *const *port;

    if (!ports)
        return false;

    port = ports;
    while (*port) {
        const struct sol_flow_port_description *const *itr = port + 1;
        const char *port_name = (*port)->name;
        if (!port_name) {
            SOL_ERR("Missing port name");
            return true;
        }
        if ((output_port) &&
            (!strcmp(SOL_FLOW_NODE_PORT_ERROR_NAME, port_name))) {
            SOL_ERR("Node not added, port has the same name of error port");
            return true;
        }
        while (*itr) {
            if (!(*itr)->name) {
                SOL_ERR("Node not added, missing port name");
                return true;
            }
            if (!strcmp(port_name, (*itr)->name)) {
                SOL_ERR("Node not added, port name %s is duplicated.", port_name);
                return true;
            }
            itr++;
        }
        port++;
    }

    return false;
}

SOL_API int
sol_flow_builder_add_node(struct sol_flow_builder *builder, const char *name, const struct sol_flow_node_type *type, const struct sol_flow_node_options *option)
{
    struct sol_flow_builder_node_spec *node_spec;
    char *node_name;
    uint16_t i;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(name, -EBADR);
    SOL_NULL_CHECK(type, -EBADR);

    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, -EINVAL);

    if (builder->node_type) {
        SOL_WRN("Node not added, node type created already");
        return -EEXIST;
    }

    /* check if name is unique, it'll be used for connections */
    SOL_VECTOR_FOREACH_IDX (&builder->nodes, node_spec, i)
        if (!strcmp(name, node_spec->name)) {
            SOL_WRN("Node not added, name %s already exists.", name);
            return -ENOTUNIQ;
        }

    /* check if port names are unique */
    if (type->description &&
        ((type->description->ports_in &&
        find_duplicated_port_names(type->description->ports_in, false)) ||
        (type->description->ports_out &&
        find_duplicated_port_names(type->description->ports_out, true))))
        return -EEXIST;

    node_name = sol_arena_strdup(builder->str_arena, name);
    if (!node_name)
        return -errno;

    node_spec = sol_vector_append(&builder->nodes);
    if (!node_spec) {
        free(node_name);
        return -ENOMEM;
    }

    node_spec->name = node_name;
    node_spec->owns_opts = false;
    node_spec->spec.name = node_name;
    node_spec->spec.type = type;
    node_spec->spec.opts = option;
    sol_vector_init(&node_spec->exported_options, sizeof(struct sol_flow_builder_node_exported_option));

    SOL_DBG("Node %s added: type=%p, opts=%p.", name, type, option);

    return 0;
}

static int
compare_conns(const void *data1, const void *data2)
{
    const struct sol_flow_static_conn_spec *conn_spec1 = data1,
    *conn_spec2 = data2;
    int retval = sol_util_int_compare(conn_spec1->src, conn_spec2->src);

    if (retval != 0)
        return retval;
    return sol_util_int_compare(conn_spec1->src_port, conn_spec2->src_port);
}

static int
conn_spec_add(struct sol_flow_builder *builder, uint16_t src, uint16_t dst, uint16_t src_port, uint16_t dst_port)
{
    struct sol_flow_static_conn_spec *conn_spec;

    conn_spec = malloc(sizeof(*conn_spec));
    if (!conn_spec)
        return -ENOMEM;

    conn_spec->src = src;
    conn_spec->dst = dst;
    conn_spec->src_port = src_port;
    conn_spec->dst_port = dst_port;

    if (sol_ptr_vector_insert_sorted(&builder->conns, conn_spec,
        compare_conns) < 0) {
        free(conn_spec);
        return -ENOMEM;
    }

    return 0;
}

static int
get_node(struct sol_flow_builder *builder, const char *node_name, uint16_t *out_index, struct sol_flow_static_node_spec **out_spec)
{
    struct sol_flow_builder_node_spec *builder_node_spec;
    uint16_t i;

    SOL_NULL_CHECK(node_name, -EBADR);
    SOL_NULL_CHECK(out_index, -EBADR);
    SOL_NULL_CHECK(out_spec, -EBADR);

    *out_index = UINT16_MAX;

    SOL_VECTOR_FOREACH_IDX (&builder->nodes, builder_node_spec, i) {
        if (streq(node_name, builder_node_spec->name)) {
            *out_index = i;
            *out_spec = &builder_node_spec->spec;
            break;
        }
    }

    if (*out_index == UINT16_MAX) {
        SOL_ERR("Failed to find node with name '%s'", node_name);
        return -EINVAL;
    }

    return 0;
}

static int
node_spec_add_options_reference(struct sol_flow_builder *builder, uint16_t node, const struct sol_flow_node_options_member_description *parent, const struct sol_flow_node_options_member_description *child)
{
    struct sol_flow_builder_node_spec *spec;
    struct sol_flow_builder_node_exported_option *ref;

    spec = sol_vector_get(&builder->nodes, node);
    ref = sol_vector_append(&spec->exported_options);
    SOL_NULL_CHECK(ref, -ENOMEM);
    ref->parent_offset = parent->offset;
    ref->child_offset = child->offset;
    ref->size = parent->size;
    ref->is_string = streq(parent->data_type, "string");

    return 0;
}

SOL_API int
sol_flow_builder_connect(struct sol_flow_builder *builder, const char *src_name, const char *src_port_name, int src_port_idx, const char *dst_name, const char *dst_port_name, int dst_port_idx)
{
    struct sol_flow_static_node_spec *src_node_spec, *dst_node_spec;
    int r;
    uint16_t src, dst, src_port, dst_port, psize;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(src_port_name, -EBADR);
    SOL_NULL_CHECK(dst_port_name, -EBADR);

    if (builder->node_type) {
        SOL_ERR("Failed to connect, node type created already");
        return -EEXIST;
    }

    r = get_node(builder, src_name, &src, &src_node_spec);
    if (r < 0)
        return r;

    r = get_node(builder, dst_name, &dst, &dst_node_spec);
    if (r < 0)
        return r;

    src_port = find_port_out(src_node_spec->type->description->ports_out,
        src_port_name, &psize);

    if (src_port == UINT16_MAX) {
        const struct sol_flow_port_description *const *itr;

        SOL_DBG("Failed to find output port '%s' in source node '%s' "
            "of type (%s), valid output ports are",
            src_port_name, src_name,
            src_node_spec->type->description->name);

        for (itr = src_node_spec->type->description->ports_out; itr && *itr; itr++)
            SOL_DBG("- '%s'", (*itr)->name);

        return -EINVAL;
    }

    if (!psize && src_port_idx != -1) {
        SOL_ERR("Failed to connect, given index '%d', but port '%s' of node '%s' "
            "is not an array port", src_port_idx, src_port_name, src_name);
        return -EINVAL;
    } else if (psize > 0 && src_port_idx == -1) {
        SOL_ERR("Failed to connect, port '%s' of node '%s' is an array port, "
            "but no index was given", src_port_name, src_name);
        return -EINVAL;
    } else if (src_port_idx > psize) {
        SOL_ERR("Failed to connect, index '%d' of port '%s' from node '%s' is "
            "out of bounds (array size = %d).", src_port_idx, src_port_name,
            src_name, psize);
        return -ERANGE;
    }

    if (src_port_idx != -1)
        src_port += src_port_idx;

    dst_port = find_port_in(dst_node_spec->type->description->ports_in,
        dst_port_name, &psize);
    if (dst_port == UINT16_MAX) {
        const struct sol_flow_port_description *const *itr;

        SOL_DBG("Failed to find input port '%s' in destination node '%s' "
            "of type (%s), valid input ports are",
            dst_port_name, dst_name,
            dst_node_spec->type->description->name);

        for (itr = dst_node_spec->type->description->ports_in; itr && *itr; itr++)
            SOL_DBG("- '%s'", (*itr)->name);

        return -EINVAL;
    }

    if (!psize && dst_port_idx != -1) {
        SOL_ERR("Failed to connect, given index '%d', but port '%s' of node '%s' "
            "is not an array port", dst_port_idx, dst_port_name, dst_name);
        return -EINVAL;
    } else if (psize > 0 && dst_port_idx == -1) {
        SOL_ERR("Failed to connect, port '%s' of node '%s' is an array port, "
            "but no index was given", dst_port_name, dst_name);
        return -EINVAL;
    } else if (dst_port_idx > psize) {
        SOL_ERR("Failed to connect, index '%d' of port '%s' from node '%s' is "
            "out of bounds (array size = %d).", dst_port_idx, dst_port_name,
            dst_name, psize);
        return -ERANGE;
    }

    if (dst_port_idx != -1)
        dst_port += dst_port_idx;

    return conn_spec_add(builder, src, dst, src_port, dst_port);
}

SOL_API int
sol_flow_builder_connect_by_index(struct sol_flow_builder *builder, const char *src_name, uint16_t src_port_index, const char *dst_name, uint16_t dst_port_index)
{
    struct sol_flow_static_node_spec *src_node_spec, *dst_node_spec;
    int r;
    uint16_t src, dst, ports_in_count, ports_out_count;

    SOL_NULL_CHECK(builder, -EBADR);

    if (builder->node_type) {
        SOL_WRN("Failed to connect, node type created already");
        return -EEXIST;
    }

    r = get_node(builder, src_name, &src, &src_node_spec);
    if (r < 0)
        return r;

    r = get_node(builder, dst_name, &dst, &dst_node_spec);
    if (r < 0)
        return r;

    /* check if port index is inside ports range */
    src_node_spec->type->get_ports_counts(src_node_spec->type, NULL,
        &ports_out_count);
    if (src_port_index >= ports_out_count) {
        SOL_WRN("Output port index %d out of ports range (count = %d).",
            src_port_index, ports_out_count);
        return -EINVAL;
    }

    dst_node_spec->type->get_ports_counts(dst_node_spec->type, &ports_in_count,
        NULL);
    if (dst_port_index >= ports_in_count) {
        SOL_WRN("Input port index %d out of ports range (count = %d).",
            src_port_index, ports_in_count);
        return -EINVAL;
    }

    return conn_spec_add(builder, src, dst, src_port_index, dst_port_index);
}

static struct sol_flow_static_node_spec *
get_node_spec(const struct sol_flow_builder *builder)
{
    struct sol_flow_static_node_spec *node_spec, *ret_spec;
    uint16_t i = 0;

    SOL_NULL_CHECK(builder, NULL);

    ret_spec = calloc(builder->nodes.len + 1,
        sizeof(struct sol_flow_static_node_spec));
    SOL_NULL_CHECK(ret_spec, NULL);

    SOL_VECTOR_FOREACH_IDX (&builder->nodes, node_spec, i) {
        ret_spec[i].type = node_spec->type;
        ret_spec[i].name = node_spec->name;
        ret_spec[i].opts = node_spec->opts;
    }

    return ret_spec;
}

static struct sol_flow_static_conn_spec *
get_conn_spec(const struct sol_flow_builder *builder)
{
    struct sol_flow_static_conn_spec *conn_spec, *ret_spec;
    uint16_t i;

    SOL_NULL_CHECK(builder, NULL);

    ret_spec = calloc(sol_ptr_vector_get_len(&builder->conns) + 1,
        sizeof(struct sol_flow_static_conn_spec));
    SOL_NULL_CHECK(ret_spec, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->conns, conn_spec, i) {
        ret_spec[i].src = conn_spec->src;
        ret_spec[i].src_port = conn_spec->src_port;
        ret_spec[i].dst = conn_spec->dst;
        ret_spec[i].dst_port = conn_spec->dst_port;
    }

    /* Add sentinel */
    ret_spec[i].src = UINT16_MAX;

    return ret_spec;
}

static struct sol_flow_node_options_description *
get_options_description(struct sol_flow_builder *builder)
{
    struct sol_flow_node_options_description *opts;
    struct sol_flow_node_options_member_description *member;
    bool required = false;

    opts = calloc(1, sizeof(*opts));
    SOL_NULL_CHECK(opts, NULL);

    opts->sub_api = SOL_FLOW_BUILDER_OPTIONS_API_VERSION;

    for (member = builder->options_description.data; member->name; member++) {
        if (member->required) {
            required = true;
            break;
        }
    }

    opts->members = builder->options_description.data;
    opts->required = required;

    return opts;
}

static struct sol_flow_node_type_description *
get_type_description(struct sol_flow_builder *builder)
{
    int r;

    /* Type description structures for ports expect NULL terminated
     * arrays. */
    if (sol_ptr_vector_get_len(&builder->ports_in_desc) > 0) {
        r = sol_ptr_vector_append(&builder->ports_in_desc, NULL);
        SOL_INT_CHECK(r, < 0, NULL);
        builder->type_desc.ports_in = builder->ports_in_desc.base.data;
    }
    if (sol_ptr_vector_get_len(&builder->ports_out_desc) > 0) {
        r = sol_ptr_vector_append(&builder->ports_out_desc, NULL);
        SOL_INT_CHECK(r, < 0, NULL);
        builder->type_desc.ports_out = builder->ports_out_desc.base.data;
    }

    if (builder->options_description.len > 0) {
        struct sol_flow_node_options_member_description *sentinel;
        sentinel = sol_vector_append(&builder->options_description);
        SOL_NULL_CHECK(sentinel, NULL);
        memset(sentinel, 0, sizeof(*sentinel));
        builder->type_desc.options = get_options_description(builder);
        SOL_NULL_CHECK_GOTO(builder->type_desc.options, opt_desc_error);
    }

    return &builder->type_desc;
opt_desc_error:
    sol_vector_del(&builder->options_description, builder->options_description.len - 1);
    return NULL;
}

static int
get_exported_ports(
    struct sol_flow_builder *builder,
    struct sol_flow_static_port_spec **exported_in,
    struct sol_flow_static_port_spec **exported_out)
{
    struct sol_flow_static_port_spec *spec, guard = SOL_FLOW_STATIC_PORT_SPEC_GUARD;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(exported_in, -EBADR);
    SOL_NULL_CHECK(exported_out, -EBADR);

    *exported_in = NULL;
    *exported_out = NULL;

    if (builder->exported_in.len > 0) {
        spec = sol_vector_append(&builder->exported_in);
        if (!spec)
            return -ENOMEM;
        *spec = guard;
        *exported_in = builder->exported_in.data;
    }

    if (builder->exported_out.len > 0) {
        spec = sol_vector_append(&builder->exported_out);
        if (!spec)
            return -ENOMEM;
        *spec = guard;
        *exported_out = builder->exported_out.data;
    }

    return 0;
}

static void
builder_type_free_options(const struct sol_flow_node_type *type, struct sol_flow_node_options *options)
{
    struct sol_flow_builder_options *opts = (struct sol_flow_builder_options *)options;
    const struct sol_flow_node_options_member_description *member;

    SOL_FLOW_NODE_OPTIONS_API_CHECK(options, SOL_FLOW_NODE_OPTIONS_API_VERSION);
    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_BUILDER_OPTIONS_API_VERSION);

    for (member = type->description->options->members; member->name; member++) {
        char **ptr;

        if (!streq(member->data_type, "string"))
            continue;

        ptr = (char **)((char *)opts + member->offset);
        free(*ptr);
    }

    free(opts);
}

static struct sol_flow_node_options *
builder_type_new_options(const struct sol_flow_node_type *type, const struct sol_flow_node_options *copy_from)
{
    struct sol_flow_builder *builder = (struct sol_flow_builder *)type->type_data;
    struct sol_flow_builder_options *opts;
    const struct sol_flow_node_options_member_description *member;

    SOL_NULL_CHECK(builder, NULL);

    if (copy_from) {
        SOL_FLOW_NODE_OPTIONS_API_CHECK(copy_from, SOL_FLOW_NODE_OPTIONS_API_VERSION, NULL);
        SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(copy_from, SOL_FLOW_BUILDER_OPTIONS_API_VERSION, NULL);
    }

    opts = calloc(1, builder->options_size);
    SOL_NULL_CHECK(opts, NULL);

    opts->base.api_version = SOL_FLOW_NODE_OPTIONS_API_VERSION;
    opts->base.sub_api = SOL_FLOW_BUILDER_OPTIONS_API_VERSION;

    for (member = type->description->options->members; member->name; member++) {
        char *dst;
        const char **src;
        bool is_string;

        is_string = streq(member->data_type, "string");

        dst = (char *)opts + member->offset;
        if (copy_from)
            src = (const char **)((char *)copy_from + member->offset);
        else
            src = (const char **)&member->defvalue.ptr;

        if (is_string) {
            char **s = (char **)dst;
            free(*s);
            if (*src) {
                if (!(*s = strdup(*src))) {
                    builder_type_free_options(type, &opts->base);
                    return NULL;
                }
            } else
                *s = NULL;
        } else
            memcpy(dst, src, member->size);
    }

    return &opts->base;
}

static int
builder_child_opts_set(const struct sol_flow_node_type *type, uint16_t child, const struct sol_flow_node_options *options, struct sol_flow_node_options *child_opts)
{
    struct sol_flow_builder_options *opts = (struct sol_flow_builder_options *)options;
    struct sol_flow_builder_node_spec *node_spec;
    struct sol_flow_builder_node_exported_option *opt_ref;
    const struct sol_flow_builder *builder = type->type_data;
    uint16_t i;

    SOL_FLOW_NODE_OPTIONS_API_CHECK(options, SOL_FLOW_NODE_OPTIONS_API_VERSION, -EINVAL);
    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_BUILDER_OPTIONS_API_VERSION, -EINVAL);
    SOL_FLOW_NODE_OPTIONS_API_CHECK(child_opts, SOL_FLOW_NODE_OPTIONS_API_VERSION, -EINVAL);

    node_spec = sol_vector_get(&builder->nodes, child);
    SOL_NULL_CHECK(node_spec, -ECHILD);

    SOL_VECTOR_FOREACH_IDX (&node_spec->exported_options, opt_ref, i) {
        const char **src;
        char *dst;

        src = (const char **)((char *)opts + opt_ref->parent_offset);
        dst = (char *)child_opts + opt_ref->child_offset;

        if (opt_ref->is_string) {
            char **s = (char **)dst;
            free(*s);
            if (*src) {
                if (!(*s = strdup(*src)))
                    return -ENOMEM;
            } else
                *s = NULL;
        } else
            memcpy(dst, src, opt_ref->size);
    }

    return 0;
}

SOL_API struct sol_flow_node_type *
sol_flow_builder_get_node_type(struct sol_flow_builder *builder)
{
    struct sol_flow_static_port_spec *exported_in, *exported_out;
    struct sol_flow_node_type_description *desc;
    int err;

    struct sol_flow_static_spec spec = {
        .api_version = SOL_FLOW_STATIC_API_VERSION,
    };


    SOL_NULL_CHECK(builder, NULL);

    if (builder->node_type)
        return builder->node_type;

    builder->conn_spec = get_conn_spec(builder);
    if (!builder->conn_spec) {
        SOL_WRN("Failed to get connections spec");
        return NULL;
    }

    builder->node_spec = get_node_spec(builder);
    if (!builder->node_spec) {
        SOL_WRN("Failed to get node spec");
        goto error_node_spec;
    }

    desc = get_type_description(builder);
    if (!desc) {
        SOL_WRN("Failed to construct type description");
        goto error_desc;
    }

    err = get_exported_ports(builder, &exported_in, &exported_out);
    if (err < 0)
        goto error_exported;

    spec.nodes = builder->node_spec;
    spec.conns = builder->conn_spec;
    spec.exported_in = exported_in;
    spec.exported_out = exported_out;
    if (desc->options)
        spec.child_opts_set = builder_child_opts_set;

    builder->node_type = sol_flow_static_new_type(&spec);
    if (!builder->node_type) {
        SOL_WRN("Failed to create new type");
        goto error_node_type;
    }

    if (desc->options) {
        builder->node_type->new_options = builder_type_new_options;
        builder->node_type->free_options = builder_type_free_options;
    }

    builder->node_type->type_data = builder;
    builder->node_type->description = desc;

    SOL_DBG("Node type %p created", builder->node_type);

    return builder->node_type;

error_node_type:
    free(builder->node_spec);
    builder->node_spec = NULL;
error_exported:
error_desc:
error_node_spec:
    free(builder->conn_spec);
    builder->conn_spec = NULL;
    return NULL;
}

static const char **
strv_join(const char *const *first, const char *const *second)
{
    unsigned int first_count = 0, second_count = 0;
    unsigned int first_size, second_size;
    const char *const *p;
    const char **joined;

    for (p = first; *p; p++, first_count++) ;
    for (p = second; *p; p++, second_count++) ;

    joined = calloc(first_count + second_count + 1, sizeof(char *));
    if (!joined)
        return NULL;

    first_size = first_count * sizeof(char *);
    second_size = second_count * sizeof(char *);
    memcpy(joined, first, first_size);
    memcpy(joined + first_count, second, second_size);

    return joined;
}

static int
find_type(const struct sol_flow_resolver *resolver, const char *id, const char *const *extra_strv,
    struct sol_flow_node_type const **type, struct sol_flow_node_options const **opts)
{
    const struct sol_flow_node_type *tmp_type;
    struct sol_flow_node_options *tmp_opts;
    const char **opts_strv, **joined_strv = NULL;
    const char *const *strv;
    int err;

    err = sol_flow_resolve(resolver, id, &tmp_type, &opts_strv);
    if (err)
        return -ENOENT;

    if (!tmp_type->new_options) {
        *type = tmp_type;
        return 0;
    }

    if (extra_strv && opts_strv) {
        joined_strv = strv_join(opts_strv, extra_strv);
        if (!joined_strv) {
            err = -ENOMEM;
            goto end;
        }
        strv = joined_strv;
    } else if (extra_strv) {
        strv = extra_strv;
    } else {
        strv = opts_strv;
    }

    tmp_opts = sol_flow_node_options_new_from_strv(tmp_type, strv);
    if (!tmp_opts) {
        err = -EINVAL;
        goto end;
    }

    *type = tmp_type;
    *opts = tmp_opts;
    err = 0;

end:
    free(joined_strv);
    sol_flow_node_options_strv_del((char **)opts_strv);
    return err;
}

static void
mark_own_opts(struct sol_flow_builder *builder, uint16_t node_idx)
{
    struct sol_flow_builder_node_spec *spec;

    spec = sol_vector_get(&builder->nodes, node_idx);
    SOL_NULL_CHECK(spec);
    spec->owns_opts = true;
}

SOL_API int
sol_flow_builder_add_node_by_type(struct sol_flow_builder *builder, const char *name, const char *type_name, const char *const *options_strv)
{
    const struct sol_flow_node_options *opts = NULL;
    const struct sol_flow_node_type *node_type = NULL;
    const struct sol_flow_resolver *builtins_resolver;
    int r;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(name, -EBADR);
    SOL_NULL_CHECK(type_name, -EBADR);

    builtins_resolver = sol_flow_get_builtins_resolver();

    /* Ensure that we'll always find builtin types regardless of the
     * resolver used. */
    if (builtins_resolver != builder->resolver)
        find_type(builtins_resolver, type_name, options_strv, &node_type, &opts);

    if (!node_type) {
        r = find_type(builder->resolver, type_name, options_strv, &node_type, &opts);
        if (r < 0)
            return r;
    }

    r = sol_flow_builder_add_node(builder, name, node_type, opts);
    if (r < 0) {
        sol_flow_node_options_del(node_type, (struct sol_flow_node_options *)opts);
    } else {
        mark_own_opts(builder, builder->nodes.len - 1);
    }

    return r;
}

static int
export_port(struct sol_flow_builder *builder, uint16_t node, uint16_t port,
    int psize, const char *exported_name, struct sol_vector *exported_vector,
    struct sol_ptr_vector *desc_vector)
{
    struct sol_flow_port_description *port_desc;
    struct sol_flow_static_port_spec *port_spec;
    char *name;
    int i = 0, r;
    uint16_t desc_len, base_port_idx = 0;

    name = sol_arena_strdup(builder->str_arena, exported_name);
    SOL_NULL_CHECK_GOTO(name, error_name);

    desc_len = sol_ptr_vector_get_len(desc_vector);
    if (desc_len) {
        port_desc = sol_ptr_vector_get(desc_vector, desc_len - 1);
        SOL_NULL_CHECK_GOTO(port_desc, error_desc);
        base_port_idx = port_desc->base_port_idx + ((port_desc->array_size) ? : 1);
    }

    port_desc = calloc(1, sizeof(struct sol_flow_port_description));
    SOL_NULL_CHECK_GOTO(port_desc, error_desc);
    port_desc->name = name;
    port_desc->array_size = psize;
    port_desc->base_port_idx = base_port_idx;

    r = sol_ptr_vector_append(desc_vector, port_desc);
    SOL_INT_CHECK_GOTO(r, < 0, error_desc_append);

    do {
        port_spec = sol_vector_append(exported_vector);
        SOL_NULL_CHECK_GOTO(port_spec, error_export);

        port_spec->node = node;
        port_spec->port = port++;
        i++;
    } while (i < psize);

    return 0;

error_export:
    for (; i > 0; i--)
        sol_vector_del(exported_vector, exported_vector->len - 1);
    sol_ptr_vector_del(desc_vector, sol_ptr_vector_get_len(desc_vector) - 1);

error_desc_append:
    free(port_desc);

error_desc:
    free(name);

error_name:
    return -ENOMEM;
}

SOL_API int
sol_flow_builder_export_in_port(struct sol_flow_builder *builder, const char *node_name,
    const char *port_name, int port_idx, const char *exported_name)
{
    struct sol_flow_static_node_spec *node_spec;
    uint16_t node, port, psize;
    int r;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(node_name, -EBADR);
    SOL_NULL_CHECK(port_name, -EBADR);
    SOL_NULL_CHECK(exported_name, -EBADR);

    if (builder->node_type) {
        SOL_ERR("Failed to export input port, node type created already");
        return -EEXIST;
    }

    r = get_node(builder, node_name, &node, &node_spec);
    if (r < 0) {
        SOL_ERR("Failed to find node '%s' to export input port", node_name);
        return -EINVAL;
    }

    port = find_port_in(node_spec->type->description->ports_in, port_name, &psize);
    if (port == UINT16_MAX) {
        SOL_ERR("Failed to find input port '%s' of node '%s' to export",
            port_name, node_name);
        return -EINVAL;
    }

    if (port_idx != -1 && psize == 0) {
        SOL_ERR("Failed to export input port '%s', indicated index '%d' for "
            "source port '%s', but it's not an array port",
            exported_name, port_idx, port_name);
        return -EINVAL;
    } else if (port_idx > psize) {
        SOL_ERR("Failed to export input port '%s', index '%d' is out of range "
            "(port '%s' is of size '%d')", exported_name, port_idx,
            port_name, psize);
        return -EINVAL;
    }

    if (port_idx != -1) {
        port += port_idx;
        psize = 0;
    }

    r = export_port(builder, node, port, psize, exported_name,
        &builder->exported_in, &builder->ports_in_desc);
    if (r < 0) {
        SOL_ERR("Failed to export input port '%s' of node '%s' with exported name '%s': %s",
            port_name, node_name, exported_name, sol_util_strerrora(-r));
        return r;
    }

    return 0;
}

SOL_API int
sol_flow_builder_export_out_port(struct sol_flow_builder *builder, const char *node_name,
    const char *port_name, int port_idx, const char *exported_name)
{
    struct sol_flow_static_node_spec *node_spec;
    uint16_t node, port, psize;
    int r;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(node_name, -EBADR);
    SOL_NULL_CHECK(port_name, -EBADR);
    SOL_NULL_CHECK(exported_name, -EBADR);

    if (builder->node_type) {
        SOL_ERR("Failed to export output port, node type created already");
        return -EEXIST;
    }

    r = get_node(builder, node_name, &node, &node_spec);
    if (r < 0) {
        SOL_ERR("Failed to find node '%s' to export output port", node_name);
        return -EINVAL;
    }

    port = find_port_out(node_spec->type->description->ports_out, port_name, &psize);
    if (port == UINT16_MAX) {
        SOL_ERR("Failed to find output port '%s' of node '%s' to export",
            port_name, node_name);
        return -EINVAL;
    }

    if (port_idx != -1 && psize == 0) {
        SOL_ERR("Failed to export output port '%s', indicated index '%d' for "
            "source port '%s', but it's not an array port",
            exported_name, port_idx, port_name);
        return -EINVAL;
    } else if (port_idx > psize) {
        SOL_ERR("Failed to export output port '%s', index '%d' is out of range "
            "(port '%s' is of size '%d')", exported_name, port_idx,
            port_name, psize);
        return -EINVAL;
    }

    if (port_idx != -1) {
        port += port_idx;
        psize = 0;
    }

    r = export_port(builder, node, port, psize, exported_name,
        &builder->exported_out, &builder->ports_out_desc);
    if (r < 0) {
        SOL_ERR("Failed to export output port '%s' of node '%s' with exported name '%s': %s",
            port_name, node_name, exported_name, sol_util_strerrora(-r));
        return r;
    }

    return 0;
}

static size_t
get_member_alignment(const struct sol_flow_node_options_member_description *member)
{
    struct sol_str_slice t;
    static const struct sol_str_table alignments[] = {
        SOL_STR_TABLE_ITEM("boolean", __alignof__(member->defvalue.b)),
        SOL_STR_TABLE_ITEM("byte", __alignof__(member->defvalue.byte)),
        SOL_STR_TABLE_ITEM("float", __alignof__(member->defvalue.f)),
        SOL_STR_TABLE_ITEM("int", __alignof__(member->defvalue.i)),
        SOL_STR_TABLE_ITEM("rgb", __alignof__(member->defvalue.rgb)),
        SOL_STR_TABLE_ITEM("string", __alignof__(member->defvalue.s)),
    };

    t = SOL_STR_SLICE_STR(member->data_type, strlen(member->data_type));
    return sol_str_table_lookup_fallback(alignments, t, __alignof__(void *));
}

SOL_API int
sol_flow_builder_export_option(struct sol_flow_builder *builder, const char *node_name, const char *option_name, const char *exported_name)
{
    struct sol_flow_static_node_spec *node_spec;
    const struct sol_flow_node_options_member_description *opt;
    struct sol_flow_node_options_member_description *exported_opt;
    size_t member_alignment, padding;
    uint16_t node;
    int r;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(node_name, -EBADR);
    SOL_NULL_CHECK(option_name, -EBADR);
    SOL_NULL_CHECK(exported_name, -EBADR);

    if (builder->node_type) {
        SOL_ERR("Failed to export output port, node type created already");
        return -EEXIST;
    }

    r = get_node(builder, node_name, &node, &node_spec);
    if (r < 0) {
        SOL_ERR("Failed to find node '%s' to export option member", node_name);
        return -EINVAL;
    }

    if (!node_spec->type->description->options || !node_spec->type->description->options->members) {
        SOL_ERR("Failed to export option member for node '%s', node type has no options", node_name);
        return -EINVAL;
    }

    for (opt = node_spec->type->description->options->members; opt->name; opt++) {
        if (streq(opt->name, option_name))
            break;
    }

    if (!opt->name) {
        SOL_ERR("Failed to find option '%s' from node '%s'", option_name, node_name);
        return -EINVAL;
    }

    exported_opt = sol_vector_append(&builder->options_description);
    if (!exported_opt) {
        SOL_ERR("Failed to export option '%s' from node '%s'", option_name, node_name);
        return -ENOMEM;
    }

    memset(exported_opt, 0, sizeof(*exported_opt));
    exported_opt->name = sol_arena_strdup(builder->str_arena, exported_name);
    exported_opt->data_type = opt->data_type;
    /* Since we can't instantiate a sub-node without its required options
     * available, we will always have a default for the exported one, which
     * means there's no point in making them required */
    exported_opt->required = false;
    exported_opt->size = opt->size;
    if (node_spec->opts) {
        /* The node has options, use whatever we have there as the default
         * for the exported one.
         * Since the sub-nodes options is owned by the builder, it's enough to
         * just reference it here without copying it.
         */
        const char *node_opt = (char *)node_spec->opts + opt->offset;
        memcpy(&exported_opt->defvalue, node_opt, exported_opt->size);
    } else
        exported_opt->defvalue = opt->defvalue;

    if (!builder->options_size)
        builder->options_size = sizeof(struct sol_flow_builder_options);

    member_alignment = get_member_alignment(opt);
    padding = builder->options_size % member_alignment;
    exported_opt->offset = builder->options_size + padding;

    builder->options_size += exported_opt->size + padding;
    r = node_spec_add_options_reference(builder, node, exported_opt, opt);
    if (r < 0) {
        sol_vector_del(&builder->options_description, builder->options_description.len - 1);
        SOL_ERR("Failed to export option '%s' from node '%s'", option_name, node_name);
        return r;
    }

    return 0;
}
