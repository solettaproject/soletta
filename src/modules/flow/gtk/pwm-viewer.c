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

#include "pwm-viewer.h"
#include "sol-flow/gtk.h"

#define CHART_HEIGHT 42
#define CHART_WIDTH 340
#define CHART_X 30
#define CHART_Y 8
#define N_WAVES 5
#define WAVE_OFFSET (CHART_WIDTH - CHART_X) / N_WAVES

static void
do_drawing_pwm(cairo_t *cr, struct gtk_pwm_viewer_data *pwm)
{
    float duty_cycle_percent = pwm->period > 0 ? (double)pwm->duty_cycle / pwm->period : 0;

    cairo_set_font_size(cr, 10);

    cairo_move_to(cr, 0, CHART_Y);
    cairo_show_text(cr, "high");

    cairo_move_to(cr, 0, CHART_HEIGHT);
    cairo_show_text(cr, "low");
    cairo_stroke(cr);

    cairo_set_line_width(cr, 1);
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);

    if (!pwm->enabled || duty_cycle_percent <= 0.0) {
        cairo_line_to(cr, CHART_X, CHART_HEIGHT);
        cairo_line_to(cr, CHART_WIDTH, CHART_HEIGHT);
    } else if (duty_cycle_percent >= 1.0) {
        cairo_line_to(cr, CHART_X, CHART_Y);
        cairo_line_to(cr, CHART_WIDTH, CHART_Y);
    } else {
        int i;
        for (i = 0; i < N_WAVES; i++) {
            cairo_line_to(cr, CHART_X + WAVE_OFFSET * i, CHART_HEIGHT);
            cairo_line_to(cr, CHART_X + WAVE_OFFSET * i, 1);
            cairo_line_to(cr, CHART_X + WAVE_OFFSET * duty_cycle_percent + WAVE_OFFSET * i, 1);
            cairo_line_to(cr, CHART_X + WAVE_OFFSET * duty_cycle_percent + WAVE_OFFSET * i, CHART_HEIGHT);
            cairo_line_to(cr, CHART_X + WAVE_OFFSET * (i + 1), CHART_HEIGHT);
        }

        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 1, 0.6, 0);
        cairo_line_to(cr, CHART_X, -(duty_cycle_percent - 1) * CHART_HEIGHT);
        cairo_line_to(cr, CHART_WIDTH, -(duty_cycle_percent - 1) * CHART_HEIGHT);
        cairo_move_to(cr, CHART_WIDTH, -(duty_cycle_percent - 1) * CHART_HEIGHT + CHART_Y);
        cairo_show_text(cr, " v average");
    }
    cairo_stroke_preserve(cr);
}

static gboolean
on_draw_event_pwm(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    do_drawing_pwm(cr, data);

    return false;
}

static int
pwm_viewer_setup(struct gtk_common_data *data, const struct sol_flow_node_options *options)
{
    struct gtk_pwm_viewer_data *mdata = (struct gtk_pwm_viewer_data *)data;

    SOL_NULL_CHECK(mdata, -ENOMEM);

    mdata->base.widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(mdata->base.widget, 400, 50);
    g_signal_connect(G_OBJECT(mdata->base.widget), "draw", G_CALLBACK(on_draw_event_pwm), mdata);

    return 0;
}

int
gtk_pwm_viewer_enable_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_pwm_viewer_data *mdata = data;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOLEAN) {
        bool value;
        int r = sol_flow_packet_get_boolean(packet, &value);
        SOL_INT_CHECK(r, < 0, r);
        mdata->enabled = value;
        gtk_widget_queue_draw(mdata->base.widget);
    } else {
        SOL_WRN("Unsupported packet=%p type=%p (%s)", packet, sol_flow_packet_get_type(packet), sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    return 0;
}

int
gtk_pwm_viewer_duty_cycle_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_pwm_viewer_data *mdata = data;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t value;
        int r = sol_flow_packet_get_irange_value(packet, &value);
        SOL_INT_CHECK(r, < 0, r);
        mdata->duty_cycle = value;
        gtk_widget_queue_draw(mdata->base.widget);
    } else {
        SOL_WRN("Unsupported packet=%p type=%p (%s)", packet, sol_flow_packet_get_type(packet), sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    return 0;
}

int
gtk_pwm_viewer_period_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_pwm_viewer_data *mdata = data;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t value;
        int r = sol_flow_packet_get_irange_value(packet, &value);
        SOL_INT_CHECK(r, < 0, r);
        mdata->period = value;
        gtk_widget_queue_draw(mdata->base.widget);
    } else {
        SOL_WRN("Unsupported packet=%p type=%p (%s)", packet, sol_flow_packet_get_type(packet), sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(pwm_viewer);
