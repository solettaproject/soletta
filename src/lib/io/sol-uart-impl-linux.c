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

#define DEFAULT_BUFFER_SIZE (4096)

struct sol_uart {
    struct sol_fd *fd_handler;
    const void *user_data;
    ssize_t (*rx_cb)(void *data, struct sol_uart *uart, const struct sol_buffer *buf);
    void (*tx_cb)(void *data, struct sol_uart *uart, struct sol_blob *blob, int status);
    struct sol_timeout *read_timeout;
    struct sol_ptr_vector pending_blobs;
    struct sol_buffer rx;
    size_t tx_size;
    size_t pending_tx;
    size_t written; //How many bytes We've written so far for the first element of pending_blobs
    int fd;
    bool delete_me;
    bool in_use;
};

static bool
read_timeout(void *data)
{
    struct sol_uart *uart = data;
    ssize_t r;
    bool keep_running = true;

    uart->in_use = true;
    r = uart->rx_cb((void *)uart->user_data, uart, &uart->rx);
    uart->in_use = false;
    SOL_INT_CHECK_GOTO(r, < 0, exit);

    if ((size_t)r < uart->rx.used) {
        memmove(uart->rx.data, (char *)uart->rx.data + r, uart->rx.used - r);
        uart->rx.used -= r;
    } else if ((size_t)r >= uart->rx.used)
        uart->rx.used = 0;

    if (!uart->rx.used) {
        keep_running = false;
        uart->read_timeout = NULL;
    }

exit:
    if (uart->delete_me) {
        keep_running = false;
        uart->read_timeout = NULL;
        sol_uart_close(uart);
    }
    return keep_running;
}

static bool
uart_fd_handler_callback(void *data, int fd, uint32_t active_flags)
{
    struct sol_uart *uart = data;
    struct sol_blob *blob;
    int status = 0;
    ssize_t r;

    if (active_flags & FD_ERROR_FLAGS) {
        SOL_ERR("Error flag was set on UART file descriptor %d.", fd);
        return true;
    }

    if ((active_flags & SOL_FD_FLAGS_IN) && uart->rx_cb) {
        size_t remaining = uart->rx.capacity - uart->rx.used;

        if (!remaining && !(uart->rx.flags & SOL_BUFFER_FLAGS_FIXED_CAPACITY)) {
            int err;

            err = sol_buffer_expand(&uart->rx, DEFAULT_BUFFER_SIZE);
            SOL_INT_CHECK(err, < 0, true);
        }

        if (remaining > 0) {
            r = read(uart->fd, sol_buffer_at_end(&uart->rx), remaining);
            if (r < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    return true;
                else {
                    status = errno;
                    SOL_WRN("Could not read from the UART fd: %d - Reason:%s", uart->fd,
                        sol_util_strerrora(status));
                    return true;
                }
            }
        }

        uart->rx.used += r;

        if (uart->rx.used && !uart->read_timeout) {
            uart->read_timeout = sol_timeout_add(0, read_timeout, uart);
            SOL_NULL_CHECK(uart->read_timeout, true);
        } else if (!uart->rx.used && uart->read_timeout) {
            sol_timeout_del(uart->read_timeout);
            uart->read_timeout = NULL;
        }
    }

    //SOL_FD_FLAGS_OUT

    if (!sol_ptr_vector_get_len(&uart->pending_blobs))
        return true;

    blob = sol_ptr_vector_get_no_check(&uart->pending_blobs, 0);
    r = write(uart->fd, (char *)blob->mem + uart->written,
        blob->size - uart->written);

    if (r < 0) {
        if (errno == EAGAIN || errno == EINTR)
            return true;
        else {
            status = errno;
            SOL_WRN("Could not write at the UART fd: %d - Reason:%s", uart->fd,
                sol_util_strerrora(status));
            goto exit;
        }
    }

    uart->written += r;
    uart->pending_tx -= r;
    if (uart->written != blob->size)
        return true;

exit:
    sol_ptr_vector_del(&uart->pending_blobs, 0);
    uart->written = 0;
    if (!sol_ptr_vector_get_len(&uart->pending_blobs))
        sol_fd_remove_flags(uart->fd_handler, SOL_FD_FLAGS_OUT);
    if (uart->tx_cb)
        uart->tx_cb((void *)uart->user_data, uart, blob, status);
    sol_blob_unref(blob);
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
        size_t rx_size;
        void *rx_buf;
        enum sol_buffer_flags flags = SOL_BUFFER_FLAGS_NO_NUL_BYTE | SOL_BUFFER_FLAGS_DEFAULT;

        uart->fd_handler = sol_fd_add(uart->fd,
            FD_ERROR_FLAGS | SOL_FD_FLAGS_IN, uart_fd_handler_callback, uart);
        if (!uart->fd_handler) {
            SOL_ERR("Unable to add file descriptor to watch UART.");
            goto fail;
        }

        uart->rx_cb = config->rx_cb;
        rx_size = config->rx_size;
        if (rx_size)
            flags |= SOL_BUFFER_FLAGS_FIXED_CAPACITY;
        else
            rx_size = DEFAULT_BUFFER_SIZE;

        rx_buf = malloc(rx_size);
        SOL_NULL_CHECK_GOTO(rx_buf, fail);

        sol_buffer_init_flags(&uart->rx, rx_buf, rx_size, flags);
    }

    uart->tx_cb = config->tx_cb;
    sol_ptr_vector_init(&uart->pending_blobs);
    uart->user_data = config->user_data;
    uart->tx_size = config->tx_size;

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

    if (uart->in_use) {
        uart->delete_me = true;
        return;
    }

    uart->in_use = true;

    SOL_PTR_VECTOR_FOREACH_IDX (&uart->pending_blobs, blob, i) {
        if (uart->tx_cb)
            uart->tx_cb((void *)uart->user_data, uart, blob, -ECANCELED);
        sol_blob_unref(blob);
    }

    if (uart->rx.used)
        uart->rx_cb((void *)uart->user_data, uart, &uart->rx);

    if (uart->fd_handler)
        sol_fd_del(uart->fd_handler);

    if (uart->read_timeout)
        sol_timeout_del(uart->read_timeout);

    sol_ptr_vector_clear(&uart->pending_blobs);
    sol_buffer_fini(&uart->rx);
    close(uart->fd);
    free(uart);
}

SOL_API int
sol_uart_write(struct sol_uart *uart, struct sol_blob *blob)
{
    int r;
    size_t total;
    bool fd_created = false;

    SOL_NULL_CHECK(uart, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    if (uart->tx_size) {
        r = sol_util_size_add(uart->pending_tx, blob->size, &total);
        SOL_INT_CHECK(r, < 0, r);

        if (total >= uart->tx_size)
            return -ENOSPC;
    }

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

    uart->pending_tx += blob->size;

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
