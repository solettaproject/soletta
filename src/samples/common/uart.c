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

#include <stdio.h>
#include <string.h>
#include "sol-mainloop.h"
#include "sol-uart.h"
#include "sol-util.h"
#include "sol-str-slice.h"

struct uart_data {
    unsigned int id;
    uint8_t rx_buffer[8];
    size_t rx_index;
};

static void
uart_rx(void *data, struct sol_uart *uart)
{
    struct uart_data *uart_data = data;
    static unsigned char missing_strings = 3;
    int r;

    r = sol_uart_read(uart, uart_data->rx_buffer + uart_data->rx_index,
        sizeof(uart_data->rx_buffer) - uart_data->rx_index);
    if (r < 0) {
        printf("Unable to read from UART%d - Reason: %s\n", uart_data->id,
            sol_util_strerrora(-r));
        return;
    }

    uart_data->rx_index += r;

    while (true) {
        struct sol_str_slice slice;

        char *sep = memchr(uart_data->rx_buffer, '\0', uart_data->rx_index);
        if (!sep)
            break;

        slice.data = (const char *)uart_data->rx_buffer;
        slice.len = sep - slice.data;

        printf("Data received on UART%d: %.*s\n",
               uart_data->id, SOL_STR_SLICE_PRINT(slice));
        missing_strings--;
        if (missing_strings == 0)
            sol_quit();
        memmove(uart_data->rx_buffer, slice.data, slice.len);
        uart_data->rx_index = 0;
    }
}

static void
uart_tx(void *data, struct sol_uart *uart, unsigned char *tx, int status)
{
    static bool missing_tx = true;
    struct uart_data *uart_data = data;

    if (status > 0)
        printf("UART%d data transmitted.\n", uart_data->id);
    else
        printf("UART%d transmission error.\n", uart_data->id);

    if (missing_tx) {
        missing_tx = false;
        sprintf((char *)tx, "async");
        sol_uart_write(uart, tx, strlen((char *)tx) + 1, uart_tx, uart_data);
    }
}

int
main(int argc, char *argv[])
{
    struct sol_uart *uart1, *uart2;
    struct sol_uart_config config;
    char uart1_buffer[8], uart2_buffer[8];
    struct uart_data uart1_data, uart2_data;

    sol_init();

    SOL_SET_API_VERSION(config.api_version = SOL_UART_CONFIG_API_VERSION; )
    config.baud_rate = SOL_UART_BAUD_RATE_9600;
    config.data_bits = SOL_UART_DATA_BITS_8;
    config.parity = SOL_UART_PARITY_NONE;
    config.stop_bits = SOL_UART_STOP_BITS_ONE;
    config.flow_control = false;
    config.rx_cb = uart_rx;
    config.rx_cb_user_data = &uart1_data;
    uart1 = sol_uart_open("ttyUSB0", &config);
    if (!uart1) {
        printf("Unable to get uart1.\n");
        goto error_uart1;
    }
    uart1_data.id = 1;
    uart1_data.rx_index = 0;

    config.rx_cb_user_data = &uart2_data;
    uart2 = sol_uart_open("ttyUSB1", &config);
    if (!uart2) {
        printf("Unable to get uart2.\n");
        goto error;
    }
    uart2_data.id = 2;
    uart2_data.rx_index = 0;

    sprintf(uart1_buffer, "Hello");
    sol_uart_write(uart1, (unsigned char *)uart1_buffer,
        strlen(uart1_buffer) + 1, uart_tx, &uart1_data);

    sprintf(uart2_buffer, "world");
    sol_uart_write(uart2, (unsigned char *)uart2_buffer,
        strlen(uart2_buffer) + 1, uart_tx, &uart2_data);

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
