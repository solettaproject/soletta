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

#include "sol-flow/color.h"

#include "sol-flow-internal.h"

#include <sol-util-internal.h>
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
    out.red = llabs(val);
    val = mdata->green * in_value.val / diff;
    out.green = llabs(val);
    val = mdata->blue * in_value.val / diff;
    out.blue = llabs(val);

    return sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_COLOR_LUMINANCE_RGB__OUT__OUT,
        &out);
}

#include "color-gen.c"
