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

#include "test.h"
#include "sol-vector.h"
#include "sol-util.h"
#include "sol-flow-packet.h"

DEFINE_TEST(test_composed_type);

static void
test_composed_type(void)
{
    const struct sol_flow_packet_type *types[] =
    { SOL_FLOW_PACKET_TYPE_BOOLEAN, SOL_FLOW_PACKET_TYPE_STRING,
      SOL_FLOW_PACKET_TYPE_IRANGE, NULL };
    const struct sol_flow_packet_type *types2[] =
    { SOL_FLOW_PACKET_TYPE_BOOLEAN, SOL_FLOW_PACKET_TYPE_STRING, NULL };
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
