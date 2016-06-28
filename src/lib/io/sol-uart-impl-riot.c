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
#include "sol-reentrant.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

struct sol_uart {
    ssize_t (*on_data)(void *data, struct sol_uart *uart, const struct sol_buffer *buf);
    void (*on_feed_done)(void *data, struct sol_uart *uart, struct sol_blob *blob, int status);
    const void *user_data;
    struct sol_timeout *tx_writer;
    struct sol_timeout *rx_reader;
    void *handler;
    struct sol_ptr_vector pending_blobs;
    struct sol_buffer rx;
    size_t pending_feed;
    size_t feed_size;
    int id;
    struct sol_reentrant reentrant;
};

static void
uart_tx_dispatch(struct sol_uart *uart, struct sol_blob *blob, int status)
{
    if (!uart->on_feed_done)
        return;
    uart->on_feed_done((void *)uart->user_data, uart, blob, status);
}

static void
close_uart(struct sol_uart *uart)
{
    struct sol_blob *blob;
    uint16_t i;

    if (uart->tx_writer) {
        sol_timeout_del(uart->tx_writer);
        uart->tx_writer = NULL;
    }

    if (uart->rx_reader) {
        sol_timeout_del(uart->rx_reader);
        uart->rx_reader = NULL;
    }

    if (uart->handler) {
        sol_interrupt_scheduler_uart_stop(uart->id, uart->handler);
        uart->handler = NULL;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&uart->pending_blobs, blob, i) {
        uart_tx_dispatch(uart, blob, -ECANCELED);
        sol_blob_unref(blob);
    }

    if (uart->rx.used)
        uart->on_data((void *)uart->user_data, uart, &uart->rx);

    uart_poweroff(uart->id);
    sol_ptr_vector_clear(&uart->pending_blobs);
    sol_buffer_fini(&uart->rx);
    free(uart);
}

static bool
rx_timeout(void *data)
{
    struct sol_uart *uart = data;
    bool keep_running = true;
    ssize_t r;
    int err;

    SOL_REENTRANT_CALL(uart->reentrant) {
        r = uart->on_data((void *)uart->user_data, uart, &uart->rx);
    }
    SOL_INT_CHECK(r, < 0, true);

    err = sol_buffer_remove_data(&uart->rx, 0, r);
    SOL_INT_CHECK(err, < 0, true);

    if (!uart->rx.used) {
        uart->rx_reader = NULL;
        keep_running = false;
    }

    if (uart->reentrant.delete_me) {
        SOL_REENTRANT_FREE(uart->reentrant) {
            close_uart(uart);
        }
    }

    return keep_running;
}

static void
uart_on_data(void *arg, uint8_t data)
{
    struct sol_uart *uart = arg;
    int r;

    if (!uart->on_data)
        return;

    r = sol_buffer_append_char(&uart->rx, (char)data);
    SOL_INT_CHECK(r, < 0);

    if (!uart->rx_reader) {
        uart->rx_reader = sol_timeout_add(0, rx_timeout, uart);
        SOL_NULL_CHECK(uart->rx_reader);
    }
}

static bool
uart_tx(void *arg)
{
    struct sol_uart *uart = arg;
    struct sol_blob *blob;
    bool r = true;

    blob = sol_ptr_vector_steal(&uart->pending_blobs, 0);

    uart_write(uart->id, blob->mem, blob->size);

    uart->pending_feed -= blob->size;

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
    size_t data_size;
    void *rx_buf = NULL;
    struct sol_uart *uart;
    enum sol_buffer_flags flags = SOL_BUFFER_FLAGS_NO_NUL_BYTE | SOL_BUFFER_FLAGS_DEFAULT;
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
        SOL_WRN("Couldn't open UART that has unsupported version '%" PRIu16 "', "
            "expected version is '%" PRIu16 "'",
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
        baud_rata_table[config->baud_rate], uart_on_data, uart,
        &uart->handler);
    SOL_INT_CHECK_GOTO(ret, != 0, fail);

    uart->on_data = config->on_data;
    uart->on_feed_done = config->on_feed_done;
    uart->user_data = config->user_data;
    sol_ptr_vector_init(&uart->pending_blobs);

    data_size = config->data_buffer_size;
    if (data_size) {
        flags |= SOL_BUFFER_FLAGS_FIXED_CAPACITY;
        rx_buf = malloc(data_size);
        SOL_NULL_CHECK_GOTO(rx_buf, err_buf);
    }

    uart->feed_size = config->feed_size;
    sol_buffer_init_flags(&uart->rx, rx_buf, data_size, flags);

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
    SOL_NULL_CHECK(uart);
    SOL_EXP_CHECK(uart->reentrant.delete_me);

    SOL_REENTRANT_FREE(uart->reentrant) {
        close_uart(uart);
    }
}

SOL_API int
sol_uart_feed(struct sol_uart *uart, struct sol_blob *blob)
{
    bool created_writer = false;
    size_t total;
    int r;

    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);
    SOL_EXP_CHECK(uart->reentrant.delete_me, -EINVAL);

    r = sol_util_size_add(uart->pending_feed, blob->size, &total);
    SOL_INT_CHECK(r, < 0, r);

    if (uart->feed_size && total >= uart->feed_size)
        return -ENOSPC;

    if (!uart->tx_writer) {
        uart->tx_writer = sol_timeout_add(0, uart_tx, uart);
        SOL_NULL_CHECK(uart->tx_writer, -ENOMEM);
        created_writer = true;
    }


    r = -EOVERFLOW;
    SOL_NULL_CHECK_GOTO(sol_blob_ref(blob), err_blob);
    r = sol_ptr_vector_append(&uart->pending_blobs, blob);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);
    uart->pending_feed = total;
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
