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

#include <errno.h>
#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-spi.h"
#include "sol-util-internal.h"

#include "periph/gpio.h"
#include "periph/spi.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "spi");

#define INTERN_ALLOCATED_TX_BUFFER (1 << 0)
#define INTERN_ALLOCATED_RX_BUFFER (1 << 1)

struct sol_spi {
    unsigned int bus;
    unsigned int cs_pin;
    struct {
        void (*cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status);
        const void *cb_data;
        uint8_t *tx;
        uint8_t *rx;
        struct sol_timeout *timeout;
        uint8_t intern_allocated_buffer_flags;
        size_t count;
        ssize_t status;
    } transfer;
};

static spi_speed_t
uint32_to_spi_speed_enum(uint32_t freq)
{
    if (freq >= 10000000)
        return SPI_SPEED_10MHZ;
    if (freq >= 5000000)
        return SPI_SPEED_5MHZ;
    if (freq >= 1000000)
        return SPI_SPEED_1MHZ;
    if (freq >= 400000)
        return SPI_SPEED_400KHZ;
    return SPI_SPEED_100KHZ;
}

SOL_API struct sol_spi *
sol_spi_open(unsigned int bus, const struct sol_spi_config *config)
{
    struct sol_spi *spi;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_SPI_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open SPI that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_SPI_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    SOL_EXP_CHECK(config->bits_per_word != 8, NULL);

    spi = malloc(sizeof(struct sol_spi));
    SOL_NULL_CHECK(spi, NULL);

    spi_poweron(bus);
    spi_acquire(bus);
    spi_conf_pins(bus);
    if (spi_init_master(bus, config->mode, uint32_to_spi_speed_enum(config->frequency)) != 0) {
        SOL_WRN("%u,%u: Unable to setup SPI", bus, config->chip_select);
        spi_release(bus);
        free(spi);
        return NULL;
    }
    spi_release(spi->bus);

    spi->bus = bus;
    spi->cs_pin = config->chip_select;
    spi->transfer.timeout = NULL;

    gpio_init(spi->cs_pin, GPIO_OUT);
    gpio_set(spi->cs_pin);
    return spi;
}

static void
spi_transfer_dispatch(struct sol_spi *spi)
{
    if (spi->transfer.intern_allocated_buffer_flags & INTERN_ALLOCATED_TX_BUFFER) {
        free(spi->transfer.tx);
        spi->transfer.tx = NULL;
    }
    if (spi->transfer.intern_allocated_buffer_flags & INTERN_ALLOCATED_RX_BUFFER) {
        free(spi->transfer.rx);
        spi->transfer.rx = NULL;
    }

    if (!spi->transfer.cb) return;
    spi->transfer.cb((void *)spi->transfer.cb_data, spi, spi->transfer.tx,
        spi->transfer.rx, spi->transfer.status);
}

static bool
spi_timeout_cb(void *data)
{
    struct sol_spi *spi = data;
    int ret;

    spi_acquire(spi->bus);
    gpio_clear(spi->cs_pin);
    ret = spi_transfer_bytes(spi->bus, (char *)spi->transfer.tx,
        (char *)spi->transfer.rx, spi->transfer.count);
    gpio_set(spi->cs_pin);
    spi_release(spi->bus);

    spi->transfer.status = ret;
    spi->transfer.timeout = NULL;
    spi_transfer_dispatch(spi);

    return false;
}

SOL_API int
sol_spi_transfer(struct sol_spi *spi, const uint8_t *tx_user, uint8_t *rx, size_t count, void (*transfer_cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status), const void *cb_data)
{
    uint8_t *tx = (uint8_t *)tx_user;

    SOL_NULL_CHECK(spi, -EINVAL);
    SOL_INT_CHECK(count, == 0, -EINVAL);

    SOL_EXP_CHECK(spi->transfer.timeout, -EBUSY);

    spi->transfer.intern_allocated_buffer_flags = 0;
    if (tx == NULL) {
        tx = calloc(count, sizeof(uint8_t));
        SOL_NULL_CHECK(tx, -ENOMEM);
        spi->transfer.intern_allocated_buffer_flags = INTERN_ALLOCATED_TX_BUFFER;
    }
    if (rx == NULL) {
        rx = calloc(count, sizeof(uint8_t));
        SOL_NULL_CHECK_GOTO(rx, rx_alloc_fail);
        spi->transfer.intern_allocated_buffer_flags = INTERN_ALLOCATED_RX_BUFFER;
    }

    spi->transfer.tx = tx;
    spi->transfer.rx = rx;
    spi->transfer.count = count;
    spi->transfer.status = -1;
    spi->transfer.cb = transfer_cb;
    spi->transfer.cb_data = cb_data;

    spi->transfer.timeout = sol_timeout_add(0, spi_timeout_cb, spi);
    SOL_NULL_CHECK_GOTO(spi->transfer.timeout, timeout_fail);

    return 0;

timeout_fail:
    if (spi->transfer.intern_allocated_buffer_flags & INTERN_ALLOCATED_RX_BUFFER)
        free(rx);
rx_alloc_fail:
    if (spi->transfer.intern_allocated_buffer_flags & INTERN_ALLOCATED_TX_BUFFER)
        free(tx);
    return -ENOMEM;
}

SOL_API void
sol_spi_close(struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi);
    if (spi->transfer.timeout) {
        sol_timeout_del(spi->transfer.timeout);
        spi_transfer_dispatch(spi);
    }
    spi_poweroff(spi->bus);
    free(spi);
}
