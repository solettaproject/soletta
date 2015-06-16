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

#include <dlfcn.h>
#include <glib.h>

#include "sol-conffile.h"
#include "sol-flow-internal.h"
#include "sol-flow-resolver.h"
#include "sol-str-slice.h"

struct resolve_module_type_foreach_ctx {
    const char *name;
    const struct sol_flow_node_type *found;
};

static bool
resolve_module_type_foreach(void *data, const struct sol_flow_node_type *type)
{
    struct resolve_module_type_foreach_ctx *ctx = data;

    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, true);

    if (!type->description || !type->description->name)
        return true;

    if (!streq(type->description->name, ctx->name))
        return true;

    ctx->found = type;
    return false;
}

static const struct sol_flow_node_type *
resolve_module_type_by_component(const char *component, void (*foreach)(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data))
{
    struct resolve_module_type_foreach_ctx ctx = {
        .name = component,
        .found = NULL
    };

    SOL_NULL_CHECK(component, NULL);
    SOL_NULL_CHECK(foreach, NULL);

    foreach(resolve_module_type_foreach, &ctx);

    return ctx.found;
}

static GHashTable *resolver_conffile_dlopens = NULL;
struct resolver_conffile_dlopen {
    char *name;
    void *handle;
    void (*foreach)(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);
};

static void
resolver_conffile_dlopen_free(struct resolver_conffile_dlopen *entry)
{
    /* do no dlclose() as some modules may crash due hanging references */
    free(entry->name);
    free(entry);
}

static void
resolver_conffile_clear_data(void)
{
    g_hash_table_destroy(resolver_conffile_dlopens);
    resolver_conffile_dlopens = NULL;
}

const char MODULE_NAME_SEPARATOR = '/';

static struct sol_str_slice
get_module_for_type(const char *type)
{
    char *sep;

    sep = strchr(type, MODULE_NAME_SEPARATOR);
    if (!sep)
        return SOL_STR_SLICE_STR(type, strlen(type));
    return SOL_STR_SLICE_STR(type, sep -  type);
}

static const struct sol_flow_node_type *
_resolver_conffile_get_module(const char *type)
{
    struct resolver_conffile_dlopen *entry;
    const struct sol_flow_node_type *ret;
    struct sol_str_slice module_name;
    char path[PATH_MAX];
    char *name;
    int r;

    if (!resolver_conffile_dlopens) {
        resolver_conffile_dlopens = g_hash_table_new_full(
            g_str_hash,
            g_str_equal,
            NULL,
            (GDestroyNotify)resolver_conffile_dlopen_free);
        SOL_NULL_CHECK(resolver_conffile_dlopens, NULL);
        atexit(resolver_conffile_clear_data);
    }

    module_name = get_module_for_type(type);
    if (module_name.len == 0) {
        SOL_DBG("Invalid empty name");
        return NULL;
    }

    name = strndup(module_name.data, module_name.len);
    SOL_NULL_CHECK(name, NULL);

    /* the hash entry keys are the type part only */
    entry = g_hash_table_lookup(resolver_conffile_dlopens, name);
    if (entry) {
        free(name);
        goto found;
    }

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        SOL_DBG("Could not alloc memory for entry");
        free(name);
        return NULL;
    }

    entry->name = name;
    r = snprintf(path, sizeof(path), "%s/%s.so",
        FLOWMODULESDIR, name);
    if (r < 0 || r >= (int)sizeof(path))
        goto error;

    entry->handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!entry->handle) {
        SOL_DBG("could not load module '%s': %s", path, dlerror());
        goto error;
    }
    entry->foreach = dlsym(entry->handle, "sol_flow_foreach_module_node_type");
    if (!entry->foreach) {
        SOL_DBG("could not find symbol "
            "sol_flow_foreach_module_node_type() "
            "in module '%s': %s", path, dlerror());
        goto error;
    }

    g_hash_table_insert(resolver_conffile_dlopens,
        entry->name, entry);

found:
    ret = resolve_module_type_by_component(type, entry->foreach);
    SOL_NULL_CHECK_GOTO(ret, wipe_entry);

    return ret;

error:
    resolver_conffile_dlopen_free(entry);
    return NULL;

wipe_entry:
    g_hash_table_remove(resolver_conffile_dlopens, entry->name);
    return NULL;
}

static int
resolver_conffile_resolve_by_type_name(const char *id,
    struct sol_flow_node_type const **node_type,
    char const ***opts_strv)
{
    const struct sol_flow_node_type *type;

    type = _resolver_conffile_get_module(id);
    if (!type)
        return -ENOENT;

    *node_type = type;
    *opts_strv = NULL;

    return 0;
}

static int
resolver_conffile_get_strv(const char *id,
    struct sol_flow_node_type const **node_type,
    char const ***opts_as_string)
{
    const char *type_name;

    if (sol_conffile_resolve(id, &type_name, opts_as_string) < 0) {
        SOL_DBG("could not resolve a type name for id='%s'", id);
        return -EINVAL;

    }

    *node_type = resolve_module_type_by_component
                     (type_name, sol_flow_foreach_builtin_node_type);
    if (!*node_type) {
        *node_type = _resolver_conffile_get_module(type_name);
        if (!*node_type) {
            SOL_DBG("could not resolve a node module for Type='%s'", type_name);
            return -EINVAL;
        }
    }

    return 0;
}

static int
resolver_conffile_resolve_by_id(const char *id,
    struct sol_flow_node_type const **node_type,
    const char ***opts_strv)
{
    return resolver_conffile_get_strv(id, node_type, opts_strv);
}

static int
resolver_conffile_resolve(void *data, const char *id,
    struct sol_flow_node_type const **node_type,
    char const ***opts_strv)
{
    const char **strv;
    int ret;

    if (strchr(id, MODULE_NAME_SEPARATOR)) {
        ret = resolver_conffile_resolve_by_type_name(id, node_type, &strv);
        if (ret < 0)
            return ret;
        goto end;
    }
    ret = resolver_conffile_resolve_by_id(id, node_type, &strv);
    if (ret < 0)
        return ret;

end:
    *opts_strv = (const char **)g_strdupv((gchar **)strv);
    return 0;
}

static struct sol_flow_resolver _resolver_conffile = {
    .api_version = SOL_FLOW_RESOLVER_API_VERSION,
    .name = "conffile",
    .resolve = resolver_conffile_resolve,
};

SOL_API struct sol_flow_resolver *sol_flow_resolver_conffile = &_resolver_conffile;
