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

// Riot includes
#include <periph/uart.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-uart.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-interrupt_scheduler_riot.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

struct sol_uart {
    uart_t id;
    struct {
        void *handler;

        void (*rx_cb)(void *data, struct sol_uart *uart, uint8_t byte_read);
        const void *rx_user_data;

        void (*tx_cb)(void *data, struct sol_uart *uart, uint8_t *tx, int status);
        const void *tx_user_data;
        struct sol_timeout *tx_writer;
        const uint8_t *tx_buffer;
        unsigned int tx_length;
    } async;
};

static void
uart_rx_cb(void *arg, char data)
{
    struct sol_uart *uart = arg;

    if (!uart->async.rx_cb)
        return;
    uart->async.rx_cb((void *)uart->async.rx_user_data, uart,
        (uint8_t)data);
}

static void
uart_tx_dispatch(struct sol_uart *uart, int status)
{
    uint8_t *tx = (uint8_t *)uart->async.tx_buffer;

    uart->async.tx_buffer = NULL;
    if (!uart->async.tx_cb)
        return;
    uart->async.tx_cb((void *)uart->async.tx_user_data, uart, tx, status);
}

static bool
uart_tx_cb(void *arg)
{
    struct sol_uart *uart = arg;

    uart_write(uart->id, uart->async.tx_buffer, uart->async.tx_length);
    uart->async.tx_writer = NULL;
    uart_tx_dispatch(uart, uart->async.tx_length);

    return false;
}

SOL_API struct sol_uart *
sol_uart_open(const char *port_name, const struct sol_uart_config *config)
{
    struct sol_uart *uart;
    const unsigned int baud_rata_table[] = {
        [SOL_UART_BAUD_RATE_9600] = 9600,
        [SOL_UART_BAUD_RATE_19200] = 19200,
        [SOL_UART_BAUD_RATE_38400] = 38400,
        [SOL_UART_BAUD_RATE_57600] = 57600,
        [SOL_UART_BAUD_RATE_115200] = 115200
    };
    int ret;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_UART_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open UART that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_UART_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    SOL_EXP_CHECK(config->parity != SOL_UART_PARITY_NONE, NULL);
    SOL_EXP_CHECK(config->data_bits != SOL_UART_DATA_BITS_8, NULL);
    SOL_EXP_CHECK(config->stop_bits != SOL_UART_STOP_BITS_ONE, NULL);
    SOL_EXP_CHECK(config->flow_control, NULL);

    SOL_NULL_CHECK(port_name, NULL);
    uart = calloc(1, sizeof(struct sol_uart));
    SOL_NULL_CHECK(uart, NULL);

    uart->id = strtol(port_name, NULL, 10);
    uart_poweron(uart->id);
    ret = sol_interrupt_scheduler_uart_init_int(uart->id,
        baud_rata_table[config->baud_rate], uart_rx_cb, uart,
        &uart->async.handler);
    SOL_INT_CHECK_GOTO(ret, != 0, fail);

    uart->async.rx_cb = config->rx_cb;
    uart->async.rx_user_data = config->rx_cb_user_data;
    uart->async.tx_buffer = NULL;
    return uart;

fail:
    free(uart);
    return NULL;
}

SOL_API void
sol_uart_close(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart);

    if (uart->async.tx_writer) {
        sol_timeout_del(uart->async.tx_writer);
        uart_tx_dispatch(uart, -1);
    }

    if (uart->async.handler)
        sol_interrupt_scheduler_uart_stop(uart->id, uart->async.handler);
    uart_poweroff(uart->id);

    free(uart);
}

SOL_API bool
sol_uart_write(struct sol_uart *uart, const uint8_t *tx, unsigned int length, void (*tx_cb)(void *data, struct sol_uart *uart, uint8_t *tx, int status), const void *data)
{
    SOL_NULL_CHECK(uart, false);
    SOL_EXP_CHECK(uart->async.tx_buffer, false);

    uart->async.tx_buffer = tx;
    uart->async.tx_cb = tx_cb;
    uart->async.tx_user_data = data;
    uart->async.tx_length = length;

    uart->async.tx_writer = sol_timeout_add(0, uart_tx_cb, uart);

    return true;
}
