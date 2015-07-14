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
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-uart.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

#define FD_ERROR_FLAGS (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)

struct sol_uart {
    int fd;
    struct {
        void *rx_fd_handler;
        void (*rx_cb)(struct sol_uart *uart, char read_char, void *data);
        void *rx_user_data;

        void *tx_fd_handler;
        struct sol_vector tx_queue;
    } async;
};

struct uart_write_data {
    char *buffer;
    unsigned int length;
    unsigned int index;
    void (*cb)(struct sol_uart *uart, int write, void *data);
    void *user_data;
};

static bool
uart_rx_callback(void *data, int fd, unsigned int active_flags)
{
    struct sol_uart *uart = data;

    if (active_flags & FD_ERROR_FLAGS) {
        SOL_ERR("Some error flag was set on UART file descriptor %d.", fd);
        return true;
    }
    if (active_flags & SOL_FD_FLAGS_IN) {
        char buf[1];
        int status = read(uart->fd, buf, sizeof(buf));
        if (status > 0)
            uart->async.rx_cb(uart, buf[0], uart->async.rx_user_data);
    }
    return true;
}

SOL_API struct sol_uart *
sol_uart_open(const char *port_name, enum sol_uart_baud_rate baud_rate,
    enum sol_uart_settings settings,
    void (*rx_cb)(struct sol_uart *uart, char read_char, void *user_data),
    const void *rx_cb_user_data)
{
    struct sol_uart *uart;
    struct termios tty;
    char device[PATH_MAX];
    int r;
    const speed_t speed_table[] = {
        [SOL_UART_BAUD_RATE_9600] = B9600,
        [SOL_UART_BAUD_RATE_19200] = B19200,
        [SOL_UART_BAUD_RATE_38400] = B38400,
        [SOL_UART_BAUD_RATE_57600] = B57600,
        [SOL_UART_BAUD_RATE_115200] = B115200
    };
    uint32_t data_bits;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_NULL_CHECK(port_name, NULL);

    r = snprintf(device, sizeof(device), "/dev/%s", port_name);
    SOL_INT_CHECK(r, >= (int)sizeof(device), NULL);

    uart = calloc(1, sizeof(struct sol_uart));
    SOL_NULL_CHECK(uart, NULL);

    uart->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC );
    if (uart->fd < 0) {
        SOL_ERR("Unable to open device %s.", device);
        goto open_fail;
    }

    memset(&tty, 0, sizeof(struct termios));

    if (cfsetospeed(&tty, speed_table[baud_rate]) != 0)
        goto fail;
    if (cfsetispeed(&tty, B0) != 0)
        goto fail;

    if (settings & SOL_UART_DATA_BITS_7)
        data_bits = CS7;
    else if (settings & SOL_UART_DATA_BITS_6)
        data_bits = CS6;
    else if (settings & SOL_UART_DATA_BITS_5)
        data_bits = CS5;
    else
        data_bits = CS8;
    tty.c_cflag |= data_bits;

    if (settings & (SOL_UART_PARITY_EVEN | SOL_UART_PARITY_ODD)) {
        tty.c_cflag |= PARENB;
        tty.c_iflag |= INPCK;
        if (settings & SOL_UART_PARITY_ODD)
            tty.c_cflag |= PARODD;
    }

    if (settings & SOL_UART_STOP_BITS_TWO)
        tty.c_cflag |= CSTOPB;

    if (settings & SOL_UART_FLOW_CONTROL) {
        tty.c_cflag |= CRTSCTS;
        tty.c_iflag |= (IXON | IXOFF | IXANY);
    }

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Unable to set UART configuration.");
        goto fail;
    }
    tcflush(uart->fd, TCIOFLUSH);

    sol_vector_init(&uart->async.tx_queue, sizeof(struct uart_write_data));

    uart->async.rx_fd_handler = sol_fd_add(uart->fd,
        FD_ERROR_FLAGS | SOL_FD_FLAGS_IN, uart_rx_callback, uart);
    if (!uart->async.rx_fd_handler) {
        SOL_ERR("Unable to add file descriptor to watch UART.");
        goto fail;
    }
    uart->async.rx_cb = rx_cb;
    uart->async.rx_user_data = (void *)rx_cb_user_data;

    return uart;

fail:
    close(uart->fd);
open_fail:
    free(uart);
    return NULL;
}

static void
clean_tx_queue(struct sol_uart *uart, int error_code)
{
    struct uart_write_data *write_data;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&uart->async.tx_queue, write_data, i) {
        write_data->cb(uart, -1, write_data->user_data);
        free(write_data->buffer);
    }
    sol_vector_clear(&uart->async.tx_queue);
}

SOL_API void
sol_uart_close(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart);
    if (uart->async.rx_fd_handler)
        sol_fd_del(uart->async.rx_fd_handler);
    if (uart->async.tx_fd_handler)
        sol_fd_del(uart->async.tx_fd_handler);
    close(uart->fd);
    clean_tx_queue(uart, -1);
    free(uart);
}

static bool
uart_tx_callback(void *data, int fd, unsigned int active_flags)
{
    struct sol_uart *uart = data;
    struct uart_write_data *write_data = sol_vector_get(&uart->async.tx_queue, 0);
    int ret;

    if (active_flags & FD_ERROR_FLAGS) {
        SOL_ERR("Some error flag was set on UART file descriptor %d.", fd);
        goto error;
    }

    if (write_data->index == write_data->length) {
        free(write_data->buffer);
        write_data->cb(uart, write_data->index, write_data->user_data);
        sol_vector_del(&uart->async.tx_queue, 0);

        if (!uart->async.tx_queue.len) {
            uart->async.tx_fd_handler = NULL;
            return false;
        }

        return uart_tx_callback(uart, fd, active_flags);
    }

    ret = write(fd, write_data->buffer + write_data->index,
        write_data->length - write_data->index);
    if (ret < 0) {
        SOL_ERR("Error when writing to file descriptor %d.", fd);
        write_data->cb(uart, ret, write_data->user_data);
        free(write_data->buffer);
        sol_vector_del(&uart->async.tx_queue, 0);
        goto error;
    }
    write_data->index += ret;
    return true;

error:
    clean_tx_queue(uart, -1);
    uart->async.tx_fd_handler = NULL;
    return false;
}

SOL_API bool
sol_uart_write(struct sol_uart *uart, const char *tx, unsigned int length, void (*tx_cb)(struct sol_uart *uart, int status, void *data), const void *data)
{
    struct uart_write_data *write_data;

    SOL_NULL_CHECK(uart, false);

    if (!uart->async.tx_fd_handler) {
        uart->async.tx_fd_handler = sol_fd_add(uart->fd,
            FD_ERROR_FLAGS | SOL_FD_FLAGS_OUT,
            uart_tx_callback, uart);
        SOL_NULL_CHECK(uart->async.tx_fd_handler, false);
    }

    write_data = sol_vector_append(&uart->async.tx_queue);
    SOL_NULL_CHECK_GOTO(write_data, append_write_data_fail);

    write_data->buffer = malloc(length);
    SOL_NULL_CHECK_GOTO(write_data->buffer, malloc_buffer_fail);

    memcpy(write_data->buffer, tx, length);
    write_data->cb = tx_cb;
    write_data->user_data = (void *)data;
    write_data->index = 0;
    write_data->length = length;

    return true;

malloc_buffer_fail:
    sol_vector_del(&uart->async.tx_queue, uart->async.tx_queue.len - 1);
append_write_data_fail:
    sol_fd_del(uart->async.tx_fd_handler);
    uart->async.tx_fd_handler = NULL;
    return false;
}
