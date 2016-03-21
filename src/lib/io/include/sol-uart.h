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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <sol-common-buildopts.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for UART access under Soletta.
 */

/**
 * @defgroup UART UART
 * @ingroup IO
 *
 * @brief UART (Universal Asynchronous Receiver/Transmitter) API for Soletta.
 *
 * @{
 */

struct sol_uart;

/**
 * @brief Baud rate is the number of times the signal can switch states in one second.
 *
 * Need to be defined to set uart config.
 */
enum sol_uart_baud_rate {
    SOL_UART_BAUD_RATE_9600 = 0,
    SOL_UART_BAUD_RATE_19200,
    SOL_UART_BAUD_RATE_38400,
    SOL_UART_BAUD_RATE_57600,
    SOL_UART_BAUD_RATE_115200
};

/**
 * @brief Amount of stop bits.
 */
enum sol_uart_data_bits {
    SOL_UART_DATA_BITS_8 = 0,
    SOL_UART_DATA_BITS_7,
    SOL_UART_DATA_BITS_6,
    SOL_UART_DATA_BITS_5
};

/**
 * @brief The parity characteristic can be even, odd, or none and it
 * influences last trasmitted bit.
 */
enum sol_uart_parity {
    SOL_UART_PARITY_NONE = 0, /**< no parity is used. */
    SOL_UART_PARITY_EVEN, /**< the last data bit transmitted will be a logical 1  if the data transmitted had an even amount of 0 bits. */
    SOL_UART_PARITY_ODD /**< the last data bit transmitted will be a logical 1 if the data transmitted had an odd amount of 0 bits. */
};

/**
 * @brief Amount of stop bits.
 */
enum sol_uart_stop_bits {
    SOL_UART_STOP_BITS_ONE = 0,
    SOL_UART_STOP_BITS_TWO
};

struct sol_uart_config {
#ifndef SOL_NO_API_VERSION
#define SOL_UART_CONFIG_API_VERSION (1)
    uint16_t api_version;
#endif
    enum sol_uart_baud_rate baud_rate;
    enum sol_uart_data_bits data_bits;
    enum sol_uart_parity parity;
    enum sol_uart_stop_bits stop_bits;
    void (*rx_cb)(void *user_data, struct sol_uart *uart, uint8_t byte_read); /** Set a callback to be called every time a character is received on UART */
    const void *rx_cb_user_data;
    bool flow_control; /** Enables software flow control(XOFF and XON) */
};

/**
 * @brief Open an UART bus.
 *
 * @param port_name the name of UART port, on Linux it should be ttyUSB0 or
 * ttyACM0 in small OSes it should be a id number.
 * @param config The UART bus configuration
 * @return A new UART bus handle
 *
 * @note For now it only supports one user of each port at time, 2 or more users
 * on the same port will cause several concurrency errors.
 */
struct sol_uart *sol_uart_open(const char *port_name, const struct sol_uart_config *config);

/**
 * @brief Close an UART bus.
 *
 * @param uart The UART bus handle
 */
void sol_uart_close(struct sol_uart *uart);

/**
 * @brief Perform a UART asynchronous transmission.
 *
 * @param uart The UART bus handle
 * @param tx The output buffer to be sent
 * @param length number of bytes to be transfer
 * @param tx_cb callback to be called when transmission finish, in case of
 * success the status parameter on tx_cb should be equal to length otherwise
 * an error happen during the transmission
 * @param data the first parameter of tx_cb
 * @return true if transfer was started
 *
 * @note Caller should guarantee that tx buffer will not be freed until
 * callback is called.
 */
bool sol_uart_write(struct sol_uart *uart, const uint8_t *tx, unsigned int length, void (*tx_cb)(void *data, struct sol_uart *uart, uint8_t *tx, int status), const void *data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
