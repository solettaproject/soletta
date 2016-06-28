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

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

#include "sol-lib-loader.h"
#include "sol-log.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

struct sol_lib_entry {
    char *name;
    void *handle;
    void *symbol;
};

struct sol_lib_loader {
    struct sol_vector loaded_cache;
    char *dir;
    char *symbol_name;
    bool (*check_func)(const char *path, const char *symbol_name, void *symbol);
};

struct sol_lib_loader *
sol_lib_loader_new(
    const char *dir,
    const char *symbol_name,
    bool (*check_func)(const char *path, const char *symbol_name, void *symbol))
{
    struct sol_lib_loader *loader;
    char *dir_copy = NULL, *symbol_name_copy = NULL;

    SOL_NULL_CHECK(dir, NULL);
    SOL_NULL_CHECK(symbol_name, NULL);

    dir_copy = strdup(dir);
    SOL_NULL_CHECK_GOTO(dir_copy, error);

    symbol_name_copy = strdup(symbol_name);
    SOL_NULL_CHECK_GOTO(symbol_name_copy, error);

    loader = calloc(1, sizeof(struct sol_lib_loader));
    SOL_NULL_CHECK_GOTO(loader, error);

    sol_vector_init(&loader->loaded_cache, sizeof(struct sol_lib_entry));
    loader->dir = dir_copy;
    loader->symbol_name = symbol_name_copy;
    loader->check_func = check_func;

    return loader;

error:
    free(dir_copy);
    free(symbol_name_copy);
    return NULL;
}

struct sol_lib_loader *
sol_lib_loader_new_in_rootdir(
    const char *dir,
    const char *symbol_name,
    bool (*check_func)(const char *path, const char *symbol_name, void *symbol))
{
    char path[PATH_MAX], install_rootdir[PATH_MAX] = {};
    int err;

    err = sol_util_get_rootdir(install_rootdir, sizeof(install_rootdir));
    SOL_INT_CHECK(err, >= (int)sizeof(install_rootdir), false);

    err = snprintf(path, sizeof(path), "%s%s", install_rootdir, dir);
    SOL_INT_CHECK(err, >= (int)sizeof(path), NULL);
    SOL_INT_CHECK(err, < 0, NULL);

    return sol_lib_loader_new(path, symbol_name, check_func);
}

void
sol_lib_loader_del(struct sol_lib_loader *loader)
{
    struct sol_lib_entry *entry;
    uint16_t i;

    if (!loader)
        return;

    SOL_VECTOR_FOREACH_REVERSE_IDX (&loader->loaded_cache, entry, i) {
        dlclose(entry->handle);
        free(entry->name);
    }

    sol_vector_clear(&loader->loaded_cache);
    free(loader->dir);
    free(loader->symbol_name);
    free(loader);
}

void *
sol_lib_load(struct sol_lib_loader *loader, const char *name)
{
    struct sol_lib_entry *entry;
    void *handle, *symbol;
    char path[PATH_MAX] = {};
    char *name_copy = NULL;
    uint16_t i;
    int err;

    if (!name || !*name) {
        errno = -EINVAL;
        return NULL;
    }

    SOL_DBG("Trying to load library named '%s'", name);

    SOL_VECTOR_FOREACH_IDX (&loader->loaded_cache, entry, i) {
        if (streq(entry->name, name)) {
            SOL_DBG("Found cached handle");
            return entry->symbol;
        }
    }

    name_copy = strdup(name);
    if (!name_copy)
        return NULL;

    err = snprintf(path, sizeof(path), "%s/%s.so", loader->dir, name);
    if (err >= (int)sizeof(path) || err < 0) {
        err = -EINVAL;
        goto error_path;
    }

    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle) {
        SOL_WRN("Could not load module '%s': %s", path, dlerror());
        goto error_dlopen;
    }

    symbol = dlsym(handle, loader->symbol_name);
    if (!symbol) {
        SOL_WRN("Could not find symbol '%s' in module '%s': %s",
            loader->symbol_name, path, dlerror());
        goto error_symbol;
    }

    if (loader->check_func && !loader->check_func(path, loader->symbol_name, symbol)) {
        SOL_WRN("Module '%s' rejected by check function", path);
        goto error_check;
    }

    entry = sol_vector_append(&loader->loaded_cache);
    if (!entry) {
        SOL_WRN("No memory to save loaded library entry for '%s'", path);
        goto error_vector;
    }

    entry->name = name_copy;
    entry->handle = handle;
    entry->symbol = symbol;

    SOL_INF("Loaded module '%s' from '%s'", entry->name, path);
    return entry->symbol;

error_vector:
error_check:
error_symbol:
    dlclose(handle);

error_dlopen:
error_path:
    free(name_copy);
    errno = -err;
    return NULL;
}
