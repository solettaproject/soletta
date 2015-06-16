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

#include <asm-generic/ioctl.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-spi.h"
#include "sol-util.h"


SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "spi");

struct sol_spi {
    int fd;
    unsigned int bus;
    unsigned int chip_select;
};

int32_t
sol_spi_get_transfer_mode(const struct sol_spi *spi)
{
    uint32_t mode;

    SOL_NULL_CHECK(spi, -EINVAL);

    if (ioctl(spi->fd, SPI_IOC_RD_MODE, &mode) == -1) {
        SOL_WRN("%u,%u: Unable to read SPI mode", spi->bus, spi->chip_select);
        return -errno;
    }

    return mode;
}

bool
sol_spi_set_transfer_mode(struct sol_spi *spi, uint32_t mode)
{
    SOL_NULL_CHECK(spi, false);

    if (ioctl(spi->fd, SPI_IOC_WR_MODE, &mode) == -1) {
        SOL_WRN("%u,%u: Unable to write SPI mode", spi->bus, spi->chip_select);
        return false;
    }

    return true;
}

int8_t
sol_spi_get_bit_justification(const struct sol_spi *spi)
{
    uint8_t justification;

    SOL_NULL_CHECK(spi, -EINVAL);

    if (ioctl(spi->fd, SPI_IOC_RD_LSB_FIRST, &justification) == -1) {
        SOL_WRN("%u,%u: Unable to read SPI bit justification", spi->bus, spi->chip_select);
        return -errno;
    }

    return justification;
}

bool
sol_spi_set_bit_justification(struct sol_spi *spi, uint8_t justification)
{
    SOL_NULL_CHECK(spi, false);

    if (ioctl(spi->fd, SPI_IOC_WR_LSB_FIRST, &justification) == -1) {
        SOL_WRN("%u,%u: Unable to write SPI bit justification", spi->bus, spi->chip_select);
        return false;
    }

    return true;
}

int8_t
sol_spi_get_bits_per_word(const struct sol_spi *spi)
{
    uint8_t bits_per_word;

    SOL_NULL_CHECK(spi, -EINVAL);

    if (ioctl(spi->fd, SPI_IOC_RD_BITS_PER_WORD, &bits_per_word) == -1) {
        SOL_WRN("%u,%u: Unable to read SPI bits per word", spi->bus, spi->chip_select);
        return -errno;
    }

    return bits_per_word;
}

bool
sol_spi_set_bits_per_word(struct sol_spi *spi, uint8_t bits_per_word)
{
    SOL_NULL_CHECK(spi, false);

    if (ioctl(spi->fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) == -1) {
        SOL_WRN("%u,%u: Unable to write SPI bits per word", spi->bus, spi->chip_select);
        return false;
    }

    return true;
}

int32_t
sol_spi_get_max_speed(const struct sol_spi *spi)
{
    uint32_t speed;

    SOL_NULL_CHECK(spi, -EINVAL);

    if (ioctl(spi->fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) == -1) {
        SOL_WRN("%u,%u: Unable to read SPI max speed", spi->bus, spi->chip_select);
        return -errno;
    }

    return speed;
}

bool
sol_spi_set_max_speed(struct sol_spi *spi, uint32_t speed)
{
    SOL_NULL_CHECK(spi, false);

    if (ioctl(spi->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1) {
        SOL_WRN("%u,%u: Unable to write SPI max speed", spi->bus, spi->chip_select);
        return false;
    }

    return true;
}

bool
sol_spi_transfer(const struct sol_spi *spi, uint8_t *tx, uint8_t *rx, size_t size)
{
    struct spi_ioc_transfer tr = { (uintptr_t)tx, (uintptr_t)rx, size };

    SOL_NULL_CHECK(spi, false);
    SOL_INT_CHECK(size, == 0, false);

    if (ioctl(spi->fd, SPI_IOC_MESSAGE(1), &tr) == -1) {
        SOL_WRN("%u,%u: Unable to perform SPI transfer", spi->bus, spi->chip_select);
        return false;
    }

    return true;
}

bool
sol_spi_raw_transfer(const struct sol_spi *spi, void *tr, size_t count)
{
    struct spi_ioc_transfer *_tr = (struct spi_ioc_transfer *)tr;

    SOL_NULL_CHECK(spi, false);
    SOL_NULL_CHECK(tr, false);
    SOL_INT_CHECK(count, == 0, false);

    if (ioctl(spi->fd, SPI_IOC_MESSAGE(count), _tr) == -1) {
        SOL_WRN("%u,%u: Unable to perform SPI transfer", spi->bus, spi->chip_select);
        return false;
    }

    return true;
}

void
sol_spi_close(struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi);

    close(spi->fd);

    free(spi);
}

struct sol_spi *
sol_spi_open(unsigned int bus, unsigned int chip_select)
{
    struct sol_spi *spi;
    char spi_dir[PATH_MAX];

    SOL_LOG_INTERNAL_INIT_ONCE;

    spi = calloc(1, sizeof(*spi));
    if (!spi) {
        SOL_WRN("%u,%u: Unable to allocate spi", bus, chip_select);
        return NULL;
    }

    spi->bus = bus;
    spi->chip_select = chip_select;

    snprintf(spi_dir, sizeof(spi_dir), "/dev/spidev%u.%u", bus, chip_select);
    spi->fd = open(spi_dir, O_RDWR | O_CLOEXEC);

    if (spi->fd < 0) {
        SOL_WRN("%u,%u:Unable to access spi device - %s", bus, chip_select, spi_dir);
        free(spi);
        return NULL;
    }

    return spi;
}
