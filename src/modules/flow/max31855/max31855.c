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

#include "max31855-gen.h"

#include "sol-flow-internal.h"
#include "sol-spi.h"
#include "sol-types.h"
#include "max31855-gen.h"

#include <sol-util.h>
#include <errno.h>
#include <stdio.h>

struct max31855_data {
    struct sol_spi *device;
    struct sol_flow_node *node;
    struct sol_spi_config spi_config;
    struct sol_drange temperature;
    uint8_t rx[4];
    uint8_t tx[4];
    uint8_t pending_packets;
    int spi_bus;
};

static int
max31855_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct max31855_data *mdata = data;
    const struct sol_flow_node_type_temperature_max31855_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_MAX31855_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_temperature_max31855_options *)options;

    mdata->spi_bus = opts->bus.val;
    mdata->spi_config.chip_select = opts->chip_select.val;
    mdata->spi_config.frequency = 2000000;
    mdata->spi_config.bits_per_word = 8;
    mdata->spi_config.mode = 0;
    mdata->device = sol_spi_open(mdata->spi_bus, &mdata->spi_config);
    mdata->node = node;
    mdata->pending_packets = 0;
    memset(mdata->tx, 0, sizeof(mdata->tx));

    if (!mdata->device)
        return -ENOMEM;

    return 0;
}

static void
max31855_close(struct sol_flow_node *node, void *data)
{
    struct max31855_data *mdata = data;

    sol_spi_close(mdata->device);
}

static void
spi_transfer_cb(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status)
{
    struct max31855_data *mdata = cb_data;
    static const int oc_fault  = 0x1;
    static const int scg_fault = 0x2;
    static const int scv_fault = 0x4;
    static const int valid_data = 14; /* nr of valid bits from the reading */
    static const double celcius_factor = 0.25; /* data read is times 4 in celcius */
    static const double c_to_k = 273.15;
    int32_t raw_value;

    /* min and max temperatures from the manual */
    mdata->temperature.max = 125.0 + c_to_k;
    mdata->temperature.min = -40.0 + c_to_k;

    /* Endian correct way of making our char array into an 32bit int
     * as stated from the upm project.
     */
    raw_value = (rx[0] << 24) | (rx[1] << 16) | (rx[2] << 8) | rx[3];

    if (raw_value & (oc_fault | scg_fault | scv_fault)) {
        sol_flow_send_error_packet(mdata->node, -EIO, "Error reading max31855 temperature sensor");
        goto end;
    }

    /* Doc says that the output is 14 bits, let's remove the 18 that are uneeded. */
    raw_value >>= (32 - valid_data);
    if (raw_value & (0x1 << valid_data)) { /* Verify signal bit. */
        raw_value &= ~(0x1 << valid_data); /* clear signal bit on the wrong place. */
        raw_value |= 0x1 << 31;    /*/ set signal bit in the right place. */
    }

    mdata->temperature.val = (raw_value * celcius_factor) + c_to_k;
    sol_flow_send_drange_packet(mdata->node, SOL_FLOW_NODE_TYPE_TEMPERATURE_MAX31855__OUT__KELVIN, &mdata->temperature);

end:
    mdata->pending_packets--;
    if (mdata->pending_packets) {
        if (!sol_spi_transfer(mdata->device, mdata->rx, mdata->tx, sizeof(mdata->tx), &spi_transfer_cb, mdata))
            sol_flow_send_error_packet(mdata->node, -EIO, "Error reading max31855 temperature sensor");
    }
}

static int
max31855_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct max31855_data *mdata = data;

    mdata->pending_packets++;

    if (mdata->pending_packets == 1) {
        if (!sol_spi_transfer(mdata->device, mdata->rx, mdata->tx, sizeof(mdata->tx), &spi_transfer_cb, mdata)) {
            sol_flow_send_error_packet(mdata->node, -EIO, "Error reading max31855 temperature sensor");
            return -EIO;
        }
    }

    return 0;
}

#include "max31855-gen.c"
