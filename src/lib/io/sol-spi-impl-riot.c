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

#include <errno.h>
#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-spi.h"
#include "sol-util.h"

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

    if (unlikely(config->api_version != SOL_SPI_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open SPI that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_SPI_CONFIG_API_VERSION);
        return NULL;
    }

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

    gpio_init(spi->cs_pin, GPIO_DIR_OUT, GPIO_NOPULL);
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

SOL_API bool
sol_spi_transfer(struct sol_spi *spi, const uint8_t *tx_user, uint8_t *rx, size_t count, void (*transfer_cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status), const void *cb_data)
{
    uint8_t *tx = (uint8_t *)tx_user;

    SOL_NULL_CHECK(spi, false);
    SOL_EXP_CHECK(spi->transfer.timeout, false);

    spi->transfer.intern_allocated_buffer_flags = 0;
    if (tx == NULL) {
        tx = calloc(count, sizeof(uint8_t));
        SOL_NULL_CHECK(tx, false);
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

    return true;

timeout_fail:
    if (spi->transfer.intern_allocated_buffer_flags & INTERN_ALLOCATED_RX_BUFFER)
        free(rx);
rx_alloc_fail:
    if (spi->transfer.intern_allocated_buffer_flags & INTERN_ALLOCATED_TX_BUFFER)
        free(tx);
    return false;
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
