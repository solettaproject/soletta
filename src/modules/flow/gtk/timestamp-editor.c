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

#include "timestamp-editor.h"
#include "sol-flow/gtk.h"
#include <time.h>

static void
send_timestamp_packet(GtkWidget *widget, gpointer data)
{
    struct gtk_common_data *mdata = data;
    GtkWidget *calendar;
    struct timespec spec = { 0 };
    struct tm tm = { 0 };
    int r;

    tm.tm_hour = gtk_spin_button_get_value_as_int
            (GTK_SPIN_BUTTON(g_object_get_data
                (G_OBJECT(mdata->widget), "hour_spin")));
    tm.tm_min = gtk_spin_button_get_value_as_int
            (GTK_SPIN_BUTTON(g_object_get_data
                (G_OBJECT(mdata->widget), "minute_spin")));
    tm.tm_sec = gtk_spin_button_get_value_as_int
            (GTK_SPIN_BUTTON(g_object_get_data
                (G_OBJECT(mdata->widget), "second_spin")));

    calendar = g_object_get_data(G_OBJECT(mdata->widget), "calendar");

    gtk_calendar_get_date(GTK_CALENDAR(calendar),
        (guint *)&tm.tm_year, (guint *)&tm.tm_mon, (guint *)&tm.tm_mday);

    tm.tm_year -= 1900;
    spec.tv_sec = mktime(&tm);
    r = sol_flow_send_timestamp_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_TIMESTAMP_EDITOR__OUT__OUT, &spec);

    if (r < 0)
        SOL_WRN("Could not send the direction vector packet. Reason: %s",
            sol_util_strerrora(-r));
}

static int
timestamp_editor_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{
    GtkWidget *calendar, *grid, *lbl, *hour_spin, *second_spin, *minute_spin;
    time_t now;
    struct tm tm;

    now = time(NULL);
    tzset();

    SOL_NULL_CHECK(localtime_r(&now, &tm), -EINVAL);

    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    g_object_set(grid, "halign", GTK_ALIGN_CENTER, NULL);

    calendar = gtk_calendar_new();
    gtk_grid_attach(GTK_GRID(grid), calendar, 0, 0, 20, 20);
    g_signal_connect(calendar, "day-selected",
        G_CALLBACK(send_timestamp_packet), mdata);
    g_object_set_data(G_OBJECT(grid), "calendar", calendar);
    gtk_widget_show(calendar);

    hour_spin = gtk_spin_button_new_with_range(0, 23, 1);
    gtk_grid_attach_next_to(GTK_GRID(grid), hour_spin, calendar,
        GTK_POS_RIGHT, 2, 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(hour_spin), tm.tm_hour);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(hour_spin), TRUE);
    g_signal_connect(hour_spin, "value-changed",
        G_CALLBACK(send_timestamp_packet), mdata);
    g_object_set_data(G_OBJECT(grid), "hour_spin", hour_spin);
    gtk_widget_show(hour_spin);

    lbl = gtk_label_new("Hour");
    gtk_grid_attach_next_to(GTK_GRID(grid), lbl, hour_spin,
        GTK_POS_TOP, 2, 2);
    gtk_widget_show(lbl);

    lbl = gtk_label_new(":");
    gtk_grid_attach_next_to(GTK_GRID(grid), lbl, hour_spin,
        GTK_POS_RIGHT, 2, 2);
    gtk_widget_show(lbl);

    minute_spin = gtk_spin_button_new_with_range(0, 59, 1);
    gtk_grid_attach_next_to(GTK_GRID(grid), minute_spin, lbl,
        GTK_POS_RIGHT, 2, 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(minute_spin), tm.tm_min);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(minute_spin), TRUE);
    g_signal_connect(minute_spin, "value-changed",
        G_CALLBACK(send_timestamp_packet), mdata);
    g_object_set_data(G_OBJECT(grid), "minute_spin", minute_spin);
    gtk_widget_show(minute_spin);

    lbl = gtk_label_new("Minute");
    gtk_grid_attach_next_to(GTK_GRID(grid), lbl, minute_spin,
        GTK_POS_TOP, 2, 2);
    gtk_widget_show(lbl);

    lbl = gtk_label_new(":");
    gtk_grid_attach_next_to(GTK_GRID(grid), lbl, minute_spin,
        GTK_POS_RIGHT, 2, 2);
    gtk_widget_show(lbl);

    second_spin = gtk_spin_button_new_with_range(0, 59, 1);
    gtk_grid_attach_next_to(GTK_GRID(grid), second_spin, lbl,
        GTK_POS_RIGHT, 2, 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(second_spin), tm.tm_sec);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(second_spin), TRUE);
    g_signal_connect(second_spin, "value-changed",
        G_CALLBACK(send_timestamp_packet), mdata);
    g_object_set_data(G_OBJECT(grid), "second_spin", second_spin);
    gtk_widget_show(second_spin);

    lbl = gtk_label_new("Second");
    gtk_grid_attach_next_to(GTK_GRID(grid), lbl, second_spin,
        GTK_POS_TOP, 2, 2);
    gtk_widget_show(lbl);

    mdata->widget = grid;
    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(timestamp_editor);
