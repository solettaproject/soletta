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

#include "slider.h"
#include "sol-flow/gtk.h"
#include "sol-flow-internal.h"

static void
on_slider_changed(GtkRange *range, gpointer data)
{
    struct gtk_common_data *mdata = data;
    GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(mdata->widget));
    struct sol_irange val;

    val.val = gtk_range_get_value(GTK_RANGE(mdata->widget));
    val.min = (int)gtk_adjustment_get_lower(adj);
    val.max = (int)gtk_adjustment_get_upper(adj);
    val.step = (int)gtk_adjustment_get_step_increment(adj);

    sol_flow_send_irange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_SLIDER__OUT__OUT,
        &val);
}

static int
slider_setup(struct gtk_common_data *data,
    const struct sol_flow_node_options *options)
{
    int min = 0;
    int max = 100;
    int step = 1;

    struct gtk_common_data *mdata = (struct gtk_common_data *)data;
    const struct sol_flow_node_type_gtk_slider_options *opts =
        (const struct sol_flow_node_type_gtk_slider_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GTK_SLIDER_OPTIONS_API_VERSION,
        -EINVAL);

    min = opts->range.min;
    max = opts->range.max;
    step = opts->range.step;

    if (min > max) {
        SOL_WRN("invalid range min=%d max=%d for slider id=%s\n",
            min, max, sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    if (step <= 0) {
        SOL_WRN("invalid step=%d for slider id=%s\n",
            step, sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    mdata->widget = gtk_scale_new_with_range
            (GTK_ORIENTATION_HORIZONTAL, min, max, step);
    g_signal_connect(mdata->widget, "value-changed",
        G_CALLBACK(on_slider_changed), mdata);
    g_object_set(mdata->widget, "hexpand", true, NULL);

    // GtkScale natural size is too small, give it a better default.
    gtk_widget_set_size_request(mdata->widget, 300, -1);

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(slider);
