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

#include <stdbool.h>
#include <stdlib.h>

#include "window.h"
#include "common.h"
#include "sol-util-internal.h"

static const int LABEL_COLUMN = 0;
static const int WIDGET_COLUMN = 1;

struct window {
    GtkWidget *toplevel, *grid;
    int grid_height;
};

static void
on_destroy(GtkWidget *widget, gpointer data)
{
    struct window *w = data;

    w->toplevel = NULL;

    // Convenience for our examples to finish the program when the
    // window is closed. We don't have access to the program's
    // mainloop from the conffile-gtk module.
    kill(getpid(), SIGINT);
}

struct window *
window_new(void)
{
    struct window *w;
    GtkWidget *scrolled_win;

    gtk_init(NULL, NULL);
    w = calloc(1, sizeof(*w));
    SOL_NULL_CHECK(w, NULL);

    w->toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(w->toplevel, "destroy", G_CALLBACK(on_destroy), w);
    gtk_window_set_title(GTK_WINDOW(w->toplevel), "Soletta");

    scrolled_win = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(w->toplevel), scrolled_win);

    w->grid = gtk_grid_new();
    g_object_set(w->grid,
        "margin", 10,
        "row-spacing", 10,
        "column-spacing", 10,
        "hexpand", true,
        NULL);
    gtk_container_add(GTK_CONTAINER(scrolled_win), w->grid);
    w->grid_height = 0;

    gtk_widget_show(w->grid);
    gtk_widget_show(scrolled_win);
    gtk_widget_show(w->toplevel);

    return w;
}

void
window_free(struct window *w)
{
    if (w->toplevel)
        gtk_widget_destroy(w->toplevel);
    free(w);
}

static void
reset_preferred_size(struct window *w)
{
    GtkRequisition natural_size = {};
    int height = 600;

    /* Scrolled window doesn't seem to take its content' size into
     * account, so we propagate ourselves. */
    gtk_widget_get_preferred_size(w->grid, NULL, &natural_size);

    if (natural_size.height < height)
        height = natural_size.height;

    gtk_widget_set_size_request(w->toplevel, natural_size.width, height);
}

void
window_add_widget(struct window *w, GtkWidget *widget, const char *id)
{
    GtkWidget *label = gtk_label_new(id);

    gtk_grid_attach(GTK_GRID(w->grid), label,
        LABEL_COLUMN, w->grid_height, 1, 1);
    gtk_grid_attach(GTK_GRID(w->grid), widget,
        WIDGET_COLUMN, w->grid_height, 1, 1);
    gtk_widget_show(label);
    gtk_widget_show(widget);
    w->grid_height++;

    reset_preferred_size(w);
}

void
window_del_widget(struct window *w, GtkWidget *widget)
{
    int i;

    if (!w->toplevel)
        return;
    for (i = 0; i < w->grid_height; i++) {
        GtkWidget *current = gtk_grid_get_child_at(GTK_GRID(w->grid),
            WIDGET_COLUMN, i);
        if (current == widget) {
            gtk_grid_remove_row(GTK_GRID(w->grid), i);
            w->grid_height--;
            return;
        }
    }
}
