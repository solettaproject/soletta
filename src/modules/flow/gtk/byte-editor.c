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

#include "byte-editor.h"
#include "gtk-gen.h"

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
