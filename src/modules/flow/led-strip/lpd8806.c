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

#include "sol-flow/led-strip.h"
#include "sol-flow-internal.h"

#include <sol-spi.h>
#include <sol-util-internal.h>
#include <errno.h>

struct lcd_strip_lpd8806_data {
    struct sol_spi *spi;
    uint8_t *pixels;
    uint8_t *spi_rx_buffer;
    int last_set_pixel;
    int last_set_color;
    uint16_t pixel_count;
    uint16_t pixel_array_length;
    uint8_t spi_busy : 1, flush_pending : 1;
};

static void
led_strip_controler_close(struct sol_flow_node *node, void *data)
{
    struct lcd_strip_lpd8806_data *mdata = data;

    if (mdata->spi)
        sol_spi_close(mdata->spi);

    free(mdata->pixels);
    free(mdata->spi_rx_buffer);
}

static void
spi_transfer_initial_reset(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status)
{
    struct lcd_strip_lpd8806_data *mdata = cb_data;

    mdata->spi_busy = false;

    if (status < 0)
        SOL_WRN("SPI error when writing initial value of pixels.");
}

static int
led_strip_controler_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct lcd_strip_lpd8806_data *mdata = data;
    const struct sol_flow_node_type_led_strip_lpd8806_options *opts;
    uint32_t data_bytes, pixel_array_length;
    uint8_t latch_bytes;
    struct sol_spi_config spi_config;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_LED_STRIP_LPD8806_OPTIONS_API_VERSION,
        -EINVAL);

    opts = (const struct sol_flow_node_type_led_strip_lpd8806_options *)options;

    SOL_INT_CHECK(opts->pixel_count, < 0, -EINVAL);
    mdata->pixel_count = opts->pixel_count;

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

    SOL_SET_API_VERSION(spi_config.api_version = SOL_SPI_CONFIG_API_VERSION; )
    spi_config.chip_select = opts->chip_select;
    spi_config.mode = SOL_SPI_MODE_0;
    spi_config.frequency = 100 * 1000; //100KHz
    spi_config.bits_per_word = SOL_SPI_DATA_BITS_DEFAULT;
    mdata->spi = sol_spi_open(opts->bus, &spi_config);
    if (mdata->spi) {
        // Initial reset
        sol_spi_transfer(mdata->spi, &mdata->pixels[mdata->pixel_count * 3],
            NULL, latch_bytes, spi_transfer_initial_reset,
            mdata);
        mdata->spi_busy = true;
    }
    mdata->flush_pending = false;

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
        SOL_WRN("Invalid pixel %d. Expected pixel ranging from 0 to %d", (int)in_value.val, mdata->pixel_count - 1);
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

static int flush_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);

static void
spi_transfer_cb(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status)
{
    struct lcd_strip_lpd8806_data *mdata = cb_data;

    mdata->spi_busy = false;

    if (status < 0) {
        SOL_WRN("SPI error when writing pixels.");
        return;
    }

    if (!mdata->flush_pending)
        return;

    mdata->flush_pending = false;
    flush_process(NULL, mdata, 0, 0, NULL);
}

static int
flush_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct lcd_strip_lpd8806_data *mdata = data;

    if (mdata->spi_busy) {
        mdata->flush_pending = true;
        return 0;
    }

    if (!sol_spi_transfer(mdata->spi, mdata->pixels, NULL,
        mdata->pixel_array_length, spi_transfer_cb, mdata)) {
        SOL_WRN("Unable to start SPI transfer.");
        return -1;
    }

    mdata->spi_busy = true;

    return 0;
}

#include "led-strip-gen.c"
