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

#pragma once

#include <stdint.h>

#include "sol-gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Pin Multiplexing and Mapping under Soletta.
 */

/**
 * @defgroup PinMux Pin Multiplexing
 * @brief These routines are used for Pin Multiplexing and Mapping under Soletta.
 *
 * @{
 */

/**
 * @brief Flags to describe pin capabilities (as the supported protocols).
 */
enum sol_io_protocol {
    SOL_IO_AIO = 0x01, /**< @brief AIO capability */
    SOL_IO_GPIO = 0x02, /**< @brief GPIO capability */
    SOL_IO_I2C = 0x04, /**< @brief I2C capability */
    SOL_IO_PWM = 0x08, /**< @brief PWM capability */
    SOL_IO_SPI = 0x10, /**< @brief SPI capability */
    SOL_IO_UART = 0x20, /**< @brief UART capability */
};

/**
 * @brief Select Pin Multiplexer instructions of a given board.
 *
 * Searches(and activate if found) for pin multiplexing instructions of the given board.
 * Built-in Pin Multiplexers have priority over modules.
 *
 * @param board The board name being used
 *
 * @return 'true' on success, 'false' otherwise.
 */
bool sol_pin_mux_select_mux(const char *board);

/**
 * @brief Maps a pin label to the parameters necessary so it works on the desired protocol.
 *
 * Find if a given pin labeled 'label' is capable of operate on protocol 'prot' and return
 * the parameters needed to setup the protocol.
 *
 * @param label The label of the pin as see on the board
 * @param prot Protocol on which the pin should operate
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_map(const char *label, const enum sol_io_protocol prot, ...);

/**
 * @brief Setup the given pin to operate as Analog I/O.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure 'device'/'pin' pair to operate as Analog I/O.
 *
 * @param device the aio device number.
 * @param pin the aio pin number.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_aio(int device, int pin);

/**
 * @brief Setup the given pin to operate in the given GPIO configuration.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure 'pin' to operate as configured by 'config'.
 *
 * @param pin the gpio pin number.
 * @param config Desired configuration for the pin.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_gpio(uint32_t pin, const struct sol_gpio_config *config);

/**
 * @brief Setup the pins used of the given i2c bus number to operate in I2C mode.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure the pins used by the given i2c bus
 * to operate in I2C mode.
 *
 * @param bus the i2c bus number.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_i2c(uint8_t bus);

/**
 * @brief Setup the given pin to operate as PWM.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure 'device'/'channel' pair to operate as PWM.
 *
 * @param device the pwm device number.
 * @param channel the channel number on device.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_pwm(int device, int channel);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
