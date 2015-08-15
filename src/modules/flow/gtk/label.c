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

#include "label.h"
#include "gtk-gen.h"

static int
label_setup(struct gtk_common_data *mdata,
    const struct sol_flow_node_options *options)
{

    mdata->widget = gtk_label_new(NULL);
    g_object_set(mdata->widget, "halign", GTK_ALIGN_CENTER, NULL);

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
    } else if (sol_flow_packet_get_type(packet) == SOL_FLOW_PACKET_TYPE_BOOLEAN) {
        bool value;
        int r = sol_flow_packet_get_boolean(packet, &value);
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
    } else {
        SOL_WRN("Unsupported packet=%p type=%p (%s)",
            packet, sol_flow_packet_get_type(packet), sol_flow_packet_get_type(packet)->name);
        return -EINVAL;
    }

    return 0;
}

DEFINE_DEFAULT_OPEN_CLOSE(label);
