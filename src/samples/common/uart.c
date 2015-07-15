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

#include <stdio.h>
#include <string.h>
#include "sol-mainloop.h"
#include "sol-uart.h"

struct uart_data {
    unsigned int id;
    char rx_buffer[64];
    unsigned int rx_index;
};

static void
uart_rx(void *data, struct sol_uart *uart, char read_char)
{
    struct uart_data *uart_data = data;
    static unsigned char missing_strings = 3;

    uart_data->rx_buffer[uart_data->rx_index] = read_char;
    if (read_char == 0) {
        uart_data->rx_index = 0;
        printf("Data received on UART%d: %s\n",
            uart_data->id, uart_data->rx_buffer);
        missing_strings--;
        if (missing_strings == 0)
            sol_quit();
    } else
        uart_data->rx_index++;
}

static void
uart_tx(void *data, struct sol_uart *uart, int status)
{
    static bool missing_tx = true;
    struct uart_data *uart_data = data;

    if (status > 0)
        printf("UART%d data transmitted.\n", uart_data->id);
    else
        printf("UART%d transmission error.\n", uart_data->id);

    if (missing_tx) {
        char buf[16];

        missing_tx = false;
        sprintf(buf, "async");
        sol_uart_write(uart, buf, strlen(buf) + 1, uart_tx, uart_data);
    }
}

int
main(int argc, char *argv[])
{
    struct sol_uart *uart1, *uart2;
    struct uart_data uart1_data, uart2_data;
    char buf[16];

    sol_init();

    uart1 = sol_uart_open("ttyUSB0", SOL_UART_BAUD_RATE_9600, SOL_UART_DEFAULT,
        uart_rx, &uart1_data);
    if (!uart1) {
        printf("Unable to get uart1.\n");
        goto error_uart1;
    }
    uart1_data.id = 1;
    uart1_data.rx_index = 0;

    uart2 = sol_uart_open("ttyUSB1", SOL_UART_BAUD_RATE_9600, SOL_UART_DEFAULT,
        uart_rx, &uart2_data);
    if (!uart2) {
        printf("Unable to get uart2.\n");
        goto error;
    }
    uart2_data.id = 2;
    uart2_data.rx_index = 0;

    sprintf(buf, "Hello");
    sol_uart_write(uart1, buf, strlen(buf) + 1, uart_tx, &uart1_data);

    sprintf(buf, "world");
    sol_uart_write(uart2, buf, strlen(buf) + 1, uart_tx, &uart2_data);

    sol_run();

    sol_shutdown();

    sol_uart_close(uart1);
    sol_uart_close(uart2);

    return 0;

error:
    sol_uart_close(uart2);
error_uart1:
    sol_uart_close(uart1);
    return -1;
}
