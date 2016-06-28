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

#include <stddef.h>

#include "sol-conffile.h"
#include "sol-flow-resolver.h"
#include "sol-flow-internal.h"
#include "sol-flow-buildopts.h"

struct find_type_ctx {
    const char *name;
    const struct sol_flow_node_type *type;
};

static bool
find_type_cb(void *data, const struct sol_flow_node_type *type)
{
    struct find_type_ctx *ctx = data;

    if (streq(ctx->name, type->description->name)) {
        ctx->type = type;
        return false;
    }
    return true;
}

static int
builtins_resolve(void *data, const char *id, struct sol_flow_node_type const **type,
    struct sol_flow_node_named_options *named_opts)
{
    struct find_type_ctx ctx = {};

    ctx.name = id;
    sol_flow_foreach_builtin_node_type(find_type_cb, &ctx);
    if (!ctx.type)
        return -ENOENT;
    *type = ctx.type;
    /* When resolving to a type, no options are set. */
    *named_opts = (struct sol_flow_node_named_options){};
    return 0;
}

static const struct sol_flow_resolver builtins_resolver = {
    SOL_SET_API_VERSION(.api_version = SOL_FLOW_RESOLVER_API_VERSION, )
    .name = "builtins_resolver",
    .resolve = builtins_resolve,
};

SOL_API const struct sol_flow_resolver *sol_flow_resolver_builtins = &builtins_resolver;

SOL_API const struct sol_flow_resolver *
sol_flow_get_builtins_resolver(void)
{
    return &builtins_resolver;
}

SOL_API const struct sol_flow_resolver *
sol_flow_get_default_resolver(void)
{
    return sol_flow_default_resolver;
}

SOL_API int
sol_flow_resolve(
    const struct sol_flow_resolver *resolver,
    const char *id,
    const struct sol_flow_node_type **type,
    struct sol_flow_node_named_options *named_opts)
{
    int err;
    const char *type_name;
    const struct sol_flow_node_type *tmp_type;
    struct sol_flow_node_named_options tmp_named_opts = {};

    SOL_NULL_CHECK(id, -EINVAL);
    SOL_NULL_CHECK(type, -EINVAL);
    SOL_NULL_CHECK(named_opts, -EINVAL);

    if (!resolver)
        resolver = sol_flow_get_default_resolver();
    SOL_NULL_CHECK(resolver, -ENOENT);

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(resolver->api_version != SOL_FLOW_RESOLVER_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%" PRIu16 "', "
            "expected version is '%" PRIu16 "'",
            resolver->api_version, SOL_FLOW_RESOLVER_API_VERSION);
        return -EINVAL;
    }
#endif

    err = resolver->resolve(resolver->data, id, &tmp_type, &tmp_named_opts);
    if (err < 0) {
        type_name = sol_conffile_resolve_alias(sol_str_slice_from_str(id));
        if (!type_name) {
            SOL_DBG("Could not resolve module nor alias for id='%s'"
                " using resolver=%s", id, resolver->name);
            return -ENOENT;
        }
        err = resolver->resolve(resolver->data, type_name, &tmp_type,
            &tmp_named_opts);
        if (err < 0) {
            SOL_DBG("Could not resolve module for id='%s' (type='%s')"
                " using resolver=%s: %s", id, type_name, resolver->name,
                sol_util_strerrora(-err));
            return -ENOENT;
        }
    } else
        SOL_DBG("module for id='%s' resolved using resolver=%s",
            id, resolver->name);

    *type = tmp_type;
    *named_opts = tmp_named_opts;

    return 0;
}
