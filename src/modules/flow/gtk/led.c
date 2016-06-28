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

#include <math.h>

#include "led.h"
#include "sol-flow/gtk.h"
#include "sol-types.h"
#include "sol-flow-internal.h"

#define LED_VIEW_DIMENSION (50)
#define RGB_VALUE_MAX (255)

static void
do_drawing(cairo_t *cr, struct gtk_led_data *mdata)
{
    float r, g, b;

    cairo_set_line_width(cr, 1);
    cairo_arc(cr, LED_VIEW_DIMENSION / 2.0,
        LED_VIEW_DIMENSION / 2.0, LED_VIEW_DIMENSION / 3.0, 0, 2 * M_PI);
    cairo_stroke_preserve(cr);

    r = mdata->on ? (float)mdata->r / RGB_VALUE_MAX : 0;
    g = mdata->on ? (float)mdata->g / RGB_VALUE_MAX : 0;
    b = mdata->on ? (float)mdata->b / RGB_VALUE_MAX : 0;

    cairo_set_source_rgb(cr, r, g, b);
    cairo_fill(cr);
}

static gboolean
on_draw_event(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    do_drawing(cr, data);

    return FALSE;
}

static int
led_setup(struct gtk_common_data *data,
    const struct sol_flow_node_options *options)
{
    struct gtk_led_data *mdata = (struct gtk_led_data *)data;
    const struct sol_flow_node_type_gtk_led_options *opts =
        (const struct sol_flow_node_type_gtk_led_options *)options;
    struct sol_rgb color;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GTK_LED_OPTIONS_API_VERSION,
        -EINVAL);

    color = opts->rgb;
    if (sol_rgb_set_max(&color, 255) < 0) {
        SOL_WRN("Invalid color");
        return -EINVAL;
    }

    mdata->on = true;
    mdata->r = color.red;
    mdata->g = color.green;
    mdata->b = color.blue;

    mdata->base.widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(mdata->base.widget,
        LED_VIEW_DIMENSION, LED_VIEW_DIMENSION);
    g_signal_connect(G_OBJECT(mdata->base.widget),
        "draw", G_CALLBACK(on_draw_event), mdata);
    g_object_set(mdata->base.widget, "halign", GTK_ALIGN_CENTER, NULL);

    return 0;
}

int
gtk_led_in_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_led_data *mdata = data;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOL) {
        bool value;
        int r = sol_flow_packet_get_bool(packet, &value);
        SOL_INT_CHECK(r, < 0, r);

        mdata->on = value;
        gtk_widget_queue_draw(mdata->base.widget);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t value;
        int r = sol_flow_packet_get_irange_value(packet, &value);
        SOL_INT_CHECK(r, < 0, r);

        mdata->on = true;
        mdata->b = value & 0xFF;
        mdata->g = (value & 0xFF00) >> 8;
        mdata->r = (value & 0xFF0000) >> 16;

        gtk_widget_queue_draw(mdata->base.widget);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_RGB) {
        struct sol_rgb value;
        int r = sol_flow_packet_get_rgb(packet, &value);
        SOL_INT_CHECK(r, < 0, r);

        if (sol_rgb_set_max(&value, RGB_VALUE_MAX) < 0)
            return -EINVAL;

        mdata->on = true;
        mdata->r = value.red;
        mdata->g = value.green;
        mdata->b = value.blue;

        gtk_widget_queue_draw(mdata->base.widget);
    } else {
        SOL_WRN("Unsupported packet=%p type=%p (%s)",
            packet, sol_flow_packet_get_type(packet), sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(led);
