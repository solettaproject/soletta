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

#include <math.h>

#include "led.h"
#include "gtk-gen.h"
#include "sol-types.h"

#define LED_VIEW_DIMENSION (50)
#define RGB_VALUE_MAX (255)

static void
do_drawing(cairo_t *cr, struct gtk_led_data *mdata)
{
    float r, g, b;

    cairo_set_line_width(cr, 1);
    cairo_arc(cr, LED_VIEW_DIMENSION / 2,
        LED_VIEW_DIMENSION / 2, LED_VIEW_DIMENSION / 3, 0, 2 * M_PI);
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

static bool
extract_rgb_option(const char *options, uint32_t *r, uint32_t *g, uint32_t *b)
{
    int items_scanned;

    if (!r || !g || !b)
        return false;

    items_scanned = sscanf(options, " %u | %u | %u", r, g, b);

    if (items_scanned != 3) {
        SOL_WRN("could not parse rgb option value '%s'", options);
        return false;
    }

    return true;
}

static int
led_setup(struct gtk_common_data *data,
    const struct sol_flow_node_options *options)
{
    uint32_t red, green, blue;
    struct gtk_led_data *mdata = (struct gtk_led_data *)data;
    const struct sol_flow_node_type_gtk_led_options *opts =
        (const struct sol_flow_node_type_gtk_led_options *)options;

    SOL_NULL_CHECK(options, -EINVAL);

    if (!extract_rgb_option(opts->rgb, &red, &green, &blue))
        return -EINVAL;

    mdata->r = (red > RGB_VALUE_MAX) ? RGB_VALUE_MAX : red;
    mdata->g = (green > RGB_VALUE_MAX) ? RGB_VALUE_MAX : green;
    mdata->b = (blue > RGB_VALUE_MAX) ? RGB_VALUE_MAX : blue;

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

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOLEAN) {
        bool value;
        int r = sol_flow_packet_get_boolean(packet, &value);
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
