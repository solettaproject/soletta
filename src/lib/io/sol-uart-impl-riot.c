/*
 * This file is part of the Soletta Project
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

// Riot includes
#include <periph/uart.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-uart.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-interrupt_scheduler_riot.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

#define DEFAULT_BUFFER_SIZE (128)

struct sol_uart {
    ssize_t (*rx_cb)(void *data, struct sol_uart *uart, const struct sol_buffer *buf);
    void (*tx_cb)(void *data, struct sol_uart *uart, struct sol_blob *blob, int status);
    const void *user_data;
    struct sol_timeout *tx_writer;
    struct sol_timeout *rx_reader;
    void *handler;
    struct sol_ptr_vector pending_blobs;
    struct sol_buffer rx_buffer;
    size_t pending_tx;
    size_t tx_size;
    int id;
};

static bool
rx_timeout(void *data)
{
    struct sol_uart *uart = data;
    ssize_t r;

    r = uart->rx_cb((void *)uart->user_data, uart, &uart->rx_buffer);
    SOL_INT_CHECK(r, < 0, true);

    r = sol_buffer_remove_data(&uart->rx_buffer, 0, r);
    SOL_INT_CHECK(r, < 0, true);

    if (uart->rx_buffer.used)
        return true;

    uart->rx_reader = NULL;
    return false;
}

static void
uart_rx_cb(void *arg, uint8_t data)
{
    struct sol_uart *uart = arg;
    int r;

    if (!uart->rx_cb)
        return;

    r = sol_buffer_append_char(&uart->rx_buffer, (char)data);
    SOL_INT_CHECK(r, < 0);

    if (!uart->rx_reader) {
        uart->rx_reader = sol_timeout_add(0, rx_timeout, uart);
        SOL_NULL_CHECK(uart->rx_reader);
    }
}

static void
uart_tx_dispatch(struct sol_uart *uart, struct sol_blob *blob, int status)
{
    if (!uart->tx_cb)
        return;
    uart->tx_cb((void *)uart->user_data, uart, blob, status);
}

static bool
uart_tx_cb(void *arg)
{
    struct sol_uart *uart = arg;
    struct sol_blob *blob;
    bool r = true;

    blob = sol_ptr_vector_take(&uart->pending_blobs, 0);

    uart_write(uart->id, blob->mem, blob->size);

    uart->pending_tx -= blob->size;

    if (!sol_ptr_vector_get_len(&uart->pending_blobs)) {
        uart->tx_writer = NULL;
        r = false;
    }

    uart_tx_dispatch(uart, blob, 0);
    sol_blob_unref(blob);
    return r;
}

SOL_API struct sol_uart *
sol_uart_open(const char *port_name, const struct sol_uart_config *config)
{
    size_t rx_size;
    void *rx_buf;
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
        &uart->handler);
    SOL_INT_CHECK_GOTO(ret, != 0, fail);

    uart->rx_cb = config->rx_cb;
    uart->tx_cb = config->tx_cb;
    uart->user_data = config->user_data;
    sol_ptr_vector_init(&uart->pending_blobs);

    rx_size = config->rx_size;
    if (!rx_size)
        rx_size = DEFAULT_BUFFER_SIZE;
    uart->tx_size = config->tx_size;

    rx_buf = malloc(rx_size);
    SOL_NULL_CHECK_GOTO(rx_buf, err_buf);
    sol_buffer_init_flags(&uart->rx_buffer, rx_buf, rx_size,
        SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    return uart;

err_buf:
    sol_interrupt_scheduler_uart_stop(uart->id, uart->handler);
fail:
    uart_poweroff(uart->id);
    free(uart);
    return NULL;
}

SOL_API void
sol_uart_close(struct sol_uart *uart)
{
    struct sol_blob *blob;
    uint16_t i;

    SOL_NULL_CHECK(uart);

    SOL_PTR_VECTOR_FOREACH_IDX (&uart->pending_blobs, blob, i) {
        uart_tx_dispatch(uart, blob, -ECANCELED);
        sol_blob_unref(blob);
    }

    if (uart->tx_writer)
        sol_timeout_del(uart->tx_writer);

    if (uart->rx_reader)
        sol_timeout_del(uart->rx_reader);

    if (uart->handler)
        sol_interrupt_scheduler_uart_stop(uart->id, uart->handler);

    uart_poweroff(uart->id);
    sol_ptr_vector_clear(&uart->pending_blobs);
    sol_buffer_fini(&uart->rx_buffer);
    free(uart);
}

SOL_API int
sol_uart_write(struct sol_uart *uart, struct sol_blob *blob)
{
    bool created_writer = false;
    size_t total;
    int r;

    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    if (uart->tx_size) {
        r = sol_util_size_add(uart->pending_tx, blob->size, &total);
        SOL_INT_CHECK(r, < 0, r);

        if (total >= uart->tx_size)
            return -ENOSPC;
    }

    if (!uart->tx_writer) {
        uart->tx_writer = sol_timeout_add(0, uart_tx_cb, uart);
        SOL_NULL_CHECK(uart->tx_writer, -ENOMEM);
        created_writer = true;
    }


    r = -EOVERFLOW;
    SOL_NULL_CHECK_GOTO(sol_blob_ref(blob), err_blob);
    r = sol_ptr_vector_append(&uart->pending_blobs, blob);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);
    uart->pending_tx += blob->size;
    return 0;

err_append:
    sol_blob_unref(blob);
err_blob:
    if (created_writer) {
        sol_timeout_del(uart->tx_writer);
        uart->tx_writer = NULL;
    }
    return r;
}
