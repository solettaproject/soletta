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

#include "pushbutton.h"
#include "sol-flow/gtk.h"

static void
on_pushbutton_pressed(GtkButton *button, gpointer data)
{
    struct gtk_common_data *mdata = data;

    sol_flow_send_bool_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_PUSHBUTTON__OUT__OUT, true);
    sol_flow_send_empty_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_PUSHBUTTON__OUT__PRESSED);
}

static void
on_pushbutton_released(GtkButton *button, gpointer data)
{
    struct gtk_common_data *mdata = data;

    sol_flow_send_bool_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_PUSHBUTTON__OUT__OUT, false);
    sol_flow_send_empty_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_PUSHBUTTON__OUT__RELEASED);
}

static int
pushbutton_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{
    mdata->widget = gtk_button_new_with_label("    ");
    g_signal_connect(mdata->widget, "pressed",
        G_CALLBACK(on_pushbutton_pressed), mdata);
    g_signal_connect(mdata->widget, "released",
        G_CALLBACK(on_pushbutton_released), mdata);
    g_object_set(mdata->widget, "halign", GTK_ALIGN_CENTER, NULL);

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(pushbutton);
