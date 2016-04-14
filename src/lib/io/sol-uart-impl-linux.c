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
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-uart.h"
#include "sol-util-internal.h"
#include "sol-util-file.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

#define FD_ERROR_FLAGS (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)

struct sol_uart {
    int fd;
    struct {
        struct sol_fd *rx_fd_handler;
        void (*rx_cb)(void *data, struct sol_uart *uart);
        const void *rx_user_data;

        struct sol_fd *tx_fd_handler;
        void (*tx_cb)(void *data, struct sol_uart *uart, uint8_t *tx, int status);
        const void *tx_user_data;
        const uint8_t *tx_buffer;
        size_t tx_length, tx_index;
    } async;
};

static bool
uart_rx_callback(void *data, int fd, uint32_t active_flags)
{
    struct sol_uart *uart = data;

    if (active_flags & FD_ERROR_FLAGS) {
        SOL_ERR("Error flag was set on UART file descriptor %d.", fd);
        return true;
    }

    if (active_flags & SOL_FD_FLAGS_IN)
        uart->async.rx_cb((void *)uart->async.rx_user_data, uart);

    return true;
}

SOL_API struct sol_uart *
sol_uart_open(const char *port_name, const struct sol_uart_config *config)
{
    struct sol_uart *uart;
    struct termios tty;
    char device[PATH_MAX];
    int r;
    const speed_t baud_table[] = {
        [SOL_UART_BAUD_RATE_9600] = B9600,
        [SOL_UART_BAUD_RATE_19200] = B19200,
        [SOL_UART_BAUD_RATE_38400] = B38400,
        [SOL_UART_BAUD_RATE_57600] = B57600,
        [SOL_UART_BAUD_RATE_115200] = B115200
    };
    const uint32_t data_bits_table[] = {
        [SOL_UART_DATA_BITS_8] = CS8,
        [SOL_UART_DATA_BITS_7] = CS7,
        [SOL_UART_DATA_BITS_6] = CS6,
        [SOL_UART_DATA_BITS_5] = CS5
    };

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (SOL_UNLIKELY(config->api_version != SOL_UART_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open UART that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_UART_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    SOL_NULL_CHECK(port_name, NULL);

    r = snprintf(device, sizeof(device), "/dev/%s", port_name);
    SOL_INT_CHECK(r, < 0, NULL);
    SOL_INT_CHECK(r, >= (int)sizeof(device), NULL);

    uart = calloc(1, sizeof(struct sol_uart));
    SOL_NULL_CHECK(uart, NULL);

    uart->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC );
    if (uart->fd < 0) {
        SOL_ERR("Unable to open device %s.", device);
        goto open_fail;
    }

    memset(&tty, 0, sizeof(struct termios));

    if (cfsetospeed(&tty, baud_table[config->baud_rate]) != 0)
        goto fail;
    if (cfsetispeed(&tty, B0) != 0)
        goto fail;

    tty.c_cflag |= data_bits_table[config->data_bits];

    if (config->parity != SOL_UART_PARITY_NONE) {
        tty.c_cflag |= PARENB;
        tty.c_iflag |= INPCK;
        if (config->parity == SOL_UART_PARITY_ODD)
            tty.c_cflag |= PARODD;
    }

    if (config->stop_bits == SOL_UART_STOP_BITS_TWO)
        tty.c_cflag |= CSTOPB;

    if (config->flow_control) {
        tty.c_cflag |= CRTSCTS;
        tty.c_iflag |= (IXON | IXOFF | IXANY);
    }

    if (tcsetattr(uart->fd, TCSANOW, &tty) != 0) {
        SOL_ERR("Unable to set UART configuration.");
        goto fail;
    }
    tcflush(uart->fd, TCIOFLUSH);

    if (config->rx_cb) {
        uart->async.rx_fd_handler = sol_fd_add(uart->fd,
            FD_ERROR_FLAGS | SOL_FD_FLAGS_IN, uart_rx_callback, uart);
        if (!uart->async.rx_fd_handler) {
            SOL_ERR("Unable to add file descriptor to watch UART.");
            goto fail;
        }
        uart->async.rx_cb = config->rx_cb;
        uart->async.rx_user_data = config->rx_cb_user_data;
    }

    return uart;

fail:
    close(uart->fd);
open_fail:
    free(uart);
    return NULL;
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
    free(uart);
}

static void
uart_tx_dispatch(struct sol_uart *uart, int status)
{
    uart->async.tx_fd_handler = NULL;
    if (!uart->async.tx_cb)
        return;
    uart->async.tx_cb((void *)uart->async.tx_user_data, uart,
        (uint8_t *)uart->async.tx_buffer, status);
}

static bool
uart_tx_callback(void *data, int fd, uint32_t active_flags)
{
    struct sol_uart *uart = data;
    int ret;

    if (active_flags & FD_ERROR_FLAGS) {
        SOL_ERR("Error flag was set on UART file descriptor %d.", fd);
        ret = -1;
        goto error;
    }

    if (uart->async.tx_index == uart->async.tx_length) {
        uart_tx_dispatch(uart, uart->async.tx_index);
        return false;
    }

    ret = write(fd, uart->async.tx_buffer + uart->async.tx_index,
        uart->async.tx_length - uart->async.tx_index);
    if (ret < 0) {
        SOL_ERR("Error when writing to file descriptor %d.", fd);
        goto error;
    }

    uart->async.tx_index += ret;
    return true;

error:
    uart_tx_dispatch(uart, ret);
    return false;
}

SOL_API int
sol_uart_write(struct sol_uart *uart, const uint8_t *tx, size_t length, void (*tx_cb)(void *data, struct sol_uart *uart, uint8_t *tx, int status), const void *data)
{
    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_EXP_CHECK(uart->async.tx_fd_handler != NULL, -EBUSY);

    uart->async.tx_fd_handler = sol_fd_add(uart->fd,
        FD_ERROR_FLAGS | SOL_FD_FLAGS_OUT, uart_tx_callback, uart);
    SOL_NULL_CHECK(uart->async.tx_fd_handler, -ENOMEM);

    uart->async.tx_buffer = tx;
    uart->async.tx_cb = tx_cb;
    uart->async.tx_user_data = data;
    uart->async.tx_index = 0;
    uart->async.tx_length = length;

    return 0;
}

SOL_API int
sol_uart_read(struct sol_uart *uart, uint8_t *rx, size_t length)
{
    size_t min;
    struct sol_buffer buf;
    int r, total;

    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_NULL_CHECK(rx, -EINVAL);

    r = ioctl(uart->fd, FIONREAD, &total);
    SOL_INT_CHECK(r, < 0, r);
    min = sol_min(length, (size_t)total);
    sol_buffer_init_flags(&buf, rx, min,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    return (int)sol_util_fill_buffer(uart->fd, &buf, min);
}
