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

static void
string_received(void)
{
    static unsigned string_receveid = 0;

    string_receveid++;
    if (string_receveid > 2) {
        sol_quit();
    }
}

static void
uart1_rx(void *data, struct sol_uart *uart, unsigned char read_char)
{
    static int index = 0;
    static char string[64];

    string[index] = (char)read_char;
    if (string[index] == 0) {
        printf("Async transmission received on uart1: %s\n", string);
        string_received();
        index = 0;
    } else {
        index++;
    }
}

static void
uart2_rx(void *data, struct sol_uart *uart, unsigned char read_char)
{
    static int index = 0;
    static char string[64];

    string[index] = (char)read_char;
    if (string[index] == 0) {
        printf("Async transmission received on uart2: %s\n", string);
        string_received();
        index = 0;
    } else {
        index++;
    }
}

static void
uart_tx_completed(void *data, struct sol_uart *uart, int status)
{
    const char *string = data;

    printf("%s\n", string);
}

int
main(int argc, char *argv[])
{
    struct sol_uart *uart1, *uart2;
    struct sol_uart_config config;
    char buf[64];

    sol_init();

    config.api_version = SOL_UART_CONFIG_API_VERSION;
    config.baud_rate = SOL_UART_BAUD_RATE_9600;
    config.data_bits = SOL_UART_DATA_BITS_8;
    config.parity = SOL_UART_PARITY_NONE;
    config.stop_bits = SOL_UART_STOP_BITS_ONE;
    config.flow_control = false;
    config.rx_cb = uart1_rx;
    config.rx_cb_user_data = NULL;
    uart1 = sol_uart_open("ttyUSB0", &config);
    if (!uart1) {
        printf("Unable to get uart1.\n");
        goto error_uart1;
    }

    config.rx_cb = uart2_rx;
    uart2 = sol_uart_open("ttyUSB1", &config);
    if (!uart2) {
        printf("Unable to get uart2.\n");
        goto error;
    }

    sprintf(buf, "Hello");
    sol_uart_write(uart1, (unsigned char *)buf, strlen(buf) + 1,
        uart_tx_completed, "Uart1 transmission completed.");

    sprintf(buf, "async");
    sol_uart_write(uart1, (unsigned char *)buf, strlen(buf) + 1,
        uart_tx_completed, "Uart1 transmission completed.");

    sprintf(buf, "world");
    sol_uart_write(uart2, (unsigned char *)buf, strlen(buf) + 1,
        uart_tx_completed, "Uart2 transmission completed.");

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
