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

#include "float-editor.h"
#include "sol-flow/gtk.h"
#include "sol-types.h"
#include <stdint.h>
#include <stdarg.h>

static void
extract_value(struct gtk_common_data *mdata, double *values, size_t values_len, ...)
{
    size_t i;
    va_list ap;

    va_start(ap, values_len);

    for (i = 0; i < values_len; i++) {
        values[i] = gtk_spin_button_get_value
                (GTK_SPIN_BUTTON(g_object_get_data
                    (G_OBJECT(mdata->widget), va_arg(ap, const char *))));
    }

    va_end(ap);
}

void
send_direction_vector_output(struct gtk_common_data *mdata)
{
    double values[3];
    int r;

    extract_value(mdata, values, 3, "X", "Y", "Z");

    r = sol_flow_send_direction_vector_components_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_DIRECTION_VECTOR_EDITOR__OUT__OUT,
        values[0], values[1], values[2]);

    if (r < 0)
        SOL_WRN("Could not send the direction vector packet. Reason: %s",
            sol_util_strerrora(-r));
}

void
send_location_output(struct gtk_common_data *mdata)
{
    struct sol_location loc;
    double values[3];
    int r;

    extract_value(mdata, values, 3, "Latitude", "Longitude", "Altitude");
    loc.lat = values[0];
    loc.lon = values[1];
    loc.alt = values[2];

    r = sol_flow_send_location_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_LOCATION_EDITOR__OUT__OUT,
        &loc);

    if (r < 0)
        SOL_WRN("Could not send the location packet. Reason: %s",
            sol_util_strerrora(-r));
}

void
send_float_output(struct gtk_common_data *mdata)
{
    double value;
    struct sol_drange drange = SOL_DRANGE_INIT();
    int r;

    extract_value(mdata, &value, 1, "Float");
    drange.val = value;

    r = sol_flow_send_drange_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_FLOAT_EDITOR__OUT__OUT, &drange);

    if (r < 0)
        SOL_WRN("Could not send the location packet. Reason: %s",
            sol_util_strerrora(-r));
}

static void
spin_value_changed(GtkWidget *widget, gpointer data)
{
    struct gtk_common_data *mdata = data;
    const struct float_editor_note_type *float_node;

    float_node = (const struct float_editor_note_type *)sol_flow_node_get_type(mdata->node);
    float_node->send_output_packet(mdata);
}

static GtkWidget *
add_spin_and_label(struct gtk_common_data *mdata, GtkWidget *relative_to, const char *lbl_text)
{
    GtkWidget *spin, *lbl;

    spin = gtk_spin_button_new_with_range(-INT64_MAX, INT64_MAX, 0.0001);

    if (!relative_to)
        gtk_grid_attach(GTK_GRID(mdata->widget), spin, 0, 0, 20, 20);
    else
        gtk_grid_attach_next_to(GTK_GRID(mdata->widget), spin,
            relative_to, GTK_POS_RIGHT, 20, 20);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 0.0);
    g_signal_connect(spin, "value-changed",
        G_CALLBACK(spin_value_changed), mdata);
    g_object_set_data(G_OBJECT(mdata->widget), lbl_text, spin);
    gtk_widget_show(spin);

    lbl = gtk_label_new(lbl_text);
    gtk_grid_attach_next_to(GTK_GRID(mdata->widget), lbl, spin,
        GTK_POS_TOP, 20, 20);
    gtk_widget_show(lbl);

    return spin;
}

void
direction_vector_setup(struct gtk_common_data *mdata)
{
    GtkWidget *spin;

    spin = add_spin_and_label(mdata, NULL, "X");
    spin = add_spin_and_label(mdata, spin, "Y");
    spin = add_spin_and_label(mdata, spin, "Z");
}

void
location_setup(struct gtk_common_data *mdata)
{
    GtkWidget *spin;

    spin = add_spin_and_label(mdata, NULL, "Latitude");
    spin = add_spin_and_label(mdata, spin, "Longitude");
    spin = add_spin_and_label(mdata, spin, "Altitude");
}

void
float_setup(struct gtk_common_data *mdata)
{
    add_spin_and_label(mdata, NULL, "Float");
}

static int
float_editor_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{
    const struct float_editor_note_type *float_node;
    GtkWidget *grid;

    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    g_object_set(grid, "halign", GTK_ALIGN_CENTER, NULL);
    mdata->widget = grid;

    float_node = (const struct float_editor_note_type *)sol_flow_node_get_type(mdata->node);
    float_node->setup_widget(mdata);

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(float_editor);
