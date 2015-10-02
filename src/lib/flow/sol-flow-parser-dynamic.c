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

#include "sol-flow-internal.h"
#include "sol-lib-loader.h"

static struct sol_lib_loader *metatype_loader;

static bool
check_metatype(const char *path, const char *symbol_name, void *symbol)
{
    const struct sol_flow_metatype **p_metatype, *metatype;

    p_metatype = symbol;
    metatype = *p_metatype;

    if (!metatype) {
        SOL_WRN("Symbol '%s' in module '%s' is points to NULL instead of a valid metatype",
            symbol_name, path);
        return false;
    }

    if (metatype->api_version != SOL_FLOW_METATYPE_API_VERSION) {
        SOL_WRN("Module '%s' has incorrect api_version: %u expected %u",
            path, metatype->api_version, SOL_FLOW_METATYPE_API_VERSION);
        return false;
    }

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
}
