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

#include "led-strip-gen.h"
#include "sol-flow-internal.h"

#include <sol-spi.h>
#include <sol-util.h>
#include <errno.h>

struct lcd_strip_lpd8806_data {
    struct sol_spi *spi;
    uint8_t *pixels;
    int last_set_pixel;
    int last_set_color;
    uint16_t pixel_count;
    uint16_t pixel_array_length;
};

static void
led_strip_controler_close(struct sol_flow_node *node, void *data)
{
    struct lcd_strip_lpd8806_data *mdata = data;

    if (mdata->spi)
        sol_spi_close(mdata->spi);

    free(mdata->pixels);
}

static int
led_strip_controler_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct lcd_strip_lpd8806_data *mdata = data;
    const struct sol_flow_node_type_led_strip_lpd8806_options *opts;
    uint32_t data_bytes, pixel_array_length;
    uint8_t latch_bytes;

    opts = (const struct sol_flow_node_type_led_strip_lpd8806_options *)options;

    SOL_INT_CHECK(opts->pixel_count.val, < 0, -EINVAL);
    mdata->pixel_count = opts->pixel_count.val;

    data_bytes = mdata->pixel_count * 3;
    SOL_INT_CHECK(data_bytes, > UINT16_MAX, -EINVAL);

    latch_bytes = (mdata->pixel_count + 31) / 32;
    pixel_array_length = data_bytes + latch_bytes;
    SOL_INT_CHECK(pixel_array_length, > UINT16_MAX, -EINVAL);

    mdata->pixel_array_length = pixel_array_length;

    mdata->pixels = malloc(mdata->pixel_array_length);
    SOL_NULL_CHECK(mdata->pixels, -ENOMEM);
    memset(mdata->pixels, 0x80, data_bytes); // Init to RGB 'off' state
    memset(&mdata->pixels[data_bytes], 0, latch_bytes); // Clear latch bytes

    mdata->spi = sol_spi_open(opts->bus.val, opts->chip_select.val);
    if (mdata->spi) {
        sol_spi_set_transfer_mode(mdata->spi, 0);

        // Initial reset
        sol_spi_transfer(mdata->spi, &mdata->pixels[mdata->pixel_count * 3], NULL, latch_bytes);
    }

    return 0;
}

static void
_set_pixel_color(struct lcd_strip_lpd8806_data *mdata)
{
    uint8_t r, g, b;
    uint8_t *pixel;

    r = 0xff & (mdata->last_set_color >> 16);
    g = 0xff & (mdata->last_set_color >> 8);
    b = 0xff & mdata->last_set_color;

    pixel = &mdata->pixels[mdata->last_set_pixel * 3];
    // Strip color order is GRB, not RGB, hence the order below.
    *pixel++ = g | 0x80;
    *pixel++ = r | 0x80;
    *pixel++ = b | 0x80;

    mdata->last_set_pixel = -1;
    mdata->last_set_color = -1;
}

static int
pixel_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct lcd_strip_lpd8806_data *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (in_value.val >= mdata->pixel_count) {
        SOL_WRN("Invalid pixel %d. Expected pixel ranging from 0 to %d", in_value.val, mdata->pixel_count - 1);
        return -EINVAL;
    }

    mdata->last_set_pixel = in_value.val;
    if (mdata->last_set_color != -1)
        _set_pixel_color(mdata);

    return 0;
}

static int
color_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct lcd_strip_lpd8806_data *mdata = data;
    int r;
    struct sol_rgb in_value;

    r = sol_flow_packet_get_rgb(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->last_set_color = (in_value.red << 16) | (in_value.green) << 8 | in_value.blue;
    if (mdata->last_set_pixel != -1)
        _set_pixel_color(mdata);

    return 0;
}

static int
flush_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct lcd_strip_lpd8806_data *mdata = data;

    sol_spi_transfer(mdata->spi, mdata->pixels, NULL, mdata->pixel_array_length);

    return 0;
}

#include "led-strip-gen.c"
