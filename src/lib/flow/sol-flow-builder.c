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

#include <stdlib.h>

#include "sol-flow-builder.h"
#include "sol-flow-internal.h"
#include "sol-flow-resolver.h"

struct sol_flow_builder {
    struct sol_vector nodes;
    struct sol_ptr_vector conns;
    const struct sol_flow_resolver *resolver;

    struct sol_flow_static_node_spec *node_spec;
    struct sol_flow_static_conn_spec *conn_spec;
    struct sol_flow_node_type *node_type;

    struct sol_vector exported_in;
    struct sol_vector exported_out;

    struct sol_ptr_vector ports_in_desc;
    struct sol_ptr_vector ports_out_desc;

    struct sol_flow_node_type_description type_desc;
};

struct sol_flow_builder_node_spec {
    struct sol_flow_static_node_spec spec;
    char *name;

    /* Whether builder owns the options for this node. */
    bool owns_opts;
};

static void
sol_flow_builder_init(struct sol_flow_builder *builder)
{
    sol_vector_init(&builder->nodes, sizeof(struct sol_flow_builder_node_spec));
    sol_ptr_vector_init(&builder->conns);
    sol_vector_init(&builder->exported_in, sizeof(struct sol_flow_static_port_spec));
    sol_vector_init(&builder->exported_out, sizeof(struct sol_flow_static_port_spec));
    sol_ptr_vector_init(&builder->ports_in_desc);
    sol_ptr_vector_init(&builder->ports_out_desc);

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

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->ports_in_desc, port_desc, i) {
        if (port_desc) {
            free((char *)port_desc->name);
            free(port_desc);
        }
    }
    sol_ptr_vector_clear(&builder->ports_in_desc);

    SOL_PTR_VECTOR_FOREACH_IDX (&builder->ports_out_desc, port_desc, i) {
        if (port_desc) {
            free((char *)port_desc->name);
            free(port_desc);
        }
    }
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
        free(builder_node_spec->name);
    }
    sol_vector_clear(&builder->nodes);

    free(builder->node_spec);
    free(builder->conn_spec);

    if (builder->node_type)
        sol_flow_static_del_type(builder->node_type);

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

    node_name = strdup(name);
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

    return &builder->type_desc;
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

SOL_API struct sol_flow_node_type *
sol_flow_builder_get_node_type(struct sol_flow_builder *builder)
{
    struct sol_flow_static_port_spec *exported_in, *exported_out;
    struct sol_flow_node_type_description *desc;
    int err;

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

    builder->node_type = sol_flow_static_new_type(
        builder->node_spec,
        builder->conn_spec,
        exported_in,
        exported_out,
        NULL);
    if (!builder->node_type) {
        SOL_WRN("Failed to create new type");
        goto error_node_type;
    }

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

struct builder_from_string {
    struct sol_flow_builder base;
    const struct sol_flow_resolver *resolver;
};

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
    memcpy(joined + first_size, second, second_size);

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
sol_flow_builder_add_node_by_id(struct sol_flow_builder *builder, const char *id)
{
    struct sol_flow_static_node_spec node_spec;
    int r;

    SOL_NULL_CHECK(builder, -EBADR);

    node_spec.name = id;
    r = find_type(builder->resolver, id, NULL, &node_spec.type, &node_spec.opts);
    if (r < 0)
        return r;

    r = sol_flow_builder_add_node(builder, node_spec.name, node_spec.type,
        node_spec.opts);
    if (r < 0) {
        if (node_spec.opts) {
            sol_flow_node_options_del(node_spec.type,
                (struct sol_flow_node_options *)node_spec.opts);
        }
    }

    mark_own_opts(builder, builder->nodes.len - 1);
    return 0;
}


SOL_API int
sol_flow_builder_add_node_by_type(struct sol_flow_builder *builder, const char *id, const char *type, const char *const *options_strv)
{
    const struct sol_flow_node_options *opts = NULL;
    const struct sol_flow_node_type *node_type = NULL;
    const struct sol_flow_resolver *builtins_resolver;
    int r;

    SOL_NULL_CHECK(builder, -EBADR);
    SOL_NULL_CHECK(id, -EBADR);
    SOL_NULL_CHECK(type, -EBADR);

    builtins_resolver = sol_flow_get_builtins_resolver();

    /* Ensure that we'll always find builtin types regardless of the
     * resolver used. */
    if (builtins_resolver != builder->resolver)
        find_type(builtins_resolver, type, options_strv, &node_type, &opts);

    if (!node_type) {
        r = find_type(builder->resolver, type, options_strv, &node_type, &opts);
        if (r < 0)
            return r;
    }

    r = sol_flow_builder_add_node(builder, id, node_type, opts);
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

    name = strdup(exported_name);
    SOL_NULL_CHECK_GOTO(name, error_name);

    desc_len = sol_ptr_vector_get_len(desc_vector);
    if (desc_len) {
        port_desc = sol_ptr_vector_get(desc_vector, desc_len - 1);
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
        port_spec->port = port;
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
