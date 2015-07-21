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
#include "sol-spi.h"
#include "sol-util.h"

#include "periph/gpio.h"
#include "periph/spi.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "spi");

struct sol_spi {
    unsigned int bus;
    unsigned int cs_pin;
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

    gpio_init_out(spi->cs_pin, GPIO_NOPULL);
    gpio_set(spi->cs_pin);
    return spi;
}

SOL_API void
sol_spi_close(struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi);
    spi_poweroff(spi->bus);
    free(spi);
}

SOL_API bool
sol_spi_transfer(const struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, size_t count)
{
    int ret;

    SOL_NULL_CHECK(spi, false);

    spi_acquire(spi->bus);
    gpio_clear(spi->cs_pin);
    ret = spi_transfer_bytes(spi->bus, (char *)tx, (char *)rx, count);
    gpio_set(spi->cs_pin);
    spi_release(spi->bus);

    return ret > 0 && ((unsigned int)ret) == count;
}
