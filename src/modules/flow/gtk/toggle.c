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

#include "toggle.h"
#include "sol-flow/gtk.h"

static void
on_toggle_changed(GtkRange *range, gpointer data)
{
    bool value;
    struct gtk_common_data *mdata = data;

    value =  gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mdata->widget));

    sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_TOGGLE__OUT__OUT,
        value);
}

static int
toggle_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{
    mdata->widget = gtk_toggle_button_new_with_label("    ");
    g_signal_connect(mdata->widget, "toggled",
        G_CALLBACK(on_toggle_changed), mdata);
    g_object_set(mdata->widget, "halign", GTK_ALIGN_CENTER, NULL);

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(toggle);
