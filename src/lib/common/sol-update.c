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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "sol-log-internal.h"

#include "sol-lib-loader.h"
#include "sol-update.h"
#include "sol-update-modules.h"
#include "sol-update-builtins-gen.h"
#include "sol-util.h"
#include "sol-util-file.h"

SOL_LOG_INTERNAL_DECLARE(_sol_update_log_domain, "update");

static struct sol_lib_loader *update_module_loader;
static const struct sol_update *update_module;

int sol_update_init(void);
void sol_update_shutdown(void);

#if (SOL_UPDATE_BUILTIN_COUNT == 0) && defined ENABLE_DYNAMIC_MODULES
static bool
check_module(const char *path, const char *symbol_name, void *symbol)
{
    const struct sol_update **p_update;

    p_update = symbol;

    SOL_NULL_CHECK_MSG(*p_update, false, "Symbol [%s] in module [%s] is NULL",
        symbol_name, path);

#ifndef SOL_NO_API_VERSION
    if ((*p_update)->api_version != SOL_UPDATE_API_VERSION) {
        SOL_WRN("Module [%s] has incorrect api_version: %u expected %u", path,
            (*p_update)->api_version, SOL_UPDATE_API_VERSION);
        return false;
    }
#endif

    return true;
}

static bool
iterate_dir_cb(void *data, const char *dir_path, struct dirent *ent)
{
    size_t len;
    char **result = data;

    len = strlen(ent->d_name);

    /* Return anything that finishes in '.so'*/
    if (len > 3) {
        if (streq((ent->d_name + (len - 3)), ".so")) {
            *result = strndup(ent->d_name, len - 3);
            return true;
        }
    }

    return false;
}

static char *
get_first_module_on_dir(const char *dir_name)
{
    char *result;
    char path[PATH_MAX], install_rootdir[PATH_MAX];
    struct stat st;
    int r;

    r = sol_util_get_rootdir(install_rootdir, sizeof(install_rootdir));
    SOL_INT_CHECK(r, >= (int)sizeof(install_rootdir), NULL);
    SOL_INT_CHECK(r, < 0, NULL);

    r = snprintf(path, sizeof(path), "%s%s", install_rootdir, dir_name);
    SOL_INT_CHECK(r, >= (int)sizeof(path), NULL);
    SOL_INT_CHECK(r, < 0, NULL);

    if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
        SOL_DBG("Invalid update module dir: %s", path);
        return NULL;
    }

    if (!sol_util_iterate_dir(path, iterate_dir_cb, &result))
        return NULL;

    return result;
}
#endif

static bool
load_update_module(void)
{
#if (SOL_UPDATE_BUILTIN_COUNT > 0)
    update_module = SOL_UPDATE_BUILTINS_ALL[0];
#elif defined ENABLE_DYNAMIC_MODULES
    void *symbol;
    const char *update_module_name = getenv("SOL_UPDATE_MODULE");
    char *name_to_free = NULL;

    if (!update_module_loader) {
        update_module_loader = sol_lib_loader_new_in_rootdir(
            UPDATEMODULESDIR, "SOL_UPDATE", check_module);
        SOL_NULL_CHECK(update_module_loader, false);
    }

    if (!update_module_name || strlen(update_module_name) == 0) {
        name_to_free = get_first_module_on_dir(UPDATEMODULESDIR);
        update_module_name = name_to_free;
    }
    if (!update_module_name) {
        SOL_DBG("No update module to load");
        return true; /* Not having an update module is not an error */
    }

    symbol = sol_lib_load(update_module_loader, update_module_name);
    free(name_to_free);
    if (!symbol) {
        SOL_DBG("No update module found");
        return true; /* Not having an update module is not an error */
    }

    update_module = *(const struct sol_update **)symbol;
    if (!update_module)
        return false;
#endif
    return true;
}

int
sol_update_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    if (!load_update_module()) {
        SOL_WRN("Could not initialise update module");
        return -1;
    }

    if (update_module && update_module->init)
        return update_module->init();

    return 0;
}

void
sol_update_shutdown(void)
{
    SOL_DBG("Update shutdown");

    if (update_module && update_module->shutdown)
        update_module->shutdown();

    sol_lib_loader_del(update_module_loader);
}

SOL_API struct sol_update_handle *
sol_update_check(
    void (*cb)(void *data, int status, const struct sol_update_info *response),
    const void *data)
{
    SOL_NULL_CHECK_MSG(update_module, NULL, "No update module found");
    SOL_NULL_CHECK_MSG(update_module->check, NULL, "No check function on update module");

    return update_module->check(cb, data);
}

SOL_API struct sol_update_handle *
sol_update_fetch(void (*cb)(void *data, int status),
    const void *data, bool resume)
{
    SOL_NULL_CHECK_MSG(update_module, NULL, "No update module found");
    SOL_NULL_CHECK_MSG(update_module->fetch, NULL, "No fetch function on update module");

    return update_module->fetch(cb, data, resume);
}

/* TODO should a cancel remove downloaded stuff? Shall we have a pause?
 * Shall we allow cancelling of 'install'? */
SOL_API bool
sol_update_cancel(struct sol_update_handle *handle)
{
    SOL_NULL_CHECK_MSG(update_module, false, "No update module found");
    SOL_NULL_CHECK_MSG(update_module->cancel, false, "No cancel function on update module");

    return update_module->cancel(handle);
}

SOL_API int
sol_update_get_progress(struct sol_update_handle *handle)
{
    SOL_NULL_CHECK_MSG(update_module, -EINVAL, "No update module found");
    SOL_NULL_CHECK_MSG(update_module->get_progress, -ENOTSUP, "No progress function on update module");

    return update_module->get_progress(handle);
}

SOL_API struct sol_update_handle *
sol_update_install(void (*cb)(void *data, int status), const void *data)
{
    SOL_NULL_CHECK_MSG(update_module, NULL, "No update module found");
    SOL_NULL_CHECK_MSG(update_module->install, NULL, "No install function on update module");

    return update_module->install(cb, data);
}
