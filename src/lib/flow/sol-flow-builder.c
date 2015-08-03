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

struct builder_type_data {
    struct sol_flow_static_spec spec;
    struct sol_flow_node_type_description desc;

    size_t options_size;
    struct node_extra *node_extras;

    struct sol_arena *arena;
};

struct sol_flow_builder {
    const struct sol_flow_resolver *resolver;

    /* Used to build the data structures that will compose the spec
     * that describes the type. */
    struct sol_vector nodes;
    struct sol_vector conns;
    struct sol_vector exported_in;
    struct sol_vector exported_out;

    struct sol_vector node_extras;

    /* Used to build the data structures that will compose the type
     * description. */
    struct sol_ptr_vector ports_in_desc;
    struct sol_ptr_vector ports_out_desc;
    struct sol_vector options_description;

    struct builder_type_data *type_data;

    struct sol_flow_node_type *node_type;
};

struct node_extra {
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
    sol_flow_builder_set_resolver(builder, NULL);

    sol_vector_init(&builder->nodes, sizeof(struct sol_flow_static_node_spec));
    sol_vector_init(&builder->conns, sizeof(struct sol_flow_static_conn_spec));
    sol_vector_init(&builder->exported_in, sizeof(struct sol_flow_static_port_spec));
    sol_vector_init(&builder->exported_out, sizeof(struct sol_flow_static_port_spec));

    sol_vector_init(&builder->node_extras, sizeof(struct node_extra));

    sol_ptr_vector_init(&builder->ports_in_desc);
    sol_ptr_vector_init(&builder->ports_out_desc);
    sol_vector_init(&builder->options_description,
        sizeof(struct sol_flow_node_options_member_description));
}

static int
sol_flow_builder_init_type_data(struct sol_flow_builder *builder)
{
    builder->type_data = calloc(1, sizeof(struct builder_type_data));
    SOL_NULL_CHECK_GOTO(builder->type_data, error_type_data);

    builder->type_data->arena = sol_arena_new();
    SOL_NULL_CHECK_GOTO(builder->type_data->arena, error_arena);

    builder->type_data->spec.api_version = SOL_FLOW_STATIC_API_VERSION;
    builder->type_data->desc.api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION;

    return 0;

error_arena:
    free(builder->type_data);
error_type_data:
    return -ENOMEM;
}

SOL_API struct sol_flow_builder *
sol_flow_builder_new(void)
{
    struct sol_flow_builder *builder;
    int err;

    builder = calloc(1, sizeof(*builder));
    SOL_NULL_CHECK(builder, NULL);

    err = sol_flow_builder_init_type_data(builder);
    SOL_INT_CHECK_GOTO(err, < 0, error_type_data);

    sol_flow_builder_init(builder);

    return builder;

error_type_data:
    free(builder);
    return NULL;
}

static void
dispose_node_options(struct builder_type_data *type_data)
{
    struct sol_flow_static_node_spec *node_spec;
    struct node_extra *node_extra;

    for (node_spec = (void *)type_data->spec.nodes, node_extra = type_data->node_extras;
        node_spec->type != NULL; node_spec++, node_extra++) {
        if (node_extra->owns_opts && node_spec->opts) {
            sol_flow_node_options_del(node_spec->type,
                (struct sol_flow_node_options *)node_spec->opts);
        }
        sol_vector_clear(&node_extra->exported_options);
    }
}

static void
dispose_description(struct sol_flow_node_type_description *desc)
{
    struct sol_flow_port_description **port_desc;

    if (desc->options) {
        free((void *)desc->options->members);
        free((void *)desc->options);
    }

    if (desc->ports_in) {
        port_desc = (struct sol_flow_port_description **)desc->ports_in;
        for (; *port_desc; port_desc++)
            free(*port_desc);
        free((void *)desc->ports_in);
    }

    if (desc->ports_out) {
        port_desc = (struct sol_flow_port_description **)desc->ports_out;
        for (; *port_desc; port_desc++)
            free(*port_desc);
        free((void *)desc->ports_out);
    }
}

static void
dispose_builder_type(const void *data)
{
    struct builder_type_data *type_data = (void *)data;
    struct sol_flow_static_spec *spec = &type_data->spec;
    struct sol_flow_node_type_description *desc = &type_data->desc;

    dispose_node_options(type_data);

    free((void *)spec->nodes);
    free((void *)spec->conns);
    free((void *)spec->exported_in);
    free((void *)spec->exported_out);

    dispose_description(desc);

    free(type_data->node_extras);

    sol_arena_del(type_data->arena);

    free(type_data);
}

SOL_API int
sol_flow_builder_del(struct sol_flow_builder *builder)
{
    struct node_extra *node_extra;
    struct sol_flow_port_description *port_desc;
    uint16_t i;

    SOL_NULL_CHECK(builder, -EINVAL);

    if (builder->node_type)
        goto end;

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->ports_in_desc, port_desc, i)
        free(port_desc);
    sol_ptr_vector_clear(&builder->ports_in_desc);

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->ports_out_desc, port_desc, i)
        free(port_desc);
    sol_ptr_vector_clear(&builder->ports_out_desc);
    sol_vector_clear(&builder->options_description);

    SOL_VECTOR_FOREACH_IDX (&builder->node_extras, node_extra, i) {
        if (node_extra->owns_opts) {
            struct sol_flow_static_node_spec *node_spec;
            node_spec = sol_vector_get(&builder->nodes, i);
            if (node_spec->opts) {
                sol_flow_node_options_del(node_spec->type,
                    (struct sol_flow_node_options *)node_spec->opts);
            }
        }
        sol_vector_clear(&node_extra->exported_options);
    }

    sol_vector_clear(&builder->node_extras);
    sol_vector_clear(&builder->conns);
    sol_vector_clear(&builder->nodes);
    sol_vector_clear(&builder->exported_in);
    sol_vector_clear(&builder->exported_out);

    sol_arena_del(builder->type_data->arena);
    free(builder->type_data);

end:
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

static struct sol_arena *
get_arena(struct sol_flow_builder *builder)
{
    return builder->type_data->arena;
}

static bool
set_type_description_symbols(struct sol_flow_builder *builder, const char *name)
{
    struct sol_flow_node_type_description *desc = &builder->type_data->desc;
    char *n;
    char buf[4096];
    int r;

    n = strdupa(name);
    for (char *p = n; *p; p++)
        *p = toupper(*p);

    r = snprintf(buf, sizeof(buf), "SOL_FLOW_NODE_TYPE_BUILDER_%s", n);
    if (r < 0 || r >= (int)sizeof(buf))
        return false;

    desc->symbol = sol_arena_strdup(get_arena(builder), buf);
    SOL_NULL_CHECK(desc->symbol, false);

    n = strdupa(name);
    for (char *p = n; *p; p++)
        *p = tolower(*p);

    r = snprintf(buf, sizeof(buf), "sol_flow_node_type_builder_%s_options", n);
    if (r < 0 || r >= (int)sizeof(buf))
        return false;

    desc->options_symbol = sol_arena_strdup(get_arena(builder), buf);
    SOL_NULL_CHECK(desc->options_symbol, false);

    return true;
}

SOL_API int
sol_flow_builder_set_type_description(struct sol_flow_builder *builder, const char *name, const char *category,
    const char *description, const char *author, const char *url, const char *license, const char *version)
{
    struct sol_flow_node_type_description *desc;

    SOL_NULL_CHECK(builder, -EINVAL);

    if (builder->node_type) {
        SOL_WRN("Couldn't set builder node type description, node type created already");
        return -EEXIST;
    }

    if (strchr(name, ' ')) {
        SOL_WRN("Whitespaces are not allowed on builder type description name");
        return -EINVAL;
    }

    desc = &builder->type_data->desc;

    desc->name = sol_arena_strdup(get_arena(builder), name);
    SOL_NULL_CHECK_GOTO(desc->name, failure);

    desc->category = sol_arena_strdup(get_arena(builder), category);
    SOL_NULL_CHECK_GOTO(desc->category, failure);

    desc->description = sol_arena_strdup(get_arena(builder), description);
    SOL_NULL_CHECK_GOTO(desc->description, failure);

    desc->author = sol_arena_strdup(get_arena(builder), author);
    SOL_NULL_CHECK_GOTO(desc->author, failure);

    desc->url = sol_arena_strdup(get_arena(builder), url);
    SOL_NULL_CHECK_GOTO(desc->url, failure);

    desc->license = sol_arena_strdup(get_arena(builder), license);
    SOL_NULL_CHECK_GOTO(desc->license, failure);

    desc->version = sol_arena_strdup(get_arena(builder), version);
    SOL_NULL_CHECK_GOTO(desc->version, failure);

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
    struct sol_flow_static_node_spec *node_spec;
    struct node_extra *node_extra;
    char *node_name;
    uint16_t i;

    SOL_NULL_CHECK(builder, -EINVAL);
    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(type, -EINVAL);

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

    node_name = sol_arena_strdup(get_arena(builder), name);
    if (!node_name)
        return -errno;

    node_spec = sol_vector_append(&builder->nodes);
    if (!node_spec) {
        free(node_name);
        return -ENOMEM;
    }

    node_extra = sol_vector_append(&builder->node_extras);
    if (!node_extra) {
        sol_vector_del(&builder->nodes, builder->nodes.len - 1);
        free(node_name);
        return -ENOMEM;
    }

    node_spec->name = node_name;
    node_spec->type = type;
    node_spec->opts = option;

    node_extra->owns_opts = false;
    sol_vector_init(&node_extra->exported_options, sizeof(struct sol_flow_builder_node_exported_option));

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

    conn_spec = sol_vector_append(&builder->conns);
    SOL_NULL_CHECK(conn_spec, -ENOMEM);

    conn_spec->src = src;
    conn_spec->dst = dst;
    conn_spec->src_port = src_port;
    conn_spec->dst_port = dst_port;

    return 0;
}

static int
get_node(struct sol_flow_builder *builder, const char *node_name, uint16_t *out_index, struct sol_flow_static_node_spec **out_spec)
{
    struct sol_flow_static_node_spec *node_spec;
    uint16_t i;

    SOL_NULL_CHECK(node_name, -EINVAL);
    SOL_NULL_CHECK(out_index, -EINVAL);
    SOL_NULL_CHECK(out_spec, -EINVAL);

    *out_index = UINT16_MAX;

    SOL_VECTOR_FOREACH_IDX (&builder->nodes, node_spec, i) {
        if (streq(node_name, node_spec->name)) {
            *out_index = i;
            *out_spec = node_spec;
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
    struct node_extra *node_extra;
    struct sol_flow_builder_node_exported_option *ref;

    node_extra = sol_vector_get(&builder->node_extras, node);
    ref = sol_vector_append(&node_extra->exported_options);
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

    SOL_NULL_CHECK(builder, -EINVAL);
    SOL_NULL_CHECK(src_port_name, -EINVAL);
    SOL_NULL_CHECK(dst_port_name, -EINVAL);

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

    SOL_NULL_CHECK(builder, -EINVAL);

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

static void
fill_options_description(struct sol_flow_builder *builder,
    struct sol_flow_node_options_description *opts)
{
    struct sol_flow_node_options_member_description *member;
    bool required = false;

    opts->sub_api = SOL_FLOW_BUILDER_OPTIONS_API_VERSION;

    for (member = builder->options_description.data; member->name; member++) {
        if (member->required) {
            required = true;
            break;
        }
    }

    opts->members = builder->options_description.data;
    opts->required = required;
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

    opts = calloc(1, builder->type_data->options_size);
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
    struct node_extra *node_extra;
    struct sol_flow_builder_node_exported_option *opt_ref;
    struct builder_type_data *type_data = (struct builder_type_data *)type->type_data;
    uint16_t i;

    SOL_FLOW_NODE_OPTIONS_API_CHECK(options, SOL_FLOW_NODE_OPTIONS_API_VERSION, -EINVAL);
    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_BUILDER_OPTIONS_API_VERSION, -EINVAL);
    SOL_FLOW_NODE_OPTIONS_API_CHECK(child_opts, SOL_FLOW_NODE_OPTIONS_API_VERSION, -EINVAL);

    node_extra = &type_data->node_extras[child];
    SOL_NULL_CHECK(node_extra, -ECHILD);

    SOL_VECTOR_FOREACH_IDX (&node_extra->exported_options, opt_ref, i) {
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

static void
vector_del_last(struct sol_vector *v)
{
    if (v->len == 0)
        return;
    sol_vector_del(v, v->len - 1);
}

static void
ptr_vector_del_last(struct sol_ptr_vector *v)
{
    const uint16_t len = sol_ptr_vector_get_len(v);

    if (len == 0)
        return;
    sol_ptr_vector_del(v, len - 1);
}

static int
add_guards(struct sol_flow_builder *builder)
{
    struct sol_flow_static_node_spec *node_spec;
    struct sol_flow_static_conn_spec *conn_spec;
    struct sol_flow_static_port_spec *port_spec;
    int err;

    struct sol_flow_static_node_spec node_guard = SOL_FLOW_STATIC_NODE_SPEC_GUARD;
    struct sol_flow_static_conn_spec conn_guard = SOL_FLOW_STATIC_CONN_SPEC_GUARD;
    struct sol_flow_static_port_spec port_guard = SOL_FLOW_STATIC_PORT_SPEC_GUARD;

    node_spec = sol_vector_append(&builder->nodes);
    SOL_NULL_CHECK_GOTO(node_spec, error_nodes);
    *node_spec = node_guard;

    conn_spec = sol_vector_append(&builder->conns);
    SOL_NULL_CHECK_GOTO(conn_spec, error_conns);
    *conn_spec = conn_guard;

    if (builder->exported_in.len > 0) {
        port_spec = sol_vector_append(&builder->exported_in);
        SOL_NULL_CHECK_GOTO(port_spec, error_exported_in);
        *port_spec = port_guard;

        err = sol_ptr_vector_append(&builder->ports_in_desc, NULL);
        SOL_INT_CHECK_GOTO(err, < 0, error_exported_in_desc);
    }

    if (builder->exported_out.len > 0) {
        port_spec = sol_vector_append(&builder->exported_out);
        SOL_NULL_CHECK_GOTO(port_spec, error_exported_out);
        *port_spec = port_guard;

        err = sol_ptr_vector_append(&builder->ports_out_desc, NULL);
        SOL_INT_CHECK_GOTO(err, < 0, error_exported_out_desc);
    }

    if (builder->options_description.len > 0) {
        struct sol_flow_node_options_member_description *opt_guard;
        opt_guard = sol_vector_append(&builder->options_description);
        SOL_NULL_CHECK_GOTO(opt_guard, error_options);
        memset(opt_guard, 0, sizeof(*opt_guard));
    }

    return 0;

error_options:
    ptr_vector_del_last(&builder->ports_out_desc);

error_exported_out_desc:
    vector_del_last(&builder->exported_out);

error_exported_out:
    ptr_vector_del_last(&builder->ports_in_desc);

error_exported_in_desc:
    vector_del_last(&builder->exported_in);

error_exported_in:
    vector_del_last(&builder->conns);

error_conns:
    vector_del_last(&builder->nodes);

error_nodes:
    return -ENOMEM;
}

static void
remove_guards(struct sol_flow_builder *builder)
{
    vector_del_last(&builder->options_description);
    ptr_vector_del_last(&builder->ports_out_desc);
    vector_del_last(&builder->exported_out);
    ptr_vector_del_last(&builder->ports_in_desc);
    vector_del_last(&builder->exported_in);
    vector_del_last(&builder->conns);
    vector_del_last(&builder->nodes);
}

SOL_API struct sol_flow_node_type *
sol_flow_builder_get_node_type(struct sol_flow_builder *builder)
{
    struct builder_type_data *type_data;
    struct sol_flow_static_spec *spec;
    struct sol_flow_node_type_description *desc;
    struct sol_flow_node_options_description *opts = NULL;
    int err;

    SOL_NULL_CHECK(builder, NULL);

    if (builder->node_type)
        return builder->node_type;

    type_data = builder->type_data;
    spec = &type_data->spec;
    desc = &type_data->desc;

    /* TODO: ensure no repeated connections in connect() */
    qsort(builder->conns.data, builder->conns.len,
        sizeof(struct sol_flow_static_conn_spec), compare_conns);

    if (builder->options_description.len > 0) {
        opts = calloc(1, sizeof(*opts));
        if (!opts) {
            SOL_WRN("Failed to allocate memory for constructing node type");
            return NULL;
        }
    }

    err = add_guards(builder);
    if (err < 0) {
        SOL_WRN("Failed to allocate memory for constructing node type");
        errno = ENOMEM;
        free(opts);
        return NULL;
    }

    spec->nodes = builder->nodes.data;
    spec->conns = builder->conns.data;
    spec->exported_in = builder->exported_in.data;
    spec->exported_out = builder->exported_out.data;

    desc->ports_in = builder->ports_in_desc.base.data;
    desc->ports_out = builder->ports_out_desc.base.data;

    type_data->node_extras = builder->node_extras.data;

    if (opts) {
        fill_options_description(builder, opts);
        spec->child_opts_set = builder_child_opts_set;
    }

    spec->dispose = dispose_builder_type;

    builder->node_type = sol_flow_static_new_type(spec);
    if (!builder->node_type) {
        SOL_WRN("Failed to create new type");
        goto error;
    }

    if (opts) {
        builder->node_type->new_options = builder_type_new_options;
        builder->node_type->free_options = builder_type_free_options;
    }

    /* If the type was successfully created, detach the data from the
     * vectors. The data is owned by the type now. */
    sol_vector_take_data(&builder->nodes);
    sol_vector_take_data(&builder->conns);
    sol_vector_take_data(&builder->exported_in);
    sol_vector_take_data(&builder->exported_out);
    sol_vector_take_data(&builder->node_extras);
    sol_ptr_vector_take_data(&builder->ports_in_desc);
    sol_ptr_vector_take_data(&builder->ports_out_desc);

    desc->options = opts;
    builder->node_type->type_data = builder->type_data;
    builder->node_type->description = &builder->type_data->desc;

    SOL_DBG("Node type %p created", builder->node_type);

    return builder->node_type;

error:
    free(opts);

    remove_guards(builder);
    spec->nodes = NULL;
    spec->conns = NULL;
    spec->exported_in = NULL;
    spec->exported_out = NULL;
    spec->child_opts_set = NULL;

    return NULL;
}

static void
mark_own_opts(struct sol_flow_builder *builder, uint16_t node_idx)
{
    struct node_extra *node_extra;

    node_extra = sol_vector_get(&builder->node_extras, node_idx);
    SOL_NULL_CHECK(node_extra);
    node_extra->owns_opts = true;
}

void
sol_flow_builder_mark_own_all_options(struct sol_flow_builder *builder)
{
    struct node_extra *node_extra;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&builder->node_extras, node_extra, i) {
        node_extra->owns_opts = true;
    }
}

SOL_API int
sol_flow_builder_add_node_by_type(struct sol_flow_builder *builder, const char *name, const char *type_name, const char *const *options_strv)
{
    struct sol_flow_node_named_options named_opts = {}, extra_opts = {};

    struct sol_flow_node_options *opts = NULL;
    const struct sol_flow_node_type *node_type = NULL;
    const struct sol_flow_resolver *builtins_resolver;
    int r;

    SOL_NULL_CHECK(builder, -EINVAL);
    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(type_name, -EINVAL);

    builtins_resolver = sol_flow_get_builtins_resolver();

    /* Ensure that we'll always find builtin types regardless of the
     * resolver used. */
    if (builtins_resolver != builder->resolver)
        sol_flow_resolve(builtins_resolver, type_name, &node_type, &named_opts);

    if (!node_type) {
        r = sol_flow_resolve(builder->resolver, type_name, &node_type, &named_opts);
        if (r < 0)
            goto end;
    }

    /* Apply extra options. */
    if (options_strv) {
        sol_flow_node_named_options_init_from_strv(&extra_opts, node_type, options_strv);

        if (extra_opts.count > 0) {
            size_t new_count = named_opts.count + extra_opts.count;
            struct sol_flow_node_named_options_member *tmp;
            uint16_t i;

            tmp = realloc(named_opts.members, new_count * sizeof(struct sol_flow_node_named_options_member));
            if (!tmp) {
                r = -errno;
                goto end;
            }
            named_opts.members = tmp;

            for (i = 0; i < extra_opts.count; i++) {
                struct sol_flow_node_named_options_member *dst = named_opts.members + named_opts.count + i;
                struct sol_flow_node_named_options_member *src = extra_opts.members + i;

                *dst = *src;
                if (dst->type == SOL_FLOW_NODE_OPTIONS_MEMBER_STRING) {
                    dst->string = strdup(src->string);
                    if (!dst->string)
                        goto end;
                }
            }
            named_opts.count = new_count;
        }
    }

    r = sol_flow_node_options_new(node_type, &named_opts, &opts);
    if (r < 0)
        goto end;

    r = sol_flow_builder_add_node(builder, name, node_type, opts);
    if (r < 0) {
        sol_flow_node_options_del(node_type, (struct sol_flow_node_options *)opts);
    } else {
        mark_own_opts(builder, builder->nodes.len - 1);
    }

end:
    sol_flow_node_named_options_fini(&named_opts);
    sol_flow_node_named_options_fini(&extra_opts);
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

    name = sol_arena_strdup(get_arena(builder), exported_name);
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

    SOL_NULL_CHECK(builder, -EINVAL);
    SOL_NULL_CHECK(node_name, -EINVAL);
    SOL_NULL_CHECK(port_name, -EINVAL);
    SOL_NULL_CHECK(exported_name, -EINVAL);

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

    SOL_NULL_CHECK(builder, -EINVAL);
    SOL_NULL_CHECK(node_name, -EINVAL);
    SOL_NULL_CHECK(port_name, -EINVAL);
    SOL_NULL_CHECK(exported_name, -EINVAL);

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

    SOL_NULL_CHECK(builder, -EINVAL);
    SOL_NULL_CHECK(node_name, -EINVAL);
    SOL_NULL_CHECK(option_name, -EINVAL);
    SOL_NULL_CHECK(exported_name, -EINVAL);

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
    exported_opt->name = sol_arena_strdup(get_arena(builder), exported_name);
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

    if (!builder->type_data->options_size)
        builder->type_data->options_size = sizeof(struct sol_flow_builder_options);

    member_alignment = get_member_alignment(opt);
    padding = builder->type_data->options_size % member_alignment;
    exported_opt->offset = builder->type_data->options_size + padding;

    builder->type_data->options_size += exported_opt->size + padding;
    r = node_spec_add_options_reference(builder, node, exported_opt, opt);
    if (r < 0) {
        sol_vector_del(&builder->options_description, builder->options_description.len - 1);
        SOL_ERR("Failed to export option '%s' from node '%s'", option_name, node_name);
        return r;
    }

    return 0;
}
