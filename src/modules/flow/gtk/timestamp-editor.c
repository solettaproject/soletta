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
