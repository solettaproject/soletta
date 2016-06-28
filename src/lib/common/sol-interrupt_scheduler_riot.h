/*
 * This file is part of the Soletta (TM) Project
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

#include "kernel_types.h"
#include "msg.h"
#ifdef USE_GPIO
#include "periph/gpio.h"
#endif

#ifdef USE_UART
#include "periph/uart.h"
#endif

#ifdef NETWORK
#include "net/gnrc.h"
#endif

void sol_interrupt_scheduler_set_pid(kernel_pid_t pid);
kernel_pid_t sol_interrupt_scheduler_get_pid(void);

#ifdef USE_GPIO
int sol_interrupt_scheduler_gpio_init_int(gpio_t dev, gpio_mode_t mode, gpio_flank_t flank, gpio_cb_t cb, const void *arg, void **handler);
void sol_interrupt_scheduler_gpio_stop(gpio_t dev, void *handler);
#endif

#ifdef USE_UART
int sol_interrupt_scheduler_uart_init_int(uart_t uart, uint32_t baudrate, uart_rx_cb_t rx_cb, const void *arg, void **handler);
void sol_interrupt_scheduler_uart_stop(uart_t uart, void *handler);
#endif

#ifdef NETWORK
void sol_network_msg_dispatch(msg_t *msg);
#endif

void sol_interrupt_scheduler_process(msg_t *msg);
