/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

/**
 * @file arctan-node.c
 *
 * This file contains the implementation of custom node types. It uses
 * generated boilerplate from JSON which later modified to
 * perform arctangent algorithm.
 *
 * The instruction used to generate custom node template code from
 * arctan-spec.json which eventually being included into arctan-node.c:
 *
 *    $ sol-flow-node-type-gen.py \
 *            --prefix sol_flow_node_type --genspec-schema \
 *            /usr/share/soletta/flow/schemas/node-type-genspec.schema \
 *            arctan-spec.json \
 *            arctan-spec-gen.h \
 *            arctan-spec-gen.c \
 *            arctan-spec-gen.json
 *
 * The instruction to compile this custom node:
 *    $ gcc `pkg-config --libs --cflags soletta` \
 *          -DSOL_FLOW_NODE_TYPE_MODULE_EXTERNAL=1 \
 *          -shared -fPIC arctan-node.c -o arctangent.so
 *
 * The location to install arctangent.so:
 *    $ install -m 0755 arctangent.so /usr/lib/soletta/modules/flow/
 */

#include <errno.h>
#include <stdio.h>
#include <math.h>

#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-flow.h>

static int
logic_process(struct sol_flow_node *node, void *data, uint16_t port,
              uint16_t conn_id, const struct sol_flow_packet *packet);

/* This file is generated using sol-flow-node-type-gen.py, see above */
#include "arctan-spec-gen.c"

/**
 * Logic:
 *
 * This node will receive a float input and then calculate the
 * value of arctangent of it to be feed at the output port.
 * Formula for arctangent power series and reciprocal arguments
 * are as follows:
 *
 *     arctangent(x) = summation[(-1)^n * (z^(2n+1) / 2n+1)]
 *
 *     arctangent(x) = -(PI/2) - arctangent(1/x)
 *
 * Detailed explanation can be refered at :
 *
 *     https://en.wikipedia.org/wiki/Inverse_trigonometric_functions
 *
 * This node contains no data, it will recompute everything based on
 * the last received packet, thus there is no node private data, open
 * or close methods.
 */
static int
logic_process(struct sol_flow_node *node, void *data, uint16_t port,
              uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange d_value;
    double arctan = 0;

    r = sol_flow_packet_get_drange(packet, &d_value);
    SOL_INT_CHECK(r, < 0, r);

    /*
     * atan() is a fuction in math.h library to calculate the
     * value of arctangent in radian
     */
    if (isfinite(d_value.val)) {
        arctan = atan(d_value.val) * 180 / M_PI;
    } else {
        arctan = 90;
    }

    r = sol_flow_send_drange_value_packet(node,
                                          SOL_FLOW_NODE_TYPE_ARCTANGENT_LOGIC__OUT__OUT,
                                          arctan);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}
