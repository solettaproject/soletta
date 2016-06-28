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

    state = irq_disable();
    if (base->pending || base->in_cb)
        base->deleted = true;
    else
        free(base);
    irq_restore(state);
}

#ifdef USE_GPIO
/* Run in interrupt context */
static void
gpio_cb(void *data)
{
    interrupt_scheduler_notify_main_thread(GPIO, data);
}

int
sol_interrupt_scheduler_gpio_init_int(gpio_t dev, gpio_mode_t mode, gpio_flank_t flank, gpio_cb_t cb, const void *arg, void **handler)
{
    struct gpio_interrupt_data *int_data;
    int ret;

    int_data = calloc(1, sizeof(*int_data));
    SOL_NULL_CHECK(int_data, -ENOMEM);

    int_data->cb = cb;
    int_data->data = arg;

    ret = gpio_init_int(dev, mode, flank, gpio_cb, int_data);
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

    state = irq_disable();
    gpio_irq_disable(dev);
    interrupt_scheduler_handler_free(handler);
    irq_restore(state);
}
#endif

/* Run in interrupt context */
#ifdef USE_UART
static void
uart_rx_cb(void *data, uint8_t char_read)
{
    struct uart_interrupt_data *int_data = data;

    if (!int_data)
        return;

    int_data->buf[int_data->buf_next_write] = char_read;
    int_data->buf_next_write = (int_data->buf_next_write + 1) % int_data->buf_len;

    interrupt_scheduler_notify_main_thread(UART_RX, &int_data->base);
}

int
sol_interrupt_scheduler_uart_init_int(uart_t uart, uint32_t baudrate, uart_rx_cb_t rx_cb, const void *arg, void **handler)
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
    int_data->data = arg;
    int_data->buf_len = buf_size;

    ret = uart_init(uart, baudrate, uart_rx_cb, int_data);
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
    uart_init(uart, 9600, uart_rx_cb, NULL);
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

        state = irq_disable();
        int_data->base.pending = false;
        irq_restore(state);

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

        state = irq_disable();
        start = int_data->buf_next_read;
        end = int_data->buf_next_write;
        len = int_data->buf_len;
        int_data->base.pending = false;
        irq_restore(state);

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
