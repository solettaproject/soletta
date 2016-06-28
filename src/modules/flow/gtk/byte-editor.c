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

#include "byte-editor.h"
#include "sol-flow/gtk.h"

#define INT_VALUE_MAX (0xF)
static const char *BIT_POSITION_KEY = "bit_position";

static void
on_byte_editor_clicked(GtkWidget *widget, gpointer data)
{
    struct gtk_common_data *mdata = data;
    unsigned char result = 0;
    GList *node;

    for (node = gtk_container_get_children(GTK_CONTAINER(mdata->widget));
        node != NULL; node = node->next) {

        GtkWidget *button = node->data;
        uintptr_t bit = (intptr_t)g_object_get_data(G_OBJECT(button),
            BIT_POSITION_KEY);
        unsigned char v = gtk_toggle_button_get_active
                (GTK_TOGGLE_BUTTON(button));
        result |= v << bit;
    }

    sol_flow_send_byte_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_GTK_BYTE_EDITOR__OUT__OUT,
        result);
}

static int
byte_editor_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{
    GtkWidget *box;
    uintptr_t bit;
    char buf[2];

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    g_object_set(box, "halign", GTK_ALIGN_CENTER, NULL);

    buf[1] = '\0';
    for (bit = 0; bit < 8; bit++) {
        GtkWidget *button;

        buf[0] = bit + '0';
        button = gtk_toggle_button_new_with_label(buf);
        g_object_set_data(G_OBJECT(button), BIT_POSITION_KEY, (gpointer)bit);
        gtk_box_pack_end(GTK_BOX(box), button, false, false, 0);
        g_signal_connect(button, "toggled",
            G_CALLBACK(on_byte_editor_clicked), mdata);
        gtk_widget_show(button);
    }

    mdata->widget = box;
    return 0;
}

#undef INT_VALUE_MAX

DEFINE_DEFAULT_OPEN_CLOSE(byte_editor);
