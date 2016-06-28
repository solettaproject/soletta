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

#include "label.h"
#include "sol-flow/gtk.h"
#include <time.h>

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
label_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{

    mdata->widget = gtk_label_new(NULL);
    g_object_set(mdata->widget, "halign", GTK_ALIGN_CENTER, NULL);
    set_min_size(mdata->widget);

    return 0;
}

int
gtk_label_in_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct gtk_common_data *mdata = data;
    struct gtk_label_data *label_data = data;

    if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_EMPTY) {
        char buf[32];
        label_data->empty_count++;
        snprintf(buf, sizeof(buf), "[empty %d]", label_data->empty_count);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_STRING) {
        const char *in_value;
        int r = sol_flow_packet_get_string(packet, &in_value);
        SOL_INT_CHECK(r, < 0, r);
        gtk_label_set_text(GTK_LABEL(mdata->widget), in_value);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOL) {
        bool value;
        int r = sol_flow_packet_get_bool(packet, &value);
        SOL_INT_CHECK(r, < 0, r);
        gtk_label_set_text(GTK_LABEL(mdata->widget), value ? "ON" : "OFF");
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BYTE) {
        char buf[9];
        unsigned char value;
        int r = sol_flow_packet_get_byte(packet, &value), i;
        SOL_INT_CHECK(r, < 0, r);

        for (i = 7; i >= 0; i--) {
            buf[i] = (value & 0x1) + '0';
            value >>= 1;
        }
        buf[8] = '\0';
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_IRANGE) {
        int32_t val;
        char buf[32];
        int r = sol_flow_packet_get_irange_value(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        snprintf(buf, sizeof(buf), "%d", val);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_DRANGE) {
        double val;
        char buf[64];
        int r = sol_flow_packet_get_drange_value(packet, &val);
        SOL_INT_CHECK(r, < 0, r);
        snprintf(buf, sizeof(buf), "%g", val);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR) {
        char buf[1024];
        double x, y, z;
        int r = sol_flow_packet_get_direction_vector_components(packet, &x,
            &y, &z);
        SOL_INT_CHECK(r, < 0, r);
        snprintf(buf, sizeof(buf), "X:%g, Y:%g, Z:%g", x, y, z);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_RGB) {
        char buf[1024];
        uint32_t r, g, b;
        int err = sol_flow_packet_get_rgb_components(packet, &r, &g, &b);
        SOL_INT_CHECK(err, < 0, err);
        snprintf(buf, sizeof(buf), "Red:%" PRIu32 ", Green:%" PRIu32
            ", Blue:%" PRIu32, r, g, b);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_LOCATION) {
        char buf[1024];
        struct sol_location loc;
        int r = sol_flow_packet_get_location(packet, &loc);
        SOL_INT_CHECK(r, < 0, r);
        snprintf(buf, sizeof(buf), "Altitude:%g, Longitude:%g, Altitude:%g",
            loc.lat, loc.lon, loc.alt);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_TIMESTAMP) {
        struct timespec spec;
        struct tm res;
        char buf[1024];
        int r = sol_flow_packet_get_timestamp(packet, &spec);
        SOL_INT_CHECK(r, < 0, r);
        tzset();
        SOL_NULL_CHECK(localtime_r(&spec.tv_sec, &res), -EINVAL);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &res);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_ERROR) {
        int code;
        const char *msg;
        char buf[1024];
        int r = sol_flow_packet_get_error(packet, &code, &msg);
        SOL_INT_CHECK(r, < 0, r);
        snprintf(buf, sizeof(buf), "Error message: %s. Code: %d", msg, code);
        gtk_label_set_text(GTK_LABEL(mdata->widget), buf);
    } else {
        SOL_WRN("Unsupported packet=%p type=%p (%s)",
            packet, sol_flow_packet_get_type(packet),
            sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(label);
