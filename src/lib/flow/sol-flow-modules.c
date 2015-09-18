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
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "sol-flow-internal.h"

struct module_cache {
    char *name;
    void *handle;
};

static struct sol_vector modules = SOL_VECTOR_INIT(struct module_cache);

static void
clear_modules_cache(struct sol_vector *cache)
{
    struct module_cache *mod;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (cache, mod, i) {
        dlclose(mod->handle);
        free(mod->name);
    }
    sol_vector_clear(cache);
}

static int
get_symbol(void *handle, const char *symbol_name, const struct sol_flow_node_type **type)
{
    void **symbol;

    symbol = dlsym(handle, symbol_name);
    if (!symbol || !(*symbol))
        return -ENOENT;

    *type = *symbol;

    return 0;
}

static int
get_internal_symbol(const char *symbol_name, const struct sol_flow_node_type **type)
{
    int ret;

    if ((ret = get_symbol(RTLD_DEFAULT, symbol_name, type)) < 0) {
        SOL_DBG("Symbol '%s' is not built-in: %s", symbol_name, dlerror());
        return ret;
    }

    SOL_INF("Symbol '%s' found built-in", symbol_name);

    return 0;
}

static int
get_module_path(char *buf, size_t len, const char *modname)
{
    static char rootdir[PATH_MAX] = { };

    if (unlikely(!*rootdir)) {
        int ret;

        ret = sol_util_get_rootdir(rootdir, sizeof(rootdir));
        SOL_INT_CHECK(ret, >= (int)sizeof(rootdir), ret);
        SOL_INT_CHECK(ret, < 0, ret);
    }

    return snprintf(buf, len, "%s%s/%s.so", rootdir, FLOWMODULESDIR, modname);
}

static void *
get_module_handle(const char *modname)
{
    char path[PATH_MAX];
    void *handle;
    int ret;

    ret = get_module_path(path, sizeof(path), modname);
    SOL_INT_CHECK(ret, >= (int)sizeof(path), NULL);
    SOL_INT_CHECK(ret, < 0, NULL);

    SOL_INF("Loading module '%s'", path);

    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle)
        SOL_WRN("Could not open module '%s' (%s): %s", modname, path, dlerror());
    return handle;
}

static struct module_cache *
get_module(const char *module)
{
    struct module_cache *mod;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&modules, mod, i) {
        if (streq(mod->name, module)) {
            SOL_INF("Module '%s' found cached", module);
            return mod;
        }
    }

    mod = sol_vector_append(&modules);
    SOL_NULL_CHECK(mod, NULL);

    mod->name = strdup(module);
    SOL_NULL_CHECK_GOTO(mod->name, strdup_error);

    mod->handle = get_module_handle(mod->name);
    SOL_NULL_CHECK_GOTO(mod->handle, dlopen_error);

    return mod;

dlopen_error:
    free(mod->name);
strdup_error:
    sol_vector_del(&modules, modules.len - 1);
    return NULL;
}

static int
get_module_symbol(const char *modname, const char *symbol_name, const struct sol_flow_node_type **type)
{
    struct module_cache *mod;
    int ret;

    mod = get_module(modname);
    SOL_NULL_CHECK(mod, -ENOMEM);

    if ((ret = get_symbol(mod->handle, symbol_name, type)) < 0) {
        char path[PATH_MAX];

        get_module_path(path, sizeof(path), modname);
        SOL_WRN("Symbol '%s' not found in module '%s' (%s): %s",
            symbol_name, modname, path, dlerror());
        return ret;
    }

    return 0;
}

SOL_API int
sol_flow_int_get_node_type(const char *modname, const char *symbol, const struct sol_flow_node_type **type)
{
    int ret;

    SOL_DBG("Trying for symbol '%s' internally", symbol);
    if ((ret = get_internal_symbol(symbol, type)) < 0) {
        SOL_DBG("Trying for symbol '%s' in module '%s'", symbol, modname);
        if ((ret = get_module_symbol(modname, symbol, type)) < 0)
            SOL_WRN("Symbol '%s' of module '%s' not found.", symbol, modname);
    }

    return ret;
}

void
sol_flow_modules_cache_shutdown(void)
{
    clear_modules_cache(&modules);
}
