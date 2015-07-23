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
#include "sol-mainloop.h"
#include "sol-spi.h"
#include "sol-util.h"
#ifdef PTHREAD
#include "sol-worker-thread.h"
#endif

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "spi");

struct sol_spi {
    int fd;
    unsigned int bus;
    unsigned int chip_select;
    uint8_t bits_per_word;

    struct {
        void (*cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status);
        const void *cb_data;
        const uint8_t *tx;
        uint8_t *rx;
#ifdef PTHREAD
        struct sol_worker_thread *worker;
#else
        struct sol_timeout *timeout;
#endif
        size_t count;
        ssize_t status;
    } transfer;
};

static void
spi_transfer(struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, size_t size)
{
    struct spi_ioc_transfer tr;

    memset(&tr, 0, sizeof(struct spi_ioc_transfer));
    tr.tx_buf = (uintptr_t)tx;
    tr.rx_buf = (uintptr_t)rx;
    tr.len = size;
    tr.bits_per_word = spi->bits_per_word;

    if (ioctl(spi->fd, SPI_IOC_MESSAGE(1), &tr) == -1) {
        SOL_WRN("%u,%u: Unable to perform SPI transfer: %s", spi->bus,
            spi->chip_select, sol_util_strerrora(errno));
        return;
    }

    spi->transfer.status = size;
}

static void
spi_transfer_dispatch(struct sol_spi *spi)
{
    if (!spi->transfer.cb) return;
    spi->transfer.cb((void *)spi->transfer.cb_data, spi, spi->transfer.tx,
        spi->transfer.rx, spi->transfer.status);
}

#ifdef PTHREAD
static void
spi_worker_thread_finished(void *data)
{
    struct sol_spi *spi = data;

    spi->transfer.worker = NULL;
    spi_transfer_dispatch(spi);
}

static bool
spi_worker_thread_iterate(void *data)
{
    struct sol_spi *spi = data;

    spi_transfer(spi, spi->transfer.tx, spi->transfer.rx, spi->transfer.count);
    return false;
}
#else
static bool
spi_timeout_cb(void *data)
{
    struct sol_spi *spi = data;

    spi_transfer(spi, spi->transfer.tx, spi->transfer.rx, spi->transfer.count);
    spi->transfer.timeout = NULL;
    spi_transfer_dispatch(spi);

    return false;
}
#endif

SOL_API bool
sol_spi_transfer(struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, size_t size, void (*transfer_cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status), const void *cb_data)
{
#ifdef PTHREAD
    struct sol_worker_thread_spec spec = {
        .api_version = SOL_WORKER_THREAD_SPEC_API_VERSION,
        .setup = NULL,
        .cleanup = NULL,
        .iterate = spi_worker_thread_iterate,
        .finished = spi_worker_thread_finished,
        .feedback = NULL,
        .data = spi
    };
#endif

    SOL_NULL_CHECK(spi, false);
    SOL_INT_CHECK(size, == 0, false);
#ifdef PTHREAD
    SOL_EXP_CHECK(spi->transfer.worker, false);
#else
    SOL_EXP_CHECK(spi->transfer.timeout, false);
#endif

    spi->transfer.tx = tx;
    spi->transfer.rx = rx;
    spi->transfer.count = size;
    spi->transfer.cb = transfer_cb;
    spi->transfer.cb_data = cb_data;
    spi->transfer.status = -1;

#ifdef PTHREAD
    spi->transfer.worker = sol_worker_thread_new(&spec);
    SOL_NULL_CHECK(spi->transfer.worker, false);
#else
    spi->transfer.timeout = sol_timeout_add(0, spi_timeout_cb, spi);
    SOL_NULL_CHECK(spi->transfer.timeout, false);
#endif

    return true;
}

SOL_API void
sol_spi_close(struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi);

#ifdef PTHREAD
    if (spi->transfer.worker) {
        sol_worker_thread_cancel(spi->transfer.worker);
    }
#else
    if (spi->transfer.timeout) {
        sol_timeout_del(spi->transfer.timeout);
        spi_transfer_dispatch(spi);
    }
#endif
    close(spi->fd);

    free(spi);
}

SOL_API struct sol_spi *
sol_spi_open(unsigned int bus, const struct sol_spi_config *config)
{
    struct sol_spi *spi;
    char spi_dir[PATH_MAX];

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (unlikely(config->api_version != SOL_SPI_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open SPI that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_SPI_CONFIG_API_VERSION);
        return NULL;
    }

    spi = malloc(sizeof(*spi));
    if (!spi) {
        SOL_WRN("%u,%u: Unable to allocate SPI", bus, config->chip_select);
        return NULL;
    }

    spi->bus = bus;
    spi->chip_select = config->chip_select;
    spi->bits_per_word = config->bits_per_word;

    snprintf(spi_dir, sizeof(spi_dir), "/dev/spidev%u.%u", bus,
        config->chip_select);
    spi->fd = open(spi_dir, O_RDWR | O_CLOEXEC);

    if (spi->fd < 0) {
        SOL_WRN("%u,%u: Unable to access SPI device - %s", bus,
            config->chip_select, spi_dir);
        goto open_error;
    }

    if (ioctl(spi->fd, SPI_IOC_WR_MODE, &config->mode) == -1) {
        SOL_WRN("%u,%u: Unable to write SPI mode: %s", spi->bus,
            spi->chip_select, sol_util_strerrora(errno));
        goto config_error;
    }

    if (ioctl(spi->fd, SPI_IOC_WR_MAX_SPEED_HZ, &config->frequency) == -1) {
        SOL_WRN("%u,%u: Unable to write SPI clock frequency: %s", spi->bus,
            spi->chip_select, sol_util_strerrora(errno));
        goto config_error;
    }

#ifdef PTHREAD
    spi->transfer.worker = NULL;
#else
    spi->transfer.timeout = NULL;
#endif
    return spi;

config_error:
    close(spi->fd);
open_error:
    free(spi);
    return NULL;
}
