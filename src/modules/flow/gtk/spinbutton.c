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

#include "spinbutton.h"
#include "sol-flow/gtk.h"
#include "sol-flow-internal.h"

static void
on_spinbutton_changed(GtkSpinButton *spin, gpointer data)
{
    struct gtk_common_data *mdata = data;
    GtkAdjustment *adj = gtk_spin_button_get_adjustment(spin);
    struct sol_irange val;

    val.val = gtk_spin_button_get_value(spin);
    val.min = (int)gtk_adjustment_get_lower(adj);
    val.max = (int)gtk_adjustment_get_upper(adj);
    val.step = (int)gtk_adjustment_get_step_increment(adj);

    sol_flow_send_irange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_SPINBUTTON__OUT__OUT,
        &val);
}

static int
spinbutton_setup(struct gtk_common_data *data, const struct sol_flow_node_options *options)
{
    struct sol_irange_spec range;
    int32_t value;

    struct gtk_common_data *mdata = (struct gtk_common_data *)data;
    const struct sol_flow_node_type_gtk_spinbutton_options *opts =
        (const struct sol_flow_node_type_gtk_spinbutton_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_GTK_SPINBUTTON_OPTIONS_API_VERSION, -EINVAL);

    range = opts->range;
    value = opts->value;

    if (range.min > range.max) {
        SOL_WRN("invalid range min=%d max=%d for spinbutton id=%s\n",
            range.min, range.max, sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    if (value < range.min || value > range.max) {
        SOL_WRN("invalid value min=%d max=%d val=%d for spinbutton id=%s\n",
            range.min, range.max, value, sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    if (range.step <= 0) {
        SOL_WRN("invalid step=%d for spinbutton id=%s\n",
            range.step, sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    mdata->widget = gtk_spin_button_new_with_range(range.min, range.max, range.step);
    g_signal_connect(mdata->widget, "value-changed", G_CALLBACK(on_spinbutton_changed), mdata);
    g_object_set(mdata->widget, "hexpand", true, NULL);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mdata->widget), value);

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(spinbutton);
