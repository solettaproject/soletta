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

#include <qm_gpio.h>
#include <qm_interrupt.h>
#include <qm_scss.h>
#include <qm_spi.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "spi");

#include "sol-event-handler-contiki.h"
#include "sol-mainloop.h"
#include "sol-spi.h"

struct sol_spi {
    qm_spi_t bus;
    qm_spi_slave_select_t slave;
    qm_spi_config_t config;
    struct {
        qm_gpio_t port;
        uint8_t pin;
    } slave_select;
    struct {
        qm_spi_async_transfer_t xfer;
        void (*cb)(void *data, struct sol_spi *spi, const uint8_t *tx,
            uint8_t *rx, ssize_t status);
        const void *data;
        ssize_t status;
    } xfer;
};

static process_event_t spi_irq_event;
extern struct process soletta_app_process;

static struct sol_spi *in_transfer[QM_SPI_NUM];

static void
spi_cb_dispatch(void *user_data, process_event_t ev, process_data_t ev_data)
{
    struct sol_spi *spi = (struct sol_spi *)ev_data;

    qm_gpio_set_pin(spi->slave_select.port, spi->slave_select.pin);
    spi->xfer.cb((void *)spi->xfer.data, spi, spi->xfer.xfer.tx,
        spi->xfer.xfer.rx, spi->xfer.status);
}

static void
spi_irq_dispatch(uint32_t id, ssize_t status)
{
    struct sol_spi *spi;

    if ((spi = in_transfer[id]) == NULL)
        return;
    spi->xfer.status = status;
    process_post(&soletta_app_process, spi_irq_event, spi);
}

static void
tx_callback(uint32_t id, uint32_t len)
{
}

static void
rx_callback(uint32_t id, uint32_t len)
{
    spi_irq_dispatch(id, len);
}

static void
err_callback(uint32_t id, qm_rc_t status)
{
    spi_irq_dispatch(id, -status);
}

static int
spi_set_gpio_ss(struct sol_spi *spi)
{
    qm_gpio_port_config_t cfg;
    uint32_t mask;
    qm_rc_t ret;

#if QUARK_SE
    spi->slave_select.port = QM_GPIO_0;
    switch (spi->bus) {
    case QM_SPI_MST_0:
        switch (spi->slave) {
        case QM_SPI_SS_0:
            spi->slave_select.pin = 24;
            break;
        case QM_SPI_SS_1:
            spi->slave_select.pin = 25;
            break;
        case QM_SPI_SS_2:
            spi->slave_select.pin = 26;
            break;
        case QM_SPI_SS_3:
            spi->slave_select.pin = 27;
            break;
        default:
            return -EINVAL;
        }
        break;
    case QM_SPI_MST_1:
        switch (spi->slave) {
        case QM_SPI_SS_0:
            spi->slave_select.pin = 11;
            break;
        case QM_SPI_SS_1:
            spi->slave_select.pin = 12;
            break;
        case QM_SPI_SS_2:
            spi->slave_select.pin = 13;
            break;
        case QM_SPI_SS_3:
            spi->slave_select.pin = 14;
            break;
        default:
            return -EINVAL;
        }
        break;
    default:
        return -EINVAL;
    }
#elif QUARK_D2000
    spi->slave_select.port = QM_GPIO_0;
    switch (spi->slave) {
    case QM_SPI_SS_0:
        spi->slave_select.pin = 0;
        break;
    case QM_SPI_SS_1:
        spi->slave_select.pin = 1;
        break;
    case QM_SPI_SS_2:
        spi->slave_select.pin = 2;
        break;
    case QM_SPI_SS_3:
        spi->slave_select.pin = 3;
        break;
    default:
        return -EINVAL;
    }
#endif

    mask = BIT(spi->slave_select.pin);
    ret = qm_gpio_get_config(spi->slave_select.port, &cfg);
    SOL_EXP_CHECK(ret != QM_RC_OK, -EIO);
    cfg.direction |= mask;
    cfg.int_en &= mask;
    ret = qm_gpio_set_config(spi->slave_select.port, &cfg);
    SOL_EXP_CHECK(ret != QM_RC_OK, -EIO);
    qm_gpio_set_pin(spi->slave_select.port, spi->slave_select.pin);

    return 0;
}

SOL_API struct sol_spi *
sol_spi_open(unsigned int bus, const struct sol_spi_config *config)
{
    struct sol_spi *spi;
    qm_spi_t max_bus_available;
    int ret;

    SOL_LOG_INTERNAL_INIT_ONCE;

    /* QM_SPI_NUM is always considering that both master and the slave
     * exist, so we can't use it to check the valid buses to use */
#if QUARK_SE
    max_bus_available = QM_SPI_MST_1;
#else
    max_bus_available = QM_SPI_MST_0;
#endif

    SOL_EXP_CHECK(bus >= max_bus_available, NULL);
    SOL_NULL_CHECK(config, NULL);

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_SPI_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open SPI that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_SPI_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    if (config->chip_select > 3) {
        SOL_WRN("Invalid chip_select value '%u'. Value must be between 0 and 3.",
            config->chip_select);
        return NULL;
    }

    if ((config->bits_per_word < 4) || (config->bits_per_word > 32)) {
        SOL_WRN("Invalid bits_per_word value '%" PRIu8 "'. Value must be "
            "between 4 and 32.", config->bits_per_word);
        return NULL;
    }

    spi = calloc(1, sizeof(*spi));
    SOL_NULL_CHECK(spi, NULL);

    if (!spi_irq_event) {
        bool r;

        spi_irq_event = process_alloc_event();
        r = sol_mainloop_contiki_event_handler_add(&spi_irq_event, NULL,
            spi_cb_dispatch, NULL);
        SOL_EXP_CHECK_GOTO(!r, error);
    }

    spi->bus = bus;
    spi->slave = BIT(config->chip_select);
    spi->config.frame_size = config->bits_per_word - 1;
    spi->config.transfer_mode = QM_SPI_TMOD_TX_RX;
    spi->config.bus_mode = config->mode;
    spi->config.clk_divider = 32000000 / config->frequency;

    switch (spi->bus) {
    case QM_SPI_MST_0:
        clk_periph_enable(CLK_PERIPH_CLK | CLK_PERIPH_SPI_M0_REGISTER);
        qm_irq_request(QM_IRQ_SPI_MASTER_0, qm_spi_master_0_isr);
        break;
#if QUARK_SE
    case QM_SPI_MST_1:
        qm_irq_request(QM_IRQ_SPI_MASTER_1, qm_spi_master_1_isr);
        break;
#endif
    case QM_SPI_SLV_0:
    case QM_SPI_NUM:
        /* We checked if we were passed the limit before, so we should never
         * hit this point. Using all the enum values and no default, however,
         * allows us to rely on the compiler to know if there are values
         * we are not considering (depending on warning levels) */
        break;
    }

    ret = spi_set_gpio_ss(spi);
    SOL_INT_CHECK_GOTO(ret, < 0, error);

    return spi;

error:
    free(spi);
    return NULL;
}

SOL_API void
sol_spi_close(struct sol_spi *spi)
{
    SOL_NULL_CHECK(spi);

    if (in_transfer[spi->bus] == spi) {
        qm_spi_transfer_terminate(spi->bus);
        in_transfer[spi->bus] = NULL;
    }

    free(spi);
}

SOL_API int
sol_spi_transfer(struct sol_spi *spi, const uint8_t *tx, uint8_t *rx,
    size_t count, void (*transfer_cb)(void *cb_data, struct sol_spi *spi,
    const uint8_t *tx, uint8_t *rx, ssize_t status), const void *cb_data)
{
    qm_rc_t ret;

    SOL_NULL_CHECK(spi, -EINVAL);
    SOL_INT_CHECK(count, == 0, -EINVAL);

    if (qm_spi_get_status(spi->bus) == QM_SPI_BUSY)
        return -EBUSY;

    spi->xfer.xfer.tx = (uint8_t *)tx;
    spi->xfer.xfer.tx_len = count;
    spi->xfer.xfer.rx = (uint8_t *)rx;
    spi->xfer.xfer.rx_len = count;
    spi->xfer.xfer.tx_callback = tx_callback;
    spi->xfer.xfer.rx_callback = rx_callback;
    spi->xfer.xfer.err_callback = err_callback;
    spi->xfer.xfer.id = spi->bus;

    spi->xfer.cb = transfer_cb;
    spi->xfer.data = cb_data;

    ret = qm_spi_set_config(spi->bus, &spi->config);
    SOL_EXP_CHECK(ret != QM_RC_OK, -EINVAL);

    ret = qm_spi_slave_select(spi->bus, spi->slave);
    SOL_EXP_CHECK(ret != QM_RC_OK, -EINVAL);

    qm_gpio_clear_pin(spi->slave_select.port, spi->slave_select.pin);

    ret = qm_spi_irq_transfer(spi->bus, &spi->xfer.xfer);
    SOL_EXP_CHECK(ret != QM_RC_OK, -EINVAL);

    in_transfer[spi->xfer.xfer.id] = spi;

    return 0;
}
