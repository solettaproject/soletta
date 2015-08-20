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

#define OC_FAULT  0x1
#define SCG_FAULT 0x2
#define SCV_FAULT 0x4
#define VALID_DATA_BITS 13 /* nr of valid bits from the reading */
#define DATA_STEP 0.25 /* data read is times 4 in celsius */
#define KELVIN_FACTOR 273.15
#define FIELD_MASK (0x7 << 10 | 0xF << 6 | 0xF << 2 | 0x3) // last 12 bits 1,

struct max31855_data {
    struct sol_spi *device;
    struct sol_flow_node *node;
    uint8_t rx[4];
    uint8_t tx[4];
    uint8_t pending_packets;
};

static int
max31855_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct max31855_data *mdata = data;
    struct sol_flow_node_type_max31855_temperature_options *opts;
    struct sol_spi_config config = { .api_version = SOL_SPI_CONFIG_API_VERSION };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_MAX31855_TEMPERATURE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (struct sol_flow_node_type_max31855_temperature_options *)options;

    config.chip_select = opts->chip_select.val;
    config.frequency = 2000000;
    config.bits_per_word = 8;
    config.mode = 0;

    mdata->device = sol_spi_open(opts->bus.val, &config);
    mdata->node = node;
    mdata->pending_packets = 0;

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

static void spi_transfer_cb(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status);

static int
spi_call(struct max31855_data *mdata)
{
    if (mdata->pending_packets) {
        if (!sol_spi_transfer(mdata->device, mdata->rx, mdata->tx, sizeof(mdata->tx), &spi_transfer_cb, mdata)) {
            sol_flow_send_error_packet(mdata->node, EIO,
                "Error reading max31855 temperature sensor");
            return -EIO;
        }
    }
    return 0;
}

static void
spi_transfer_cb(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status)
{
    struct max31855_data *mdata = cb_data;
    struct sol_drange temperature;

    int32_t raw_value;

    // Amount asked to read is uint8_t[4]
    if (status != sizeof(uint8_t) * 4) {
        sol_flow_send_error_packet(mdata->node, EIO,
            "Error reading max31855 temperature sensor");
        goto end;
    }

    /* min and max temperatures from the manual */
    temperature.max = 125.0 + KELVIN_FACTOR;
    temperature.min = -40.0 + KELVIN_FACTOR;
    temperature.step = 0.25;

    /* Endian correct way of making our char array into an 32bit int
     * as stated from the upm project.
     */
    raw_value = (rx[0] << 24) | (rx[1] << 16) | (rx[2] << 8) | rx[3];

    if (raw_value & (OC_FAULT | SCG_FAULT | SCG_FAULT)) {
        sol_flow_send_error_packet(mdata->node, EIO,
            "Error reading max31855 temperature sensor");
        goto end;
    }

    /* Doc says that the output is 14 bits, let's remove the 18 that are uneeded. */
    raw_value >>= (32 - VALID_DATA_BITS);
    if (raw_value & (0x1 << VALID_DATA_BITS)) { /* Verify signal bit. */
        raw_value |= ~FIELD_MASK;  // invert bytes
    }

    temperature.val = (raw_value * DATA_STEP) + KELVIN_FACTOR;
    sol_flow_send_drange_packet(mdata->node, SOL_FLOW_NODE_TYPE_MAX31855_TEMPERATURE__OUT__KELVIN, &temperature);

end:
    mdata->pending_packets--;
    spi_call(mdata);
}

static int
max31855_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct max31855_data *mdata = data;
    int ret_value = 0;

    mdata->pending_packets++;

    if (mdata->pending_packets == 1) {
        ret_value = spi_call(mdata);
    }

    return ret_value;
}

#include "max31855-gen.c"
