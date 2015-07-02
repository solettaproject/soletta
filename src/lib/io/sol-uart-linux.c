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

#define REG_SET(reg, reg_mask, value) reg = ((reg & ~reg_mask) | value)

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

SOL_API struct sol_uart *
sol_uart_open(const char *port_name)
{
    struct sol_uart *uart;
    struct termios tty;
    char device[PATH_MAX];
    int r;

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
    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Unable to set default configuration of UART.");
        goto tcsetattr_fail;
    }

    tcflush(uart->fd, TCIOFLUSH);
    sol_vector_init(&uart->async.tx_queue, sizeof(struct uart_write_data));

    return uart;

tcsetattr_fail:
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

static speed_t
uint_to_speed(uint32_t baud_rate)
{
    switch (baud_rate) {
    case 230400:
        return B230400;
    case 115200:
        return B115200;
    case 57600:
        return B57600;
    case 38400:
        return B38400;
    case 19200:
        return B19200;
    case 9600:
        return B9600;
    case 4800:
        return B4800;
    case 2400:
        return B2400;
    case 1800:
        return B1800;
    default:
    case 0:
        return B0;
    }
}

SOL_API bool
sol_uart_set_baud_rate(struct sol_uart *uart, uint32_t baud_rate)
{
    struct termios tty;

    SOL_NULL_CHECK(uart, false);

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return false;
    }

    if (cfsetospeed(&tty, uint_to_speed(baud_rate)) != 0)
        goto error_setting_baud_rate;

    if (cfsetispeed(&tty, B0) != 0)
        goto error_setting_baud_rate;

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0)
        goto error_setting_baud_rate;

    return true;

error_setting_baud_rate:
    SOL_ERR("Error setting baud rate.");
    return false;
}

static uint32_t
speed_to_uint(speed_t baud_rate)
{
    switch (baud_rate) {
    case B230400:
        return 230400;
    case B115200:
        return 115200;
    case B57600:
        return 57600;
    case B38400:
        return 38400;
    case B19200:
        return 19200;
    case B9600:
        return 9600;
    case B4800:
        return 4800;
    case B2400:
        return 2400;
    case B1800:
        return 1800;
    default:
    case B0:
        return 0;
    }
}

SOL_API uint32_t
sol_uart_get_baud_rate(const struct sol_uart *uart)
{
    struct termios tty;

    SOL_NULL_CHECK(uart, 0);

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return false;
    }

    return speed_to_uint(cfgetospeed(&tty));
}

SOL_API bool
sol_uart_set_parity_bit(struct sol_uart *uart, bool enable, bool odd_paraty)
{
    struct termios tty;

    SOL_NULL_CHECK(uart, false);

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return false;
    }

    REG_SET(tty.c_cflag, PARENB, enable ? PARENB : 0);
    REG_SET(tty.c_iflag, INPCK, enable ? INPCK : 0);
    REG_SET(tty.c_cflag, PARODD, odd_paraty ? PARODD : 0);

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Error setting parity bit.");
        return false;
    }

    return true;
}

static tcflag_t
uart_get_control_flags(struct sol_uart *uart)
{
    struct termios tty;

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return 0;
    }

    return tty.c_cflag;
}

SOL_API bool
sol_uart_get_parity_bit_enable(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart, false);
    return uart_get_control_flags(uart) & PARENB;
}

SOL_API bool
sol_uart_get_parity_bit_odd(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart, false);
    return uart_get_control_flags(uart) & PARODD;
}

SOL_API bool
sol_uart_set_data_bits_length(struct sol_uart *uart, uint8_t length)
{
    struct termios tty;
    uint32_t reg_length_value = 0;

    SOL_NULL_CHECK(uart, false);
    SOL_INT_CHECK(length, < 5, false);
    SOL_INT_CHECK(length, > 9, false);

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return false;
    }

    switch (length) {
    case 5:
        reg_length_value = CS5;
        break;
    case 6:
        reg_length_value = CS6;
        break;
    case 7:
        reg_length_value = CS7;
        break;
    case 8:
        reg_length_value = CS8;
    }

    REG_SET(tty.c_cflag, CSIZE, reg_length_value);

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Error setting data bit length.");
        return false;
    }

    return true;
}

SOL_API uint8_t
sol_uart_get_data_bits_length(struct sol_uart *uart)
{
    uint32_t reg_length_value;

    SOL_NULL_CHECK(uart, false);

    reg_length_value = uart_get_control_flags(uart) & CSIZE;
    switch (reg_length_value) {
    case CS5:
        return 5;
    case CS6:
        return 6;
    case CS7:
        return 7;
    case CS8:
        return 8;
    default:
        return 0;
    }
}

SOL_API bool
sol_uart_set_stop_bits_length(struct sol_uart *uart, bool two_bits)
{
    struct termios tty;

    SOL_NULL_CHECK(uart, false);

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return false;
    }

    REG_SET(tty.c_cflag, CSTOPB, two_bits ? CSTOPB : 0);

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Error setting stop bit length.");
        return false;
    }

    return true;
}

SOL_API uint8_t
sol_uart_get_stop_bits_length(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart, false);
    return uart_get_control_flags(uart) & CSTOPB ? 2 : 1;
}

SOL_API bool
sol_uart_set_flow_control(struct sol_uart *uart, bool enable)
{
    struct termios tty;

    SOL_NULL_CHECK(uart, false);

    if (tcgetattr(uart->fd, &tty) != 0) {
        SOL_ERR("Unable to get UART settings.");
        return false;
    }

    REG_SET(tty.c_cflag, CRTSCTS, enable ? CRTSCTS : 0);
    REG_SET(tty.c_iflag, (IXON | IXOFF | IXANY),
        enable ? (IXON | IXOFF | IXANY) : 0);

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Error setting flow control.");
        return false;
    }

    return true;
}

SOL_API bool
sol_uart_get_flow_control(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart, false);

    return uart_get_control_flags(uart) & CRTSCTS;
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

SOL_API bool
sol_uart_set_rx_callback(struct sol_uart *uart, void (*rx_cb)(struct sol_uart *uart, char read_char, void *data), const void *data)
{
    SOL_NULL_CHECK(uart, false);
    SOL_EXP_CHECK(uart->async.rx_fd_handler != NULL, false);

    uart->async.rx_fd_handler = sol_fd_add(uart->fd,
        FD_ERROR_FLAGS | SOL_FD_FLAGS_IN,
        uart_rx_callback, uart);
    SOL_NULL_CHECK(uart->async.rx_fd_handler, false);
    uart->async.rx_cb = rx_cb;
    uart->async.rx_user_data = (void *)data;
    return true;
}

SOL_API void
sol_uart_del_rx_callback(struct sol_uart *uart)
{
    SOL_NULL_CHECK(uart);
    SOL_NULL_CHECK(uart->async.rx_fd_handler);

    sol_fd_del(uart->async.rx_fd_handler);
    uart->async.rx_fd_handler = NULL;
}
