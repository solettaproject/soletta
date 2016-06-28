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

#include "led-7seg.h"
#include "sol-flow/gtk.h"

static void
set_min_size(GtkWidget *widget)
{
    GtkRequisition natural_size = {};
    int width = 100;

    gtk_widget_get_preferred_size(widget, NULL, &natural_size);

    if (natural_size.width > width)
        width = natural_size.width;

    gtk_widget_set_size_request(widget, width, natural_size.height);
}

static int
led_7seg_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{

    mdata->widget = gtk_label_new(NULL);
    g_object_set(mdata->widget, "halign", GTK_ALIGN_CENTER, NULL);
    set_min_size(mdata->widget);

    return 0;
}

int
gtk_led_7seg_value_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_common_data *mdata = data;
    int32_t val;
    char buf[32];
    int r;

    r = sol_flow_packet_get_irange_value(packet, &val);
    SOL_INT_CHECK(r, < 0, r);
    snprintf(buf, sizeof(buf), "%d", val);
    gtk_label_set_text(GTK_LABEL(mdata->widget), buf);

    return 0;
}

int
gtk_led_7seg_segments_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_common_data *mdata = data;
    char buf[9];
    unsigned char value;
    int r, i;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    for (i = 7; i >= 0; i--) {
        buf[i] = (value & 0x1) + '0';
        value >>= 1;
    }
    buf[8] = '\0';
    gtk_label_set_text(GTK_LABEL(mdata->widget), buf);

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(led_7seg);
