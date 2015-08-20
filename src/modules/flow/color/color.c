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

#include "color-gen.h"

#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

static int
color_luminance_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct sol_rgb *mdata = data;
    const struct sol_flow_node_type_color_luminance_rgb_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_COLOR_LUMINANCE_RGB_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_color_luminance_rgb_options *)options;

    *mdata = opts->color;

    mdata->red = mdata->red > mdata->red_max ? mdata->red_max : mdata->red;
    mdata->green = mdata->green > mdata->green_max ?
        mdata->green_max : mdata->green;
    mdata->blue = mdata->blue > mdata->blue_max ? mdata->blue_max : mdata->blue;

    return 0;
}

static int
color_luminance_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_rgb *mdata = data;
    struct sol_rgb out;
    struct sol_irange in_value;
    int r;
    int64_t val, diff;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.max <= in_value.min) {
        sol_flow_send_error_packet(node, EINVAL,
            "Max luminance %" PRId32 " must be greater than "
            "min %" PRId32 " luminance", in_value.max, in_value.min);
        return -EINVAL;
    }

    if (in_value.val > in_value.max || in_value.val < in_value.min) {
        sol_flow_send_error_packet(node, EINVAL,
            "Luminance value %" PRId32 " can't be out of luminance range: "
            "%" PRId32 " - %" PRId32 "",
            in_value.val, in_value.min, in_value.max);
        return -EINVAL;
    }

    out.red_max = mdata->red_max;
    out.green_max = mdata->green_max;
    out.blue_max = mdata->blue_max;

    diff = (int64_t)in_value.max - in_value.min;

    val = (int64_t)mdata->red * in_value.val / diff;
    out.red = abs(val);
    val = mdata->green * in_value.val / diff;
    out.green = abs(val);
    val = mdata->blue * in_value.val / diff;
    out.blue = abs(val);

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_COLOR_LUMINANCE_RGB__OUT__OUT,
        &out);
}

#include "color-gen.c"
