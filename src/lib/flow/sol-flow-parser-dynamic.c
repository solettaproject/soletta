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

#include "sol-flow-internal.h"
#include "sol-lib-loader.h"

static struct sol_lib_loader *metatype_loader;

static bool
check_metatype(const char *path, const char *symbol_name, void *symbol)
{
    const struct sol_flow_metatype **p_metatype, *metatype;

    p_metatype = symbol;
    metatype = *p_metatype;

    SOL_NULL_CHECK_MSG(metatype, false,
        "Symbol '%s' in module '%s' is points to NULL instead of a valid metatype",
        symbol_name, path);

#ifndef SOL_NO_API_VERSION
    if (metatype->api_version != SOL_FLOW_METATYPE_API_VERSION) {
        SOL_WRN("Module '%s' has incorrect api_version: %" PRIu16 " expected %" PRIu16,
            path, metatype->api_version, SOL_FLOW_METATYPE_API_VERSION);
        return false;
    }
#endif

    return true;
}

const struct sol_flow_metatype *
get_dynamic_metatype(const struct sol_str_slice name)
{
    const struct sol_flow_metatype *metatype;
    void *symbol;
    char name_str[PATH_MAX] = {};

    SOL_INT_CHECK(name.len, > (PATH_MAX - 1), NULL);
    memcpy(name_str, name.data, name.len);

    if (!metatype_loader) {
        metatype_loader = sol_lib_loader_new_in_rootdir(
            FLOWMETATYPEMODULESDIR, "SOL_FLOW_METATYPE", check_metatype);
        SOL_NULL_CHECK(metatype_loader, NULL);
    }

    symbol = sol_lib_load(metatype_loader, name_str);
    if (!symbol)
        return NULL;

    metatype = *(const struct sol_flow_metatype **)symbol;
    return metatype;
}

void
loaded_metatype_cache_shutdown(void)
{
    sol_lib_loader_del(metatype_loader);
    metatype_loader = NULL;
}
