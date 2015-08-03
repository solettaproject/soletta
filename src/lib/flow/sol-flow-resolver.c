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

#include <stddef.h>

#include "sol-flow-resolver.h"
#include "sol-flow-internal.h"
#include "sol-flow-buildopts.h"

SOL_API int
sol_flow_resolve(
    const struct sol_flow_resolver *resolver,
    const char *id,
    struct sol_flow_node_type const **type,
    const char ***opts_strv)
{
    const struct sol_flow_node_type *tmp_type = NULL;
    const char **tmp_opts_strv = NULL;
    int ret;

    SOL_NULL_CHECK(id, -EINVAL);
    SOL_NULL_CHECK(type, -EINVAL);
    SOL_NULL_CHECK(opts_strv, -EINVAL);

    if (!resolver)
        resolver = sol_flow_get_default_resolver();
    SOL_NULL_CHECK(resolver, -ENOENT);

    ret = resolver->resolve(resolver->data, id, &tmp_type, &tmp_opts_strv);
    if (ret != 0) {
        SOL_DBG("could not resolve module for id='%s' using resolver=%s",
            id, resolver->name);
        return -ENOENT;
    }

    *type = tmp_type;
    *opts_strv = tmp_opts_strv;
    return 0;
}

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
    const char ***opts_strv)
{
    struct find_type_ctx ctx = {};

    ctx.name = id;
    sol_flow_foreach_builtin_node_type(find_type_cb, &ctx);
    if (!ctx.type)
        return -ENOENT;
    *type = ctx.type;
    /* When resolving to a type, no options are set. */
    *opts_strv = NULL;
    return 0;
}

static const struct sol_flow_resolver builtins_resolver = {
    .api_version = SOL_FLOW_RESOLVER_API_VERSION,
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
