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
    uint32_t mode;
    spi_speed_t speed;
};

static bool
spi_init(struct sol_spi *spi)
{
    int retval;

    spi_acquire(spi->bus);
    spi_conf_pins(spi->bus);
    retval = spi_init_master(spi->bus, spi->mode, spi->speed);
    spi_release(spi->bus);
    return retval == 0;
}

SOL_API struct sol_spi *
sol_spi_open(unsigned int bus, unsigned int chip_select)
{
    struct sol_spi *spi;

    SOL_LOG_INTERNAL_INIT_ONCE;

    spi = calloc(sizeof(struct sol_spi), 1);
    SOL_NULL_CHECK(spi, NULL);

    spi->bus = bus;
    spi->cs_pin = chip_select;

    spi_poweron(spi->bus);
    spi_init(spi);

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

SOL_API int32_t
sol_spi_get_transfer_mode(const struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi, -EINVAL);
    return spi->mode;
}

SOL_API bool
sol_spi_set_transfer_mode(struct sol_spi *spi, uint32_t mode)
{
    SOL_NULL_CHECK(spi, false);
    SOL_INT_CHECK(mode, > 3, false);
    spi->mode = mode;
    return spi_init(spi);
}

SOL_API int8_t
sol_spi_get_bit_justification(const struct sol_spi *spi)
{
    return 0;
}

SOL_API bool
sol_spi_set_bit_justification(struct sol_spi *spi, uint8_t justification)
{
    return false;
}

SOL_API int8_t
sol_spi_get_bits_per_word(const struct sol_spi *spi)
{
    return 8;
}

SOL_API bool
sol_spi_set_bits_per_word(struct sol_spi *spi, uint8_t bits_per_word)
{
    SOL_NULL_CHECK(spi, false);
    if (bits_per_word != 8)
        return false;
    return true;
}

static int32_t
riot_speed_to_hz(spi_speed_t speed)
{
    const unsigned int table[] = {
        [SPI_SPEED_100KHZ] = 100000,
        [SPI_SPEED_400KHZ] = 400000,
        [SPI_SPEED_1MHZ] = 1000000,
        [SPI_SPEED_5MHZ] = 5000000,
        [SPI_SPEED_10MHZ] = 10000000
    };

    if (unlikely(speed > (sizeof(table) / sizeof(unsigned int))))
        return 0;
    return table[speed];
}

SOL_API int32_t
sol_spi_get_max_speed(const struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi, 0);
    return riot_speed_to_hz(spi->speed);
}

static spi_speed_t
hz_to_riot_speed(uint32_t speed)
{
    if (speed >= 10000000)
        return SPI_SPEED_10MHZ;
    if (speed >= 5000000)
        return SPI_SPEED_5MHZ;
    if (speed >= 1000000)
        return SPI_SPEED_1MHZ;
    if (speed >= 400000)
        return SPI_SPEED_400KHZ;
    return SPI_SPEED_100KHZ;
}

SOL_API bool
sol_spi_set_max_speed(struct sol_spi *spi, uint32_t speed)
{
    SOL_NULL_CHECK(spi, false);
    spi->speed = hz_to_riot_speed(speed);
    return spi_init(spi);
}

SOL_API bool
sol_spi_transfer(const struct sol_spi *spi, uint8_t *tx, uint8_t *rx, size_t count)
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
