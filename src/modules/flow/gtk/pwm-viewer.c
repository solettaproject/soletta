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

#include "pwm-viewer.h"
#include "sol-flow/gtk.h"
#include "sol-flow-internal.h"

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
    const struct sol_flow_node_type_gtk_pwm_viewer_options *opts;
    struct gtk_pwm_viewer_data *mdata = (struct gtk_pwm_viewer_data *)data;

    SOL_NULL_CHECK(mdata, -ENOMEM);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_GTK_PWM_VIEWER_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_gtk_pwm_viewer_options *)options;

    mdata->enabled = opts->enabled;
    mdata->duty_cycle = opts->duty_cycle;
    mdata->period = opts->period;

    mdata->base.widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(mdata->base.widget, 400, 50);
    g_signal_connect(G_OBJECT(mdata->base.widget), "draw", G_CALLBACK(on_draw_event_pwm), mdata);

    gtk_widget_queue_draw(mdata->base.widget);

    return 0;
}

int
gtk_pwm_viewer_enable_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_pwm_viewer_data *mdata = data;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOL) {
        bool value;
        int r = sol_flow_packet_get_bool(packet, &value);
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
