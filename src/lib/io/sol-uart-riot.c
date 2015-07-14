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
#include "sol-uart.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"
#include "sol-interrupt_scheduler_riot.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

struct sol_uart {
    uart_t id;
    struct {
        void *handler;

        void (*rx_cb)(void *data, struct sol_uart *uart, char read_char);
        const void *rx_user_data;

        struct sol_vector tx_queue;
    } async;
};

struct uart_write_data {
    char *buffer;
    unsigned int length;
    unsigned int index;
    void (*cb)(void *data, struct sol_uart *uart, int write);
    const void *user_data;
    struct sol_uart *uart;
};

static void
uart_rx_cb(void *arg, char data)
{
    struct sol_uart *uart = arg;

    if (!uart->async.rx_cb)
        return;
    uart->async.rx_cb((void *)uart->async.rx_user_data, uart, data);
}

static void
uart_dispatch_write_data(struct uart_write_data *write_data, int write)
{
    free(write_data->buffer);
    write_data->cb((void *)write_data->user_data, write_data->uart, write);
}

static int
uart_tx_cb(void *arg)
{
    struct sol_uart *uart = arg;
    struct uart_write_data *write_data;

    write_data = sol_vector_get(&uart->async.tx_queue, 0);
    if (!write_data)
        return 0;

    if (uart_write(uart->id, write_data->buffer[write_data->index]) == -1) {
        uint16_t i;

        SOL_ERR("Error when writing to UART %d.", uart->id);

        SOL_VECTOR_FOREACH_IDX (&uart->async.tx_queue, write_data, i)
            uart_dispatch_write_data(write_data, -1);
        sol_vector_clear(&uart->async.tx_queue);

        return 0;
    }

    write_data->index++;

    if (write_data->index == write_data->length) {
        uart_dispatch_write_data(write_data, write_data->index);
        sol_vector_del(&uart->async.tx_queue, 0);

        return !!uart->async.tx_queue.len;
    }

    return 1;
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

    if (unlikely(config->api_version != SOL_UART_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open UART that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_UART_CONFIG_API_VERSION);
        return NULL;
    }

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
        baud_rata_table[config->baud_rate], uart_rx_cb, uart_tx_cb, uart,
        &uart->async.handler);
    SOL_INT_CHECK_GOTO(ret, != 0, fail);

    uart->async.rx_cb = config->rx_cb;
    uart->async.rx_user_data = config->rx_cb_user_data;
    sol_vector_init(&uart->async.tx_queue, sizeof(struct uart_write_data));
    return uart;

fail:
    free(uart);
    return NULL;
}

SOL_API void
sol_uart_close(struct sol_uart *uart)
{
    struct uart_write_data *write_data;
    uint16_t i;

    SOL_NULL_CHECK(uart);
    if (uart->async.handler)
        sol_interrupt_scheduler_uart_stop(uart->id, uart->async.handler);
    uart_poweroff(uart->id);

    SOL_VECTOR_FOREACH_IDX (&uart->async.tx_queue, write_data, i) {
        free(write_data->buffer);
        write_data->cb((void *)write_data->user_data, uart, -1);
    }
    sol_vector_clear(&uart->async.tx_queue);

    free(uart);
}

SOL_API bool
sol_uart_write(struct sol_uart *uart, const char *tx, unsigned int length, void (*tx_cb)(void *data, struct sol_uart *uart, int status), const void *data)
{
    struct uart_write_data *write_data;

    SOL_NULL_CHECK(uart, false);

    write_data = sol_vector_append(&uart->async.tx_queue);
    SOL_NULL_CHECK(write_data, false);

    write_data->buffer = malloc(length);
    SOL_NULL_CHECK_GOTO(write_data->buffer, malloc_buffer_fail);

    memcpy(write_data->buffer, tx, length);
    write_data->cb = tx_cb;
    write_data->user_data = data;
    write_data->index = 0;
    write_data->length = length;
    write_data->uart = uart;

    if (uart->async.tx_queue.len == 1)
        uart_tx_begin(uart->id);

    return true;

malloc_buffer_fail:
    sol_vector_del(&uart->async.tx_queue, uart->async.tx_queue.len - 1);
    return false;
}
