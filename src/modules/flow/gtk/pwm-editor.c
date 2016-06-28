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

#include "pwm-editor.h"
#include "sol-flow/gtk.h"
#include "sol-flow-internal.h"

static void
on_pwm_editor_toggle_changed(GtkToggleButton *toggle, gpointer data)
{
    bool value;
    struct gtk_common_data *mdata = data;

    value = gtk_toggle_button_get_active(toggle);

    sol_flow_send_bool_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_PWM_EDITOR__OUT__ENABLED,
        value);
}

static void
on_pwm_editor_spin_changed(GtkSpinButton *spin, gpointer data, uint16_t port)
{
    struct gtk_common_data *mdata = data;
    GtkAdjustment *adj = gtk_spin_button_get_adjustment(spin);
    struct sol_irange val;

    val.val = gtk_spin_button_get_value(spin);
    val.min = (int)gtk_adjustment_get_lower(adj);
    val.max = (int)gtk_adjustment_get_upper(adj);
    val.step = (int)gtk_adjustment_get_step_increment(adj);

    sol_flow_send_irange_packet(mdata->node, port, &val);
}

static void
on_pwm_editor_period_spin_changed(GtkSpinButton *spin, gpointer data)
{
    on_pwm_editor_spin_changed(spin, data, SOL_FLOW_NODE_TYPE_GTK_PWM_EDITOR__OUT__PERIOD);
}

static void
on_pwm_editor_duty_cycle_spin_changed(GtkSpinButton *spin, gpointer data)
{
    on_pwm_editor_spin_changed(spin, data, SOL_FLOW_NODE_TYPE_GTK_PWM_EDITOR__OUT__DUTY_CYCLE);
}

static int
pwm_editor_setup(struct gtk_common_data *mdata, const struct sol_flow_node_options *options)
{
    int range_min = 0;
    int range_max = 100;
    int range_step = 1;

    GtkWidget *grid;
    GtkWidget *enable_toggle;
    GtkWidget *period_range_spin;
    GtkWidget *period_range_label;
    GtkWidget *duty_cycle_range_spin;
    GtkWidget *duty_cycle_range_label;

    const struct sol_flow_node_type_gtk_pwm_editor_options *opts =
        (const struct sol_flow_node_type_gtk_pwm_editor_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GTK_PWM_EDITOR_OPTIONS_API_VERSION,
        -EINVAL);

    range_min = opts->range.min;
    range_max = opts->range.max;
    range_step = opts->range.step;

    if (range_min > range_max) {
        SOL_WRN("invalid range min=%d max=%d for pwm-editor id=%s\n", range_min, range_max,
            sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    if (range_step <= 0) {
        SOL_WRN("invalid range step=%d for pwm-editor id=%s\n", range_step, sol_flow_node_get_id(mdata->node));
        return -EINVAL;
    }

    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    g_object_set(grid, "halign", GTK_ALIGN_CENTER, NULL);

    enable_toggle = gtk_toggle_button_new_with_label("Enable");
    gtk_grid_attach(GTK_GRID(grid), enable_toggle, 0, 0, 20, 20);
    g_signal_connect(enable_toggle, "toggled", G_CALLBACK(on_pwm_editor_toggle_changed), mdata);
    gtk_widget_show(enable_toggle);

    period_range_spin = gtk_spin_button_new_with_range(range_min, range_max, range_step);
    gtk_grid_attach_next_to(GTK_GRID(grid), period_range_spin, enable_toggle, GTK_POS_RIGHT, 20, 20);
    g_signal_connect(period_range_spin, "value-changed", G_CALLBACK(on_pwm_editor_period_spin_changed), mdata);
    gtk_widget_show(period_range_spin);

    period_range_label = gtk_label_new("Period Range");
    gtk_widget_show(period_range_label);
    gtk_grid_attach_next_to(GTK_GRID(grid), period_range_label, period_range_spin, GTK_POS_TOP, 20, 20);

    duty_cycle_range_spin = gtk_spin_button_new_with_range(range_min, range_max, range_step);
    gtk_grid_attach_next_to(GTK_GRID(grid), duty_cycle_range_spin, period_range_spin, GTK_POS_RIGHT, 20, 20);
    g_signal_connect(duty_cycle_range_spin, "value-changed", G_CALLBACK(on_pwm_editor_duty_cycle_spin_changed), mdata);
    gtk_widget_show(duty_cycle_range_spin);

    duty_cycle_range_label = gtk_label_new("Duty Cycle Range");
    gtk_widget_show(duty_cycle_range_label);
    gtk_grid_attach_next_to(GTK_GRID(grid), duty_cycle_range_label, duty_cycle_range_spin, GTK_POS_TOP, 20, 20);

    mdata->widget = grid;
    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(pwm_editor);
