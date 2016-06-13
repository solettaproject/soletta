/*
 * This file is part of the Soletta™ Project
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

#include "rgb-editor.h"
#include "sol-flow/gtk.h"
#include "sol-types.h"

#define COLOR_VALUE_MIN (0)
#define COLOR_VALUE_MAX (255)

static void
on_value_changed(GtkWidget *widget, gpointer data)
{
    GdkRGBA rgba;
    uint8_t red, green, blue;
    struct gtk_common_data *mdata = data;
    struct sol_rgb color;

    red = gtk_spin_button_get_value_as_int
            (GTK_SPIN_BUTTON(g_object_get_data
                (G_OBJECT(mdata->widget), "spin_r")));
    green = gtk_spin_button_get_value_as_int
            (GTK_SPIN_BUTTON(g_object_get_data
                (G_OBJECT(mdata->widget), "spin_g")));
    blue = gtk_spin_button_get_value_as_int
            (GTK_SPIN_BUTTON(g_object_get_data
                (G_OBJECT(mdata->widget), "spin_b")));

    rgba.red = (gdouble)red / COLOR_VALUE_MAX;
    rgba.green = (gdouble)green / COLOR_VALUE_MAX;
    rgba.blue = (gdouble)blue / COLOR_VALUE_MAX;
    rgba.alpha = 1.0;

    gtk_color_chooser_set_rgba
        (GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(mdata->widget),
        "rgb_button")), &rgba);

    color.red = red;
    color.red_max = COLOR_VALUE_MAX;
    color.green = green;
    color.green_max = COLOR_VALUE_MAX;
    color.blue = blue;
    color.blue_max = COLOR_VALUE_MAX;

    sol_flow_send_rgb_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_RGB_EDITOR__OUT__OUT,
        &color);
}

static void
on_rgb_button_value_changed(GtkColorButton *widget, gpointer data)
{
    GdkRGBA rgba;
    struct gtk_common_data *mdata = data;

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), &rgba);

    gtk_spin_button_set_value
        (GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(mdata->widget), "spin_r")),
        rgba.red * COLOR_VALUE_MAX);
    gtk_spin_button_set_value
        (GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(mdata->widget), "spin_g")),
        rgba.green * COLOR_VALUE_MAX);
    gtk_spin_button_set_value
        (GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(mdata->widget), "spin_b")),
        rgba.blue * COLOR_VALUE_MAX);
}

static int
rgb_editor_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{
    GtkWidget *grid;
    GtkWidget *red_spin;
    GtkWidget *red_label;
    GtkWidget *green_spin;
    GtkWidget *green_label;
    GtkWidget *blue_spin;
    GtkWidget *blue_label;
    GtkWidget *rgb_button;

    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    g_object_set(grid, "halign", GTK_ALIGN_CENTER, NULL);

    red_spin = gtk_spin_button_new_with_range
            (COLOR_VALUE_MIN, COLOR_VALUE_MAX, 1);
    gtk_grid_attach(GTK_GRID(grid), red_spin, 0, 0, 20, 20);
    g_signal_connect(red_spin, "value-changed",
        G_CALLBACK(on_value_changed), mdata);
    g_object_set_data(G_OBJECT(grid), "spin_r", red_spin);
    gtk_widget_show(red_spin);

    red_label = gtk_label_new("Red");
    gtk_grid_attach_next_to(GTK_GRID(grid), red_label, red_spin,
        GTK_POS_TOP, 20, 20);
    gtk_widget_show(red_label);

    green_spin = gtk_spin_button_new_with_range
            (COLOR_VALUE_MIN, COLOR_VALUE_MAX, 1);
    gtk_grid_attach_next_to(GTK_GRID(grid), green_spin, red_spin,
        GTK_POS_RIGHT, 20, 20);
    g_signal_connect(green_spin, "value-changed",
        G_CALLBACK(on_value_changed), mdata);
    g_object_set_data(G_OBJECT(grid), "spin_g", green_spin);
    gtk_widget_show(green_spin);

    green_label = gtk_label_new("Green");
    gtk_grid_attach_next_to(GTK_GRID(grid), green_label, green_spin,
        GTK_POS_TOP, 20, 20);
    gtk_widget_show(green_label);

    blue_spin = gtk_spin_button_new_with_range
            (COLOR_VALUE_MIN, COLOR_VALUE_MAX, 1);
    gtk_grid_attach_next_to(GTK_GRID(grid), blue_spin, green_spin,
        GTK_POS_RIGHT, 20, 20);
    g_signal_connect(blue_spin, "value-changed",
        G_CALLBACK(on_value_changed), mdata);
    g_object_set_data(G_OBJECT(grid), "spin_b", blue_spin);
    gtk_widget_show(blue_spin);

    blue_label = gtk_label_new("Blue");
    gtk_grid_attach_next_to(GTK_GRID(grid), blue_label, blue_spin,
        GTK_POS_TOP, 20, 20);
    gtk_widget_show(blue_label);

    rgb_button = gtk_color_button_new();
    gtk_grid_attach_next_to(GTK_GRID(grid), rgb_button, blue_spin,
        GTK_POS_RIGHT, 20, 20);
    g_signal_connect(rgb_button, "color-set",
        G_CALLBACK(on_rgb_button_value_changed), mdata);
    g_object_set_data(G_OBJECT(grid), "rgb_button", rgb_button);
    gtk_widget_show(rgb_button);

    mdata->widget = grid;
    return 0;
}

#undef COLOR_VALUE_MIN
#undef COLOR_VALUE_MAX

DEFINE_DEFAULT_OPEN_CLOSE(rgb_editor);
