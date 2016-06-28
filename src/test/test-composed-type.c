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

#include "test.h"
#include "sol-vector.h"
#include "sol-util-internal.h"
#include "sol-flow-packet.h"

DEFINE_TEST(test_composed_type);

static void
test_composed_type(void)
{
    const struct sol_flow_packet_type *types[] =
    { SOL_FLOW_PACKET_TYPE_BOOL, SOL_FLOW_PACKET_TYPE_STRING,
      SOL_FLOW_PACKET_TYPE_IRANGE, NULL };
    const struct sol_flow_packet_type *types2[] =
    { SOL_FLOW_PACKET_TYPE_BOOL, SOL_FLOW_PACKET_TYPE_STRING, NULL };
    const struct sol_flow_packet_type *composed_type, *composed_type2,
    *composed_type3;
    bool is_composed;

    composed_type = sol_flow_packet_type_composed_new(NULL);
    ASSERT(!composed_type);

    composed_type = sol_flow_packet_type_composed_new(types);
    ASSERT(composed_type);

    composed_type2 = sol_flow_packet_type_composed_new(types);
    ASSERT(composed_type == composed_type2);

    composed_type3 = sol_flow_packet_type_composed_new(types2);
    ASSERT(composed_type != composed_type3);

    is_composed = sol_flow_packet_is_composed_type(composed_type);
    ASSERT(is_composed);

    is_composed = sol_flow_packet_is_composed_type(composed_type2);
    ASSERT(is_composed);

    is_composed = sol_flow_packet_is_composed_type(SOL_FLOW_PACKET_TYPE_DRANGE);
    ASSERT(!is_composed);
}

TEST_MAIN();
