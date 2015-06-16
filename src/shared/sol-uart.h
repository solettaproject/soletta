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

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct sol_uart;

struct sol_uart *sol_uart_open(const char *port_name);
void sol_uart_close(struct sol_uart *uart);

bool sol_uart_set_baud_rate(struct sol_uart *uart, uint32_t baud_rate);
uint32_t sol_uart_get_baud_rate(const struct sol_uart *uart);

bool sol_uart_set_parity_bit(struct sol_uart *uart, bool enable, bool odd_paraty);
bool sol_uart_get_parity_bit_enable(struct sol_uart *uart);
bool sol_uart_get_parity_bit_odd(struct sol_uart *uart);

bool sol_uart_set_data_bits_length(struct sol_uart *uart, uint8_t lenght);
uint8_t sol_uart_get_data_bits_length(struct sol_uart *uart);

bool sol_uart_set_stop_bits_length(struct sol_uart *uart, bool two_bits);
uint8_t sol_uart_get_stop_bits_length(struct sol_uart *uart);

bool sol_uart_set_flow_control(struct sol_uart *uart, bool enable);
bool sol_uart_get_flow_control(struct sol_uart *uart);

/**
 * Write X characters on UART without block the execution, when the
 * transmission finish the callback will be called.
 */
bool sol_uart_write(struct sol_uart *uart, const char *tx, unsigned int length, void (*tx_cb)(struct sol_uart *uart, int status, void *data), const void *data);

/**
 * Set a callback to be called every time a character is received on UART.
 */
bool sol_uart_set_rx_callback(struct sol_uart *uart, void (*rx_cb)(struct sol_uart *uart, char read_char, void *data), const void *data);
void sol_uart_del_rx_callback(struct sol_uart *uart);
