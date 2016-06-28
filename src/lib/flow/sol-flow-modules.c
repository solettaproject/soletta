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

#include <errno.h>

#include "sol-flow-internal.h"
#include "sol-modules.h"

SOL_API int
sol_flow_internal_get_node_type(const char *modname, const char *symbol, const struct sol_flow_node_type ***type)
{
    const struct sol_flow_node_type **ret;

    ret = sol_modules_get_symbol("flow", modname, symbol);
    if (!ret || !*ret)
        return -errno;

    *type = ret;
    return 0;
}

SOL_API int
sol_flow_internal_get_packet_type(const char *modname, const char *symbol, const struct sol_flow_packet_type **type)
{
    const struct sol_flow_packet_type **ret;

    ret = sol_modules_get_symbol("flow", modname, symbol);
    if (!ret || !*ret)
        return -errno;

    *type = *ret;
    return 0;
}
