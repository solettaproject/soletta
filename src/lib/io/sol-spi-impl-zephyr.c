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

/* Zephyr includes */
#include "spi.h"

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "spi");

#include "sol-io-common-zephyr.h"

#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-spi.h"
#include "sol-util.h"

/* Zephyr's code has 2 SPI implementations -- Intel and DW. We'll
 * start by hardcoding the latter */

struct spi_dev {
    const char *name;
    int refcnt;
};

static struct spi_dev spi_0_dev = {
    .name = "SPI_0",
    .refcnt = 0
};

static struct spi_dev spi_1_dev = {
    .name = "SPI_1",
    .refcnt = 0
};

static struct spi_dev *devs[2] = {
    &spi_0_dev,
    &spi_1_dev
};

struct sol_spi {
    struct device *dev;
    struct spi_dev *dev_ref;

    unsigned int cs_pin;
    struct {
        void (*cb)(void *cb_data,
            struct sol_spi *spi,
            const uint8_t *tx,
            uint8_t *rx,
            ssize_t status);
        const void *cb_data;
        const uint8_t *tx;
        uint8_t *rx;
        struct sol_timeout *timeout;
        size_t count;
        ssize_t status;
    } transfer;
};

static void
spi_transfer_dispatch(struct sol_spi *spi)
{
    if (!spi->transfer.cb) return;

    spi->transfer.cb((void *)spi->transfer.cb_data, spi, spi->transfer.tx,
        spi->transfer.rx, spi->transfer.status);
}

SOL_API struct sol_spi *
sol_spi_open(unsigned int bus, const struct sol_spi_config *cfg)
{
    struct sol_spi *spi = NULL;
    struct device *dev = NULL;
    struct spi_config config = { 0 };
    uint32_t freq;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (bus != 0 && bus != 1) {
        SOL_WRN("Unsupported SPI bus %d", bus);
        goto err;
    }

    SOL_NULL_CHECK_GOTO(cfg, err);
    freq = cfg->frequency;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_SPI_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open SPI that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_SPI_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    dev = device_get_binding((char *)devs[bus]->name);
    if (!dev) {
        SOL_WRN("Failed to open SPI device %s", devs[bus]->name);
        return NULL;
    }

    spi = calloc(1, sizeof(struct sol_spi));
    SOL_NULL_CHECK(spi, NULL);

    spi->cs_pin = cfg->chip_select;

    switch (cfg->mode) {
    case SOL_SPI_MODE_1:
        config.config |= SPI_MODE_CPHA;
        break;
    case SOL_SPI_MODE_2:
        config.config |= SPI_MODE_CPOL;
        break;
    case SOL_SPI_MODE_3:
        config.config |= (SPI_MODE_CPOL & SPI_MODE_CPHA);
        break;
    default:
        /* already zeroed */
        break;
    }

    config.config |= SPI_WORD(cfg->bits_per_word);
    /* For DW, max_sys_freq is the factor so host clock / factor = the
     * speed you want (e.g. factor of 320 for 100KHz). Minimum factor
     * = 2. */

    /* min 2 */
    if (freq > 1600000) {
        SOL_WRN("SPI controller frequency has to be at most 16Mhz"
            " (%" PRIu32 "Hz was passed), using the maximum value.",
            freq);
        freq = 1600000;
    }

    /* max 32k */
    if (!freq) {
        SOL_WRN("SPI controller frequency has to be non-zero, using the"
            " minimum value of 1Hz.");
        freq = 1;
    }
    config.max_sys_freq = 32000000 / freq;

    if (spi_configure(dev, &config) < 0) {
        SOL_WRN("Failed to configure SPI device %s", devs[bus]->name);
        free(spi);
        return NULL;
    }

    devs[bus]->refcnt++;
    if (devs[bus]->refcnt == 1) {
        if (spi_resume(dev) < 0) {
            SOL_WRN("Failed to resume SPI device %s", devs[bus]->name);
            free(spi);
            return NULL;
        }
    }

    spi->dev = dev;
    spi->dev_ref = devs[bus];

err:
    return spi;
}

SOL_API void
sol_spi_close(struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi);

    if (spi->transfer.timeout) {
        spi->transfer.timeout = NULL;
        spi_transfer_dispatch(spi);
    }

    if (!--(spi->dev_ref->refcnt)) {
        if (spi_suspend(spi->dev) < 0) {
            SOL_WRN("Failed to suspend SPI device %s", spi->dev_ref->name);
        }
    }

    free(spi);
}

static bool
spi_read_timeout_cb(void *data)
{
    struct sol_spi *spi = data;
    int ret;

    ret = spi_transceive(spi->dev, (uint8_t *)spi->transfer.tx,
        spi->transfer.tx ? spi->transfer.count : 0, spi->transfer.rx,
        spi->transfer.rx ? spi->transfer.count : 0);
    if (ret != DEV_OK)
        spi->transfer.status = zephyr_err_to_errno(ret);
    else
        spi->transfer.status = spi->transfer.count;

    spi->transfer.timeout = NULL;
    spi_transfer_dispatch(spi);

    return false;
}

SOL_API bool
sol_spi_transfer(struct sol_spi *spi,
    const uint8_t *tx,
    uint8_t *rx,
    size_t count,
    void (*transfer_cb)(void *cb_data,
    struct sol_spi *spi,
    const uint8_t *tx,
    uint8_t *rx,
    ssize_t status),
    const void *cb_data)
{
    SOL_NULL_CHECK(spi, false);
    SOL_EXP_CHECK(spi->transfer.timeout, false);
    SOL_INT_CHECK(count, == 0, false);

    if (spi_slave_select(spi->dev, spi->cs_pin) < 0) {
        SOL_WRN("Failed to select slave 0x%02x for SPI device %s",
            spi->cs_pin, spi->dev_ref->name);
        return false;
    }

    spi->transfer.tx = tx;
    spi->transfer.rx = rx;
    spi->transfer.count = count;
    spi->transfer.status = -1;
    spi->transfer.cb = transfer_cb;
    spi->transfer.cb_data = cb_data;

    spi->transfer.timeout = sol_timeout_add(0, spi_read_timeout_cb, spi);
    SOL_NULL_CHECK(spi->transfer.timeout, false);

    return true;
}
