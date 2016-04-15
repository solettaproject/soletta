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
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "uart");

#define FD_ERROR_FLAGS (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL)

struct sol_uart {
    struct sol_fd *fd_handler;
    const void *user_data;
    void (*rx_cb)(void *data, struct sol_uart *uart);
    void (*tx_cb)(void *data, struct sol_uart *uart, struct sol_blob *blob, int status);
    struct sol_ptr_vector pending_blobs;
    size_t written; //How many bytes We've written so far for the first element of pending_blobs
    int fd;
};


static bool
uart_fd_handler_callback(void *data, int fd, uint32_t active_flags)
{
    struct sol_uart *uart = data;
    struct sol_blob *blob;
    ssize_t r;

    if (active_flags & FD_ERROR_FLAGS) {
        SOL_ERR("Error flag was set on UART file descriptor %d.", fd);
        return true;
    }

    if (active_flags & SOL_FD_FLAGS_IN) {
        uart->rx_cb((void *)uart->user_data, uart);
        return true;
    }

    //SOL_FD_FLAGS_OUT

    blob = sol_ptr_vector_get_nocheck(&uart->pending_blobs, 0);
    r = write(uart->fd, (char *)blob->mem + uart->written,
        blob->size - uart->written);

    if (r < 0) {
        if (errno != EAGAIN) {
            int aux_errno = errno;
            SOL_WRN("Could not write at the UART fd: %d - Reason:%s", uart->fd,
                sol_util_strerrora(aux_errno));
            if (uart->tx_cb)
                uart->tx_cb((void *)uart->user_data, uart, blob, -aux_errno);
        }
        return false;
    }
    uart->written += r;
    if (uart->written == blob->size) {
        if (uart->tx_cb)
            uart->tx_cb((void *)uart->user_data, uart, blob, 0);
        sol_blob_unref(blob);
        sol_ptr_vector_del(&uart->pending_blobs, 0);
        uart->written = 0;
        if (!sol_ptr_vector_get_len(&uart->pending_blobs))
            sol_fd_remove_flags(uart->fd_handler, SOL_FD_FLAGS_OUT);
    }
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
        uart->fd_handler = sol_fd_add(uart->fd,
            FD_ERROR_FLAGS | SOL_FD_FLAGS_IN, uart_fd_handler_callback, uart);
        if (!uart->fd_handler) {
            SOL_ERR("Unable to add file descriptor to watch UART.");
            goto fail;
        }
        uart->rx_cb = config->rx_cb;
    }

    uart->tx_cb = config->tx_cb;
    sol_ptr_vector_init(&uart->pending_blobs);
    uart->user_data = config->user_data;

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
    struct sol_blob *blob;
    uint16_t i;

    SOL_NULL_CHECK(uart);

    if (uart->fd_handler)
        sol_fd_del(uart->fd_handler);

    SOL_PTR_VECTOR_FOREACH_IDX (&uart->pending_blobs, blob, i)
        sol_blob_unref(blob);
    sol_ptr_vector_clear(&uart->pending_blobs);
    close(uart->fd);
    free(uart);
}

SOL_API int
sol_uart_write(struct sol_uart *uart, struct sol_blob *blob)
{
    int r;
    bool fd_created = false;

    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    if (!uart->fd_handler) {
        uart->fd_handler = sol_fd_add(uart->fd,
            FD_ERROR_FLAGS | SOL_FD_FLAGS_OUT, uart_fd_handler_callback, uart);
        SOL_NULL_CHECK(uart->fd_handler, -ENOMEM);
        fd_created = true;
    } else {
        bool err;

        err = sol_fd_add_flags(uart->fd_handler, SOL_FD_FLAGS_OUT);
        SOL_EXP_CHECK(!err, -EINVAL);
    }

    r = -EOVERFLOW;
    SOL_NULL_CHECK_GOTO(sol_blob_ref(blob), err_ref);

    r = sol_ptr_vector_append(&uart->pending_blobs, blob);
    SOL_INT_CHECK_GOTO(r, < 0, err_append);

    return 0;

err_append:
    sol_blob_unref(blob);
err_ref:
    if (fd_created) {
        sol_fd_del(uart->fd_handler);
        uart->fd_handler = NULL;
    }
    return r;
}

SOL_API int
sol_uart_read(struct sol_uart *uart, struct sol_buffer *buf)
{
    int r, total;

    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);

    r = ioctl(uart->fd, FIONREAD, &total);
    SOL_INT_CHECK(r, < 0, r);
    return (int)sol_util_fill_buffer(uart->fd, buf, total);
}
