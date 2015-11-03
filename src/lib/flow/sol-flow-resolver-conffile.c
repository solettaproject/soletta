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

#include <assert.h>
#include <dlfcn.h>
#include <limits.h>

#include "sol-conffile.h"
#include "sol-flow-internal.h"
#include "sol-flow-resolver.h"
#include "sol-str-slice.h"
#include "sol-util.h"

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

struct resolver_conffile_dlopen {
    char *name;
    void *handle;
    void (*foreach)(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);
};

static struct sol_vector resolver_conffile_dlopens = SOL_VECTOR_INIT(struct resolver_conffile_dlopen);

static void
resolver_conffile_dlopen_free(struct resolver_conffile_dlopen *entry)
{
    /* do no dlclose() as some modules may crash due hanging references */
    free(entry->name);
}

static void
resolver_conffile_clear_data(void)
{
    struct resolver_conffile_dlopen *entry;
    unsigned int i;

    SOL_VECTOR_FOREACH_IDX (&resolver_conffile_dlopens, entry, i) {
        resolver_conffile_dlopen_free(entry);
    }

    sol_vector_clear(&resolver_conffile_dlopens);
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

static int
find_entry_by_match(struct sol_vector *entries,
    const struct resolver_conffile_dlopen *match)
{
    const struct resolver_conffile_dlopen *entry;
    unsigned int i;

    SOL_VECTOR_FOREACH_IDX (entries, entry, i) {
        if (entry == match)
            return i;
    }

    return -1;
}

static struct resolver_conffile_dlopen *
find_entry_by_name(struct sol_vector *entries, const char *name)
{
    struct resolver_conffile_dlopen *entry;
    unsigned int i;

    SOL_VECTOR_FOREACH_IDX (entries, entry, i) {
        if (streq(name, entry->name))
            return entry;
    }

    return NULL;
}

static const struct sol_flow_node_type *
_resolver_conffile_get_module(const char *type)
{
    struct resolver_conffile_dlopen *entry;
    const struct sol_flow_node_type *ret;
    struct sol_str_slice module_name;
    char path[PATH_MAX], install_rootdir[PATH_MAX] = { 0 };
    char *name;
    int r, index;

    module_name = get_module_for_type(type);
    if (module_name.len == 0) {
        SOL_WRN("invalid empty module name");
        return NULL;
    }

    name = strndup(module_name.data, module_name.len);
    SOL_NULL_CHECK(name, NULL);

    /* the hash entry keys are the type part only */
    entry = find_entry_by_name(&resolver_conffile_dlopens, name);
    if (entry) {
        SOL_DBG("module named '%s' previously loaded", name);
        free(name);
        goto found;
    }

    if (resolver_conffile_dlopens.len == 0) {
        atexit(resolver_conffile_clear_data);
    }

    entry = sol_vector_append(&resolver_conffile_dlopens);
    SOL_NULL_CHECK_GOTO(entry, entry_error);

    entry->name = name;

    r = sol_util_get_rootdir(install_rootdir, sizeof(install_rootdir));
    if (r < 0 || r >= (int)sizeof(install_rootdir)) {
        SOL_WRN("failed to get rootdir for module %s", name);
        goto error;
    }

    r = snprintf(path, sizeof(path), "%s%s/%s.so",
        install_rootdir, FLOWMODULESDIR, name);
    if (r < 0 || r >= (int)sizeof(path)) {
        SOL_WRN("failed set path for module %s", name);
        goto error;
    }

    entry->handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!entry->handle) {
        SOL_WRN("Could not load module '%s':\n    %s", name, dlerror());
        goto error;
    }
    entry->foreach = dlsym(entry->handle, "sol_flow_foreach_module_node_type");
    if (!entry->foreach) {
        SOL_WRN("could not find symbol "
            "sol_flow_foreach_module_node_type() "
            "in module '%s': %s", path, dlerror());
        goto error;
    }

    SOL_DBG("module named '%s' loaded from '%s'", entry->name, path);

found:
    ret = resolve_module_type_by_component(type, entry->foreach);
    SOL_NULL_CHECK_MSG_GOTO(ret, error, "Type='%s' not found.", type);
    return ret;

entry_error:
    SOL_WRN("failed to alloc memory for new module %s", name);
    free(name);
    return NULL;

error:
    /* In case 'entry' was not the last one appended. */
    index = find_entry_by_match(&resolver_conffile_dlopens, entry);
    assert(index >= 0);
    resolver_conffile_dlopen_free(entry);
    sol_vector_del(&resolver_conffile_dlopens, index);

    return NULL;
}

static int
resolver_conffile_resolve_by_type_name(const char *id,
    struct sol_flow_node_type const **node_type,
    struct sol_flow_node_named_options *named_opts)
{
    const struct sol_flow_node_type *type;

    type = _resolver_conffile_get_module(id);
    if (!type)
        return -ENOENT;

    *node_type = type;
    *named_opts = (struct sol_flow_node_named_options){};

    return 0;
}

static int
resolver_conffile_resolve_by_id(const char *id,
    struct sol_flow_node_type const **node_type,
    struct sol_flow_node_named_options *named_opts)
{
    const struct sol_flow_node_type *tmp_type;
    const char **opts_strv = NULL;
    const char *type_name;
    int r = 0;

    /* TODO: replace strv in sol_conffile interface with something
    * that holds line/column number information for each entry. */
    r = sol_conffile_resolve(id, &type_name, &opts_strv);
    if (r < 0) {
        /* the resolver may fail because there's no entry with the given
         * name, but that may be because it's the name of a single type
         * module, like console or timer, so treat that case especially */
        if (r != -ENOENT) {
            SOL_DBG("could not resolve a type name for id='%s'", id);
            return -EINVAL;
        }
        type_name = id;
        r = 0;
    }

    /* TODO: is this needed given we already handle builtins from the outside? */
    tmp_type = resolve_module_type_by_component(type_name, sol_flow_foreach_builtin_node_type);

    if (!tmp_type) {
        tmp_type = _resolver_conffile_get_module(type_name);
        if (!tmp_type) {
            r = -EINVAL;
            SOL_DBG("could not resolve a node module for Type='%s'", type_name);
            goto end;
        }
    }

    if (opts_strv) {
        r = sol_flow_node_named_options_init_from_strv(named_opts, tmp_type, opts_strv);
        if (r < 0)
            goto end;
    } else
        *named_opts = (struct sol_flow_node_named_options){};

    *node_type = tmp_type;
end:
    return r;
}

static int
resolver_conffile_resolve(void *data, const char *id,
    struct sol_flow_node_type const **node_type,
    struct sol_flow_node_named_options *named_opts)
{
    if (strchr(id, MODULE_NAME_SEPARATOR))
        return resolver_conffile_resolve_by_type_name(id, node_type, named_opts);
    return resolver_conffile_resolve_by_id(id, node_type, named_opts);
}

static const struct sol_flow_resolver _resolver_conffile = {
    .api_version = SOL_FLOW_RESOLVER_API_VERSION,
    .name = "conffile",
    .resolve = resolver_conffile_resolve,
};

SOL_API const struct sol_flow_resolver *sol_flow_resolver_conffile = &_resolver_conffile;
