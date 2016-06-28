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

#include "sol-flow/max31855.h"

#include "sol-flow-internal.h"
#include "sol-flow/max31855.h"
#include "sol-spi.h"
#include "sol-types.h"

#include <sol-util-internal.h>
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
    struct sol_flow_node_type_max31855_options *opts;
    struct sol_spi_config config = {
        SOL_SET_API_VERSION(.api_version = SOL_SPI_CONFIG_API_VERSION)
    };

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options,
        SOL_FLOW_NODE_TYPE_MAX31855_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (struct sol_flow_node_type_max31855_options *)options;

    config.chip_select = opts->chip_select;
    config.frequency = 2000000;
    config.bits_per_word = 8;
    config.mode = 0;

    mdata->device = sol_spi_open(opts->bus, &config);
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
    sol_flow_send_drange_packet(mdata->node, SOL_FLOW_NODE_TYPE_MAX31855__OUT__KELVIN, &temperature);

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
