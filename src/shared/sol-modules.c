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

#ifdef MODULES

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "sol-log-internal.h"

#include "sol-modules.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE(_sol_modules_log_domain, "modules");

struct nspace_cache {
    char *name;
    struct sol_vector modules;
};

struct module_cache {
    char *name;
    void *handle;
};

static struct sol_vector namespaces = SOL_VECTOR_INIT(struct nspace_cache);

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

static void
clear_namespace_cache(struct sol_vector *cache)
{
    struct nspace_cache *ns;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (cache, ns, i) {
        clear_modules_cache(&ns->modules);
        free(ns->name);
    }
    sol_vector_clear(cache);
}

static int
get_symbol(void *handle, const char *symbol_name, void **symbol)
{
    *symbol = dlsym(handle, symbol_name);
    if (!*symbol)
        return -ENOENT;

    return 0;
}

static int
get_internal_symbol(const char *symbol_name, void **symbol)
{
    int ret;

    if ((ret = get_symbol(RTLD_DEFAULT, symbol_name, symbol)) < 0) {
        SOL_DBG("Symbol '%s' is not built-in: %s", symbol_name, dlerror());
        return ret;
    }

    SOL_INF("Symbol '%s' found built-in", symbol_name);

    return 0;
}

static struct nspace_cache *
get_namespace(const char *nspace)
{
    struct nspace_cache *ns;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&namespaces, ns, i) {
        if (streq(ns->name, nspace))
            return ns;
    }

    ns = sol_vector_append(&namespaces);
    SOL_NULL_CHECK(ns, NULL);

    ns->name = strdup(nspace);
    SOL_NULL_CHECK_GOTO(ns->name, strdup_error);

    sol_vector_init(&ns->modules, sizeof(struct module_cache));

    return ns;

strdup_error:
    free(ns);
    return NULL;
}

static int
get_module_path(char *buf, size_t len, const char *nspace, const char *modname)
{
    static char rootdir[PATH_MAX] = { };

    if (unlikely(!*rootdir)) {
        int ret;

        ret = sol_util_get_rootdir(rootdir, sizeof(rootdir));
        SOL_INT_CHECK(ret, >= (int)sizeof(rootdir), ret);
        SOL_INT_CHECK(ret, < 0, ret);
    }

    return snprintf(buf, len, "%s%s%s/%s.so", rootdir, MODULESDIR, nspace, modname);
}

static void *
get_module_handle(const char *nspace, const char *modname)
{
    char path[PATH_MAX];
    void *handle;
    int ret;

    ret = get_module_path(path, sizeof(path), nspace, modname);
    SOL_INT_CHECK(ret, >= (int)sizeof(path), NULL);
    SOL_INT_CHECK(ret, < 0, NULL);

    SOL_INF("Loading module '%s'", path);

    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle)
        SOL_WRN("Could not open module '%s/%s' (%s): %s", nspace, modname, path, dlerror());
    return handle;
}

static struct module_cache *
get_module(struct nspace_cache *ns, const char *module)
{
    struct module_cache *mod;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&ns->modules, mod, i) {
        if (streq(mod->name, module)) {
            SOL_INF("Module '%s/%s' found cached", ns->name, module);
            return mod;
        }
    }

    mod = sol_vector_append(&ns->modules);
    SOL_NULL_CHECK(mod, NULL);

    mod->name = strdup(module);
    SOL_NULL_CHECK_GOTO(mod->name, strdup_error);

    mod->handle = get_module_handle(ns->name, mod->name);
    SOL_NULL_CHECK_GOTO(mod->handle, dlopen_error);

    return mod;

dlopen_error:
    free(mod->name);
strdup_error:
    sol_vector_del(&ns->modules, ns->modules.len - 1);
    return NULL;
}

static int
get_module_symbol(const char *nspace, const char *modname, const char *symbol_name, void **symbol)
{
    struct nspace_cache *ns;
    struct module_cache *mod;
    int ret;

    ns = get_namespace(nspace);
    SOL_NULL_CHECK(ns, -ENOMEM);

    mod = get_module(ns, modname);
    SOL_NULL_CHECK(mod, -ENOMEM);

    if ((ret = get_symbol(mod->handle, symbol_name, symbol)) < 0) {
        char path[PATH_MAX];

        get_module_path(path, sizeof(path), nspace, modname);
        SOL_WRN("Symbol '%s' not found in module '%s/%s' (%s): %s",
            symbol_name, nspace, modname, path, dlerror());
        return ret;
    }

    return 0;
}

void *
sol_modules_get_symbol(const char *nspace, const char *modname, const char *symbol)
{
    void *sym;
    int ret;

    SOL_DBG("Trying for symbol '%s' internally", symbol);
    if ((ret = get_internal_symbol(symbol, &sym)) < 0) {
        SOL_DBG("Trying for symbol '%s' in '%s' module '%s'", symbol, nspace, modname);
        if ((ret = get_module_symbol(nspace, modname, symbol, &sym)) < 0) {
            SOL_DBG("Symbol '%s' of module '%s/%s' not found.", modname, nspace, symbol);
            errno = -ret;
            sym = NULL;
        }
    }

    return sym;
}
#endif /* MODULES */

void
sol_modules_clear_cache(void)
{
#ifdef MODULES
    clear_namespace_cache(&namespaces);
#endif
}
