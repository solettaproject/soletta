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
#include <stdbool.h>
#include <stdlib.h>

#include <thread.h>

#include "sol-interrupt_scheduler_riot.h"
#include "sol-log.h"

static kernel_pid_t pid;

enum interrupt_type {
#ifdef USE_GPIO
    GPIO,
#endif
#ifdef USE_UART
    UART_RX,
    UART_TX
#endif
};

struct interrupt_data_base {
    bool pending : 1;
    bool in_cb : 1;
    bool deleted : 1;
};

#ifdef USE_GPIO
struct gpio_interrupt_data {
    struct interrupt_data_base base;
    gpio_cb_t cb;
    const void *data;
};
#endif

#ifdef USE_UART
struct uart_interrupt_data {
    struct interrupt_data_base base;
    uart_t uart_id;
    uart_rx_cb_t rx_cb;
    uart_tx_cb_t tx_cb;
    const void *data;
    uint16_t buf_len;
    uint16_t buf_next_read;
    uint16_t buf_next_write;
    char buf[];
};
#endif

void
sol_interrupt_scheduler_set_pid(kernel_pid_t p)
{
    pid = p;
}

kernel_pid_t
sol_interrupt_scheduler_get_pid(void)
{
    return pid;
}

static void
interrupt_scheduler_notify_main_thread(uint16_t type, struct interrupt_data_base *handler)
{
    msg_t m;

    if (handler->pending)
        return;

    handler->pending = true;

    m.type = type;
    m.content.ptr = (char *)handler;
    msg_send_int(&m, pid);
}

static void
interrupt_scheduler_handler_free(void *handler)
{
    struct interrupt_data_base *base = handler;
    unsigned int state;

    state = disableIRQ();
    if (base->pending || base->in_cb)
        base->deleted = true;
    else
        free(base);
    restoreIRQ(state);
}

#ifdef USE_GPIO
/* Run in interrupt context */
static void
gpio_cb(void *data)
{
    interrupt_scheduler_notify_main_thread(GPIO, data);
}

int
sol_interrupt_scheduler_gpio_init_int(gpio_t dev, gpio_pp_t pullup, gpio_flank_t flank, gpio_cb_t cb, const void *arg, void **handler)
{
    struct gpio_interrupt_data *int_data;
    int ret;

    int_data = calloc(1, sizeof(*int_data));
    SOL_NULL_CHECK(int_data, -ENOMEM);

    int_data->cb = cb;
    int_data->data = arg;

    ret = gpio_init_int(dev, pullup, flank, gpio_cb, int_data);
    SOL_INT_CHECK_GOTO(ret, < 0, error);

    *handler = int_data;

    return 0;
error:
    free(int_data);
    return ret;
}

void
sol_interrupt_scheduler_gpio_stop(gpio_t dev, void *handler)
{
    unsigned int state;

    state = disableIRQ();
    gpio_irq_disable(dev);
    interrupt_scheduler_handler_free(handler);
    restoreIRQ(state);
}
#endif

/* Run in interrupt context */
#ifdef USE_UART
static void
uart_rx_cb(void *data, char char_read)
{
    struct uart_interrupt_data *int_data = data;

    if (!int_data)
        return;

    int_data->buf[int_data->buf_next_write] = char_read;
    int_data->buf_next_write = (int_data->buf_next_write + 1) % int_data->buf_len;

    interrupt_scheduler_notify_main_thread(UART_RX, &int_data->base);
}

/* Run in interrupt context */
static int
uart_tx_cb(void *data)
{
    if (!data)
        return 0;

    interrupt_scheduler_notify_main_thread(UART_TX, data);
    return 0;
}

int
sol_interrupt_scheduler_uart_init_int(uart_t uart, uint32_t baudrate, uart_rx_cb_t rx_cb, uart_tx_cb_t tx_cb, const void *arg, void **handler)
{
    struct uart_interrupt_data *int_data;
    uint16_t buf_size;
    int ret;

    /* buffer size for rx, basically enough bytes for 0.01 seconds */
    buf_size = baudrate / 800;

    int_data = calloc(1, sizeof(*int_data) + buf_size);
    SOL_NULL_CHECK(int_data, -ENOMEM);

    int_data->uart_id = uart;
    int_data->rx_cb = rx_cb;
    int_data->tx_cb = tx_cb;
    int_data->data = arg;
    int_data->buf_len = buf_size;

    ret = uart_init(uart, baudrate, uart_rx_cb, uart_tx_cb, int_data);
    SOL_INT_CHECK_GOTO(ret, < 0, error);

    *handler = int_data;

    return 0;
error:
    free(int_data);
    return ret;
}

void
sol_interrupt_scheduler_uart_stop(uart_t uart, void *handler)
{
    /*
     * Looking at RIOT's code, there are not guarantees that using
     * uart_init_blocking() will not keep the previously set interruptions
     * alive, and uart_poweroff() may not always be implemented, so the only
     * safe way to stop handling interruptions is to keep using our functions
     * with a NULL data pointer and check for that there, doing nothing if
     * that's the case. If uart_poweroff() works, it will be called by the
     * sol_uart implementation after this function.
     */
    uart_init(uart, 9600, uart_rx_cb, uart_tx_cb, NULL);
    interrupt_scheduler_handler_free(handler);
}
#endif

void
sol_interrupt_scheduler_process(msg_t *msg)
{
    unsigned int state;

    switch (msg->type) {
#ifdef USE_GPIO
    case GPIO: {
        struct gpio_interrupt_data *int_data = (void *)msg->content.ptr;

        state = disableIRQ();
        int_data->base.pending = false;
        restoreIRQ(state);

        if (int_data->base.deleted)
            interrupt_scheduler_handler_free(int_data);
        else
            int_data->cb((void *)int_data->data);
        break;
    }
#endif
#ifdef USE_UART
    case UART_RX: {
        struct uart_interrupt_data *int_data = (void *)msg->content.ptr;
        uint16_t start, end, len;

        state = disableIRQ();
        start = int_data->buf_next_read;
        end = int_data->buf_next_write;
        len = int_data->buf_len;
        int_data->base.pending = false;
        restoreIRQ(state);

        int_data->base.in_cb = true;
        while (!int_data->base.deleted) {
            if (start == end)
                break;
            int_data->rx_cb((void *)int_data->data, int_data->buf[start]);
            start = (start + 1) % len;
        }
        int_data->base.in_cb = false;
        if (int_data->base.deleted)
            interrupt_scheduler_handler_free(int_data);
        else
            int_data->buf_next_read = start;
        break;
    }
    case UART_TX: {
        struct uart_interrupt_data *int_data = (void *)msg->content.ptr;

        if (int_data->base.deleted)
            interrupt_scheduler_handler_free(int_data);
        else {
            if (int_data->tx_cb((void *)int_data->data))
                uart_tx_begin(int_data->uart_id);
        }
        break;
    }
#endif
#ifdef NETWORK
    case GNRC_NETAPI_MSG_TYPE_RCV:
    case GNRC_NETAPI_MSG_TYPE_SND:
    case GNRC_NETAPI_MSG_TYPE_SET:
    case GNRC_NETAPI_MSG_TYPE_GET:
        sol_network_msg_dispatch(msg);
        break;
#endif
    }
}
