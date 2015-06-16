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

#include "slider.h"
#include "gtk-gen.h"

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

    if (opts) {
        min = opts->range.min;
        max = opts->range.max;
        step = opts->range.step;
    }

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
